
#include <gdnative_api_struct.gen.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <stdint.h>
#include <string.h>

#include "packet_queue.h"
#include "set.h"

enum POSITION_TYPE {POS_V_PTS, POS_TIME, POS_A_TIME};
typedef struct videodecoder_data_struct {

	godot_object *instance; // Don't clean
	uint8_t *io_buffer;
	AVIOContext *io_ctx;
	AVFormatContext *format_ctx;
	godot_bool input_open;
	int videostream_idx;
	AVCodecContext *vcodec_ctx;
	godot_bool vcodec_open;
	AVFrame *frame_yuv;
	AVFrame *frame_rgb;
	struct SwsContext *sws_ctx;
	uint8_t *frame_buffer;
	int frame_buffer_size;
	godot_pool_byte_array unwrapped_frame;
	godot_real time;

	double audio_time;
	int debug_audio;

	int audiostream_idx;
	AVCodecContext *acodec_ctx;
	godot_bool acodec_open;
	AVFrame *audio_frame;
	void *mix_udata;

	int num_decoded_samples;
	float *audio_buffer;
	int audio_buffer_pos;

	SwrContext *swr_ctx;

	PacketQueue *audio_packet_queue;
	PacketQueue *video_packet_queue;

	enum POSITION_TYPE position_type;
	unsigned long drop_frame;
	unsigned long total_frame;
	unsigned long drop_concurrent;
	char drop_hist[256];

} videodecoder_data_struct;

const godot_int IO_BUFFER_SIZE = 512 * 1024; // File reading buffer of 512 KiB
const godot_int AUDIO_BUFFER_MAX_SIZE = 192000;

const godot_gdnative_core_api_struct *api = NULL;
const godot_gdnative_ext_nativescript_api_struct *nativescript_api = NULL;
const godot_gdnative_ext_videodecoder_api_struct *videodecoder_api = NULL;

extern const godot_videodecoder_interface_gdnative plugin_interface;

static const char *plugin_name = "ffmpeg_videoplayer";
static int num_supported_ext = 0;
static const char **supported_ext = NULL;
static uint64_t _clock_start = 0;

static uint64_t get_ticks_msec() {
	struct timespec tv_now = { 0, 0 };
	clock_gettime(CLOCK_MONOTONIC_RAW, &tv_now);
	uint64_t longtime = ((uint64_t)tv_now.tv_nsec / 1.0e6L) + (uint64_t)tv_now.tv_sec * 1000L;
	longtime -= _clock_start;

	return longtime;
}

// Cleanup should empty the struct to the point where you can open a new file from.
static void _cleanup(videodecoder_data_struct *data) {

	if (data->audio_packet_queue != NULL) {
		packet_queue_deinit(data->audio_packet_queue);
		data->audio_packet_queue = NULL;
	}

	if (data->video_packet_queue != NULL) {
		packet_queue_deinit(data->video_packet_queue);
		data->video_packet_queue = NULL;
	}

	if (data->sws_ctx != NULL) {
		sws_freeContext(data->sws_ctx);
		data->sws_ctx = NULL;
	}

	if (data->audio_frame != NULL) {
		av_frame_unref(data->audio_frame);
		data->audio_frame = NULL;
	}

	if (data->frame_rgb != NULL) {
		av_frame_unref(data->frame_rgb);
		data->frame_rgb = NULL;
	}

	if (data->frame_yuv != NULL) {
		av_frame_unref(data->frame_yuv);
		data->frame_yuv = NULL;
	}

	if (data->frame_buffer != NULL) {
		api->godot_free(data->frame_buffer);
		data->frame_buffer = NULL;
		data->frame_buffer_size = 0;
	}

	if (data->vcodec_ctx != NULL) {
		if (data->vcodec_open) {
			avcodec_close(data->vcodec_ctx);
			data->vcodec_open = GODOT_FALSE;
		}
		avcodec_free_context(&data->vcodec_ctx);
		data->vcodec_ctx = NULL;
	}

	if (data->acodec_ctx != NULL) {
		if (data->acodec_open) {
			avcodec_close(data->acodec_ctx);
			data->vcodec_open = GODOT_FALSE;
			avcodec_free_context(&data->acodec_ctx);
			data->acodec_ctx = NULL;
		}
	}

	if (data->format_ctx != NULL) {
		if (data->input_open) {
			avformat_close_input(&data->format_ctx);
			data->input_open = GODOT_FALSE;
		}
		avformat_free_context(data->format_ctx);
		data->format_ctx = NULL;
	}

	if (data->io_ctx != NULL) {
		avio_context_free(&data->io_ctx);
		data->io_ctx = NULL;
	}

	if (data->io_buffer != NULL) {
		api->godot_free(data->io_buffer);
		data->io_buffer = NULL;
	}

	if (data->audio_buffer != NULL) {
		api->godot_free(data->audio_buffer);
		data->audio_buffer = NULL;
	}

	if (data->swr_ctx != NULL) {
		swr_free(&data->swr_ctx);
		data->swr_ctx = NULL;
	}

	data->time = 0;
	data->videostream_idx = -1;
	data->audiostream_idx = -1;
	data->num_decoded_samples = 0;
	data->audio_buffer_pos = 0;

	data->drop_frame = data->total_frame = 0;
}

static void _unwrap_video_frame(godot_pool_byte_array *dest, AVFrame *frame, int width, int height) {
	int frame_size = width * height * 4;
	if (api->godot_pool_byte_array_size(dest) != frame_size) {
		api->godot_pool_byte_array_resize(dest, frame_size);
	}

	godot_pool_byte_array_write_access *write_access = api->godot_pool_byte_array_write(dest);
	uint8_t *write_ptr = api->godot_pool_byte_array_write_access_ptr(write_access);
	int val = 0;
	for (int y = 0; y < height; y++) {
		memcpy(write_ptr, frame->data[0] + y * frame->linesize[0], width * 4);
		write_ptr += width * 4;
	}
	api->godot_pool_byte_array_write_access_destroy(write_access);
}

static int _interleave_audio_frame(float *dest, AVFrame *audio_frame) {
	float **audio_frame_data = (float **)audio_frame->data;
	int count = 0;
	for (int j = 0; j != audio_frame->nb_samples; j++) {
		for (int i = 0; i != audio_frame->channels; i++) {
			dest[count++] = audio_frame_data[i][j];
		}
	}
	return audio_frame->nb_samples;
}

static void _update_extensions() {
	if (num_supported_ext > 0) return;

	const AVInputFormat *current_fmt = NULL;
	set_t *sup_ext_set = NULL;
	void *iterator_opaque = NULL;
	while ((current_fmt = av_demuxer_iterate(&iterator_opaque)) != NULL) {
		if (current_fmt->extensions != NULL) {
			char *exts = (char *)api->godot_alloc(strlen(current_fmt->extensions) + 1);
			strcpy(exts, current_fmt->extensions);
			char *token = strtok(exts, ",");
			while (token != NULL) {
				sup_ext_set = set_insert(sup_ext_set, token);
				token = strtok(NULL, ", ");
			}
			api->godot_free(exts);
			if (current_fmt->mime_type) {
				char *mime_types = (char *)api->godot_alloc(strlen(current_fmt->mime_type) + 1);
				strcpy(mime_types, current_fmt->mime_type);
				char *token = strtok(mime_types, ",");
				// for some reason the webm extension is missing from the format that supports it
				while (token != NULL) {
					if (strcmp("video/webm", token) == 0) {
						sup_ext_set = set_insert(sup_ext_set, "webm");
					}
					token = strtok(NULL, ",");
				}
				api->godot_free(mime_types);
			}
		}
	}

	list_t ext_list = set_create_list(sup_ext_set);
	num_supported_ext = list_size(&ext_list);
	supported_ext = (const char **)api->godot_alloc(sizeof(char *) * num_supported_ext);
	list_node_t *cur_node = ext_list.start;
	int i = 0;
	while (cur_node != NULL) {
		supported_ext[i] = cur_node->value;
		cur_node->value = NULL;
		cur_node = cur_node->next;
		i++;
	}
	list_free(&ext_list);
	set_free(sup_ext_set);
}

static inline godot_real _avtime_to_sec(int64_t avtime) {
	return avtime / (godot_real)AV_TIME_BASE;
}

static void print_codecs() {

	const AVCodecDescriptor *desc = NULL;
	unsigned nb_codecs = 0, i = 0;
	printf("%s: Supported video codecs:\n", plugin_name);
	while ((desc = avcodec_descriptor_next(desc))) {
		const AVCodec* codec = NULL;
		void* i = NULL;
		bool found = false;

		while ((codec = av_codec_iterate(&i))) {
			if (codec->id == desc->id && av_codec_is_decoder(codec)) {
				if (!found && avcodec_find_decoder(desc->id) || avcodec_find_encoder(desc->id)) {
					printf("%s ", desc->name);
					printf(avcodec_find_decoder(desc->id) ? "D" : ".");
					printf(avcodec_find_encoder(desc->id) ? "E" : ".");
					printf(" (decoders: ");
					found = true;
				}
				if (strcmp(codec->name, desc->name) != 0) {
					printf("%s, ", codec->name);
				}
			}
		}
		if (found) {
			printf(")\n");
		}
	}
}

void GDN_EXPORT godot_gdnative_init(godot_gdnative_init_options *p_options) {
	_clock_start = get_ticks_msec();
	api = p_options->api_struct;

	for (int i = 0; i < api->num_extensions; i++) {
		switch (api->extensions[i]->type) {
			case GDNATIVE_EXT_VIDEODECODER: {
				videodecoder_api = (godot_gdnative_ext_videodecoder_api_struct *)api->extensions[i];
			}; break;
			case GDNATIVE_EXT_NATIVESCRIPT: {
				nativescript_api = (godot_gdnative_ext_nativescript_api_struct *)api->extensions[i];
			}; break;
			default: break;
		}
	}
	print_codecs();
}

void GDN_EXPORT godot_gdnative_terminate(godot_gdnative_terminate_options *p_options) {
	api = NULL;
}

void GDN_EXPORT godot_gdnative_singleton() {
	if (videodecoder_api != NULL) {
		videodecoder_api->godot_videodecoder_register_decoder(&plugin_interface);
	}
}

void *godot_videodecoder_constructor(godot_object *p_instance) {
	videodecoder_data_struct *data = api->godot_alloc(sizeof(videodecoder_data_struct));

	data->instance = p_instance;

	data->io_buffer = NULL;
	data->io_ctx = NULL;

	data->format_ctx = NULL;
	data->input_open = GODOT_FALSE;

	data->videostream_idx = -1;
	data->vcodec_ctx = NULL;
	data->vcodec_open = GODOT_FALSE;

	data->frame_rgb = NULL;
	data->frame_yuv = NULL;
	data->sws_ctx = NULL;

	data->frame_buffer = NULL;
	data->frame_buffer_size = 0;

	data->audiostream_idx = -1;
	data->acodec_ctx = NULL;
	data->acodec_open = GODOT_FALSE;
	data->audio_frame = NULL;
	data->audio_buffer = NULL;

	data->swr_ctx = NULL;

	data->num_decoded_samples = 0;
	data->audio_buffer_pos = 0;

	data->audio_packet_queue = NULL;
	data->video_packet_queue = NULL;

	data->position_type = POS_A_TIME;
	data->time = 0;
	data->audio_time = NAN;


	api->godot_pool_byte_array_new(&data->unwrapped_frame);

	memset(data->drop_hist, 0, sizeof(data->drop_hist));
	// printf("ctor()\n");

	return data;
}

void godot_videodecoder_destructor(void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	_cleanup(data);

	data->instance = NULL;
	api->godot_pool_byte_array_destroy(&data->unwrapped_frame);

	api->godot_free(data);
	data = NULL; // Not needed, but just to be safe.

	if (num_supported_ext > 0) {
		for (int i = 0; i < num_supported_ext; i++) {
			if (supported_ext[i] != NULL) {
				api->godot_free((void *)supported_ext[i]);
			}
		}
		api->godot_free(supported_ext);
		num_supported_ext = 0;
	}

	// printf("dtor()\n");
}

const char **godot_videodecoder_get_supported_ext(int *p_count) {
	_update_extensions();
	*p_count = num_supported_ext;
	return supported_ext;
}

const char *godot_videodecoder_get_plugin_name(void) {
	return plugin_name;
}

godot_bool godot_videodecoder_open_file(void *p_data, void *file) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;

	// printf("open_file()\n");

	// Clean up the previous file.
	_cleanup(data);

	data->io_buffer = (uint8_t *)api->godot_alloc(IO_BUFFER_SIZE * sizeof(uint8_t));
	if (data->io_buffer == NULL) {
		_cleanup(data);
		api->godot_print_warning("Buffer alloc error", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	godot_int read_bytes = videodecoder_api->godot_videodecoder_file_read(file, data->io_buffer, IO_BUFFER_SIZE);

	if (read_bytes < IO_BUFFER_SIZE) {
		// something went wrong, we should be able to read atleast one buffer length.
		_cleanup(data);
		api->godot_print_warning("File less then minimum buffer.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	// Rewind to 0
	videodecoder_api->godot_videodecoder_file_seek(file, 0, SEEK_SET);

	// Determine input format
	AVProbeData probe_data;
	probe_data.buf = data->io_buffer;
	probe_data.buf_size = read_bytes;
	probe_data.filename = "";
	probe_data.mime_type = "";

	AVInputFormat *input_format = NULL;
	input_format = av_probe_input_format(&probe_data, 1);
	if (input_format == NULL) {
		_cleanup(data);
		api->godot_print_warning("Format not recognized.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}
	input_format->flags |= AVFMT_SEEK_TO_PTS;

	printf("Format: %s\n", input_format->long_name);

	data->io_ctx = avio_alloc_context(data->io_buffer, IO_BUFFER_SIZE, 0, file,
			videodecoder_api->godot_videodecoder_file_read, NULL,
			videodecoder_api->godot_videodecoder_file_seek);
	if (data->io_ctx == NULL) {
		_cleanup(data);
		api->godot_print_warning("IO context alloc error.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	data->format_ctx = avformat_alloc_context();
	if (data->format_ctx == NULL) {
		_cleanup(data);
		api->godot_print_warning("Format context alloc error.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	data->format_ctx->pb = data->io_ctx;
	data->format_ctx->flags = AVFMT_FLAG_CUSTOM_IO;
	data->format_ctx->iformat = input_format;

	if (avformat_open_input(&data->format_ctx, "", NULL, NULL) != 0) {
		_cleanup(data);
		api->godot_print_warning("Input stream failed to open", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}
	data->input_open = GODOT_TRUE;

	if (avformat_find_stream_info(data->format_ctx, NULL) < 0) {
		_cleanup(data);
		api->godot_print_warning("Could not find stream info.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	data->videostream_idx = -1; // should be -1 anyway, just being paranoid.
	data->audiostream_idx = -1;
	// find stream
	for (int i = 0; i < data->format_ctx->nb_streams; i++) {
		if (data->format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			data->videostream_idx = i;
		} else if (data->format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			data->audiostream_idx = i;
		}
	}
	if (data->videostream_idx == -1) {
		_cleanup(data);
		api->godot_print_error("Video Stream not found.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}
	if (data->audiostream_idx == -1) {
		api->godot_print_warning("Audio Stream not found.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
	}

	AVCodecParameters *vcodec_param = data->format_ctx->streams[data->videostream_idx]->codecpar;

	AVCodec *vcodec = NULL;
	vcodec = avcodec_find_decoder(vcodec_param->codec_id);
	if (vcodec == NULL) {
		const AVCodecDescriptor *desc = avcodec_descriptor_get(vcodec_param->codec_id);
		char msg[512] = {0};
		snprintf(msg, sizeof(msg) - 1, "Videodecoder %s (%s) not found.", desc->name, desc->long_name);
		api->godot_print_warning(msg, "godot_videodecoder_open_file()", __FILE__, __LINE__);
		_cleanup(data);
		return GODOT_FALSE;
	}

	data->vcodec_ctx = avcodec_alloc_context3(vcodec);
	if (data->vcodec_ctx == NULL) {
		_cleanup(data);
		api->godot_print_warning("Videocodec allocation error.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	if (avcodec_parameters_to_context(data->vcodec_ctx, vcodec_param) < 0) {
		_cleanup(data);
		api->godot_print_warning("Videocodec context init error.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}
	// enable multi-thread decoding based on CPU core count
	data->vcodec_ctx->thread_count = 0;

	if (avcodec_open2(data->vcodec_ctx, vcodec, NULL) < 0) {
		_cleanup(data);
		api->godot_print_warning("Videocodec failed to open.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}
	data->vcodec_open = GODOT_TRUE;


	AVCodecParameters *acodec_param = NULL;
	AVCodec *acodec = NULL;
	if (data->audiostream_idx >= 0) {
		acodec_param = data->format_ctx->streams[data->audiostream_idx]->codecpar;

		acodec = avcodec_find_decoder(acodec_param->codec_id);
		if (acodec == NULL) {
			const AVCodecDescriptor *desc = avcodec_descriptor_get(acodec_param->codec_id);
			char msg[512] = {0};
			snprintf(msg, sizeof(msg) - 1, "Audiodecoder %s (%s) not found.", desc-> name, desc->long_name);
			api->godot_print_warning(msg, "godot_videodecoder_open_file()", __FILE__, __LINE__);
			_cleanup(data);
			return GODOT_FALSE;
		}
		data->acodec_ctx = avcodec_alloc_context3(acodec);
		if (data->acodec_ctx == NULL) {
			_cleanup(data);
			api->godot_print_warning("Audiocodec allocation error.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
			return GODOT_FALSE;
		}

		if (avcodec_parameters_to_context(data->acodec_ctx, acodec_param) < 0) {
			_cleanup(data);
			api->godot_print_warning("Audiocodec context init error.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
			return GODOT_FALSE;
		}

		if (avcodec_open2(data->acodec_ctx, acodec, NULL) < 0) {
			_cleanup(data);
			api->godot_print_warning("Audiocodec failed to open.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
			return GODOT_FALSE;
		}
		data->acodec_open = GODOT_TRUE;
		printf("Channel count: %i\n", data->acodec_ctx->channels);

		data->audio_buffer = (float *)api->godot_alloc(AUDIO_BUFFER_MAX_SIZE * sizeof(float));
		if (data->audio_buffer == NULL) {
			_cleanup(data);
			api->godot_print_warning("Audio buffer alloc failed.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
			return GODOT_FALSE;
		}

		data->audio_frame = av_frame_alloc();
		if (data->audio_frame == NULL) {
			_cleanup(data);
			api->godot_print_warning("Frame alloc fail.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
			return GODOT_FALSE;
		}

		data->swr_ctx = swr_alloc();
		av_opt_set_int(data->swr_ctx, "in_channel_layout", data->acodec_ctx->channel_layout, 0);
		av_opt_set_int(data->swr_ctx, "out_channel_layout", data->acodec_ctx->channel_layout, 0);
		av_opt_set_int(data->swr_ctx, "in_sample_rate", data->acodec_ctx->sample_rate, 0);
		av_opt_set_int(data->swr_ctx, "out_sample_rate", 22050, 0);
		av_opt_set_sample_fmt(data->swr_ctx, "in_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);
		av_opt_set_sample_fmt(data->swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
		swr_init(data->swr_ctx);
	}

	// NOTE: Align of 1 (I think it is for 32 bit alignment.) Doesn't work otherwise
	data->frame_buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGB32,
			data->vcodec_ctx->width, data->vcodec_ctx->height, 1);

	data->frame_buffer = (uint8_t *)api->godot_alloc(data->frame_buffer_size);
	if (data->frame_buffer == NULL) {
		_cleanup(data);
		api->godot_print_warning("Framebuffer alloc fail.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	data->frame_rgb = av_frame_alloc();
	if (data->frame_rgb == NULL) {
		_cleanup(data);
		api->godot_print_warning("Frame alloc fail.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	data->frame_yuv = av_frame_alloc();
	if (data->frame_yuv == NULL) {
		_cleanup(data);
		api->godot_print_warning("Frame alloc fail.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	int width = data->vcodec_ctx->width;
	int height = data->vcodec_ctx->height;
	if (av_image_fill_arrays(data->frame_rgb->data, data->frame_rgb->linesize, data->frame_buffer,
				AV_PIX_FMT_RGB32, width, height, 1) < 0) {
		_cleanup(data);
		api->godot_print_warning("Frame fill.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	data->sws_ctx = sws_getContext(width, height, data->vcodec_ctx->pix_fmt,
			width, height, AV_PIX_FMT_RGB0, SWS_BILINEAR,
			NULL, NULL, NULL);
	if (data->sws_ctx == NULL) {
		_cleanup(data);
		api->godot_print_warning("Swscale context not created.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	data->time = 0;
	data->num_decoded_samples = 0;

	data->audio_packet_queue = packet_queue_init();
	data->video_packet_queue = packet_queue_init();

	data->drop_frame = data->total_frame = 0;
	printf("AUDIO: %i\tVIDEO: %i\n", data->audiostream_idx, data->videostream_idx);

	return GODOT_TRUE;
}

godot_real godot_videodecoder_get_length(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// printf("get_length()\n");

	if (data->format_ctx == NULL) {
		api->godot_print_warning("Format context is null.", "godot_videodecoder_get_length()", __FILE__, __LINE__);
		return -1;
	}

	return data->format_ctx->streams[data->videostream_idx]->duration * av_q2d(data->format_ctx->streams[data->videostream_idx]->time_base);
}

static bool read_frame(videodecoder_data_struct *data) {
	while (data->video_packet_queue->nb_packets < 8)  {
		AVPacket pkt;
		int ret = av_read_frame(data->format_ctx, &pkt);
		if (ret >= 0) {
			if (pkt.stream_index == data->videostream_idx) {
				packet_queue_put(data->video_packet_queue, &pkt);
			} else if (pkt.stream_index == data->audiostream_idx) {
				packet_queue_put(data->audio_packet_queue, &pkt);
			} else {
				av_packet_unref(&pkt);
			}
		} else {
			//char msg[512];
			//sprintf(msg, "av_read_frame returns %d", ret);
			//api->godot_print_warning(msg, "read_frame()", __FILE__, __LINE__);
			return false;
		}
	}
	//printf("A: %i\tV: %i\tread_frame()\n", data->audio_packet_queue->nb_packets, data->video_packet_queue->nb_packets);

	return true;
}

void godot_videodecoder_update(void *p_data, godot_real p_delta) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// during 'update' make sure to use video_pts for the time
	// the godot plugin interface expects this
	data->position_type = POS_V_PTS;

	data->time += p_delta;
	bool print = data->debug_audio > 0;
	double nan = NAN;
	if (!isnan(data->audio_time)) {
		data->audio_time += p_delta;
	}
	if (print) printf("%ld UPDATE: %f (at:%f)\n", get_ticks_msec(), data->time, data->audio_time);
	read_frame(data);
}

godot_pool_byte_array *godot_videodecoder_get_videoframe(void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	AVPacket pkt = {0};
	int ret;
	size_t drop_count = 0;
	// to maintain a decent game frame rate
	// don't let frame decoding take more than this number of ms
	uint64_t max_frame_drop_time = 10;
	uint64_t start = get_ticks_msec();
	//printf("get_videoframe()\n");
retry:
	ret = avcodec_receive_frame(data->vcodec_ctx, data->frame_yuv);
	if (ret == AVERROR(EAGAIN)) {
		// need to call avcodedc_send_packet, get a packet from queue to send it
		while (!packet_queue_get(data->video_packet_queue, &pkt)) {
			//api->godot_print_warning("video packet queue empty", "godot_videodecoder_get_videoframe()", __FILE__, __LINE__);
			if (!read_frame(data))
				return NULL;
		}
		ret = avcodec_send_packet(data->vcodec_ctx, &pkt);
		if (ret < 0) {
			char err[512];
			char msg[768];
			av_strerror(ret, err, sizeof(err) - 1);
			sprintf(msg, "avcodec_send_packet returns %d (%s)", ret, err);
			api->godot_print_error(msg, "godot_videodecoder_get_videoframe()", __FILE__, __LINE__);
			av_packet_unref(&pkt);
			return NULL;
		}
		av_packet_unref(&pkt);
		goto retry;
	} else if (ret < 0) {
		char msg[512];
		sprintf(msg, "avcodec_receive_frame returns %d", ret);
		api->godot_print_error(msg, "godot_videodecoder_get_videoframe()", __FILE__, __LINE__);
		return NULL;
	}

	// frame successfully decoded here, now if it lags behind too much (0.05 sec)
	// let's discard this frame and get the next frame instead
	bool pts_correct = data->frame_yuv->pts == AV_NOPTS_VALUE;
	int64_t pts = pts_correct ? data->frame_yuv->pkt_dts : data->frame_yuv->pts;

	double ts = pts * av_q2d(data->format_ctx->streams[data->videostream_idx]->time_base);
	bool audio_drop = false;

	if (pts_correct && !isnan(data->audio_time) && data->audiostream_idx >= 0) {
		double audio_offset = data->time - data->audio_time;
		if (fabs(audio_offset) > 0.05) {
			if (audio_offset < 0) {
				audio_drop = true;
			} else {
				printf("Excessive audio offset: (%f) v:%f .. a:%f\n", audio_offset, ts, data->audio_time);
			}
		}
	}

	data->total_frame++;

	const static double diff_tolerance = 0.05;
	double drop_time = data->time;//!isnan(data->audio_time) ? data->time : data->audio_time;
	bool drop = ts < drop_time - diff_tolerance;
	data->drop_hist[(data->total_frame + 1) % (sizeof(data->drop_hist)-1)] = '<';
	data->drop_hist[data->total_frame % (sizeof(data->drop_hist)-1)] = drop ? 'x' : '.';
	data->drop_concurrent = drop ? data->drop_concurrent + 1 : 0;
	uint64_t drop_duration = get_ticks_msec() - start;
	if (drop && drop_duration > max_frame_drop_time) {
		data->debug_audio = 5;
		// hack to get video_stream_gdnative to stop asking for frames.
		// stop trusting frame_pts until the next time get_videoframe is called.
		// this will unblock VideoStreamPlaybackGDNative::update() which keeps calling get_texture() until the time matches
		char msg[512];
		sprintf(msg, "Slow CPU? Dropped frames for %ldms frame dropped last %ld: %ld/%ld (%.1f%% %s) pts=%.1f t=%.1f %s",
			drop_duration,
			data->drop_concurrent,
			data->drop_frame,
			data->total_frame,
			100.0 * data->drop_frame / data->total_frame,
			data->drop_hist,
			ts, data->time, audio_drop ? "(audio)" : "");
		api->godot_print_warning(msg, "godot_videodecoder_get_videoframe()", __FILE__, __LINE__);
	} else if (drop) {
		data->drop_frame++;
		av_packet_unref(&pkt);
		goto retry;
	}

	sws_scale(data->sws_ctx, (uint8_t const *const *)data->frame_yuv->data, data->frame_yuv->linesize, 0,
			data->vcodec_ctx->height, data->frame_rgb->data, data->frame_rgb->linesize);

	_unwrap_video_frame(&data->unwrapped_frame, data->frame_rgb, data->vcodec_ctx->width, data->vcodec_ctx->height);
	av_packet_unref(&pkt);
	// don't use video pts for playback time next time godot calls.
	// otherwise godot will keep retrying, and we've already handled frame skipping
	data->position_type = POS_TIME;
	return &data->unwrapped_frame;
}

static void _print_audio_buffer(videodecoder_data_struct *data, int size) {
	char vis[256] = "";
	int i;
	bool empty = true;
	int j = 0;
	int k = 0;
	float s = 0;
	const int s_per_char = 4;
	const char s_chars[] = " _.-'^";
	const int s_chars_len = strlen(s_chars);
	float s_ceil = s_per_char * 0.05;
	for (size_t i = 0; i < size; ++i) {
		s += fabs(data->audio_buffer[data->acodec_ctx->channels * data->audio_buffer_pos + i]);
		if (s != 0) {
			empty = false;
		}
		++j;
		if (j > s_per_char) {
			j = 0;
			char ch = ' ';
			if (s != 0) {
				s = s;
				int idx = (int)ceil(s / s_ceil - 0.01);
				if (idx >= s_chars_len) {
					idx = s_chars_len - 1;
				} else if (idx < 0) {
					idx = 0;
				}
				ch = s_chars[idx];
			}
			vis[k] = ch;
			++k;
			if (k >= sizeof(vis)) {
				break;
			}
		}
		s = 0;
	}
	if (k > sizeof(vis) - 1) {
		k = sizeof(vis) - 1;
	}
	vis[k] = 0;
	if (empty) {
		vis[0] = 0;
	}
	printf("\n\t[SEND p:%d s:%d %s] ", data->audio_buffer_pos, size, vis);
}

godot_int godot_videodecoder_get_audio(void *p_data, float *pcm, int num_samples) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	if (data->audiostream_idx < 0)
		return 0;
	if (isnan(data->audio_time)) {
		data->debug_audio = 25;
	}
	bool first_frame = true;
	bool print = data->debug_audio > 0;
	uint64_t start = get_ticks_msec();
	if (print) {
		--data->debug_audio;
	}
	float diff_tolerance = 0.1;
	float audio_offset = 0;
	bool audio_wait = isnan(data->audio_time) || data->audio_time > data->time + audio_offset;
	if (print) {
		printf("%d %ld: aw:%d audio decoded:%d ", data->debug_audio, start, audio_wait, data->num_decoded_samples);
	}
	const int total_req_samples = num_samples;
	int pcm_offset = 0;
	// don't send any pcm data if the frame hasn't started yet
	double ats = data->audio_frame->pts * av_q2d(data->format_ctx->streams[data->audiostream_idx]->time_base);
	if (audio_wait && data->num_decoded_samples) {
		if (ats > data->time) {
			if (print) printf("\n\t[WAIT %f > %f]\n", ats, data->time);
			return 0;
		}
		if (print) printf(" [!WAIT %f <= %f]", ats, data->time);
	}
	bool skip = audio_wait && data->time - ats > diff_tolerance;
	if (skip) {
		// skip the already decoded samples because it's too late.
		if (print) {
			printf(" [SKIP %d (%f > %f)] ", data->num_decoded_samples, data->time - ats, diff_tolerance);
		}
		data->num_decoded_samples = 0;
	}
	int to_send_total = 0;
	int to_send = (num_samples < data->num_decoded_samples) ? num_samples : data->num_decoded_samples;

	if (to_send > 0) {
		if (print) {
			_print_audio_buffer(data, to_send);
			printf("[OLD %d]", to_send);
		}
		memcpy(pcm, data->audio_buffer + data->acodec_ctx->channels * data->audio_buffer_pos, sizeof(float) * to_send * data->acodec_ctx->channels);
		// printf("Copy 0:%i from %i:%i\n", to_send, data->audio_buffer_pos, data->audio_buffer_pos + to_send);
		pcm_offset += to_send;
		num_samples -= to_send;
		data->num_decoded_samples -= to_send;
		data->audio_buffer_pos += to_send;
		to_send_total += to_send;
	}
	if (print) printf(" [NS %d DS %d] ", num_samples, data->num_decoded_samples);
	while (num_samples > 0) {
		if (data->num_decoded_samples <= 0) {
			AVPacket pkt;

			int ret;
retry_audio:
			if (print) printf(" [RECF %d] ", to_send);
			ret = avcodec_receive_frame(data->acodec_ctx, data->audio_frame);
			if (ret == AVERROR(EAGAIN)) {
				// need to call avcodec_send_packet, get a packet from queue to send it
				if (!packet_queue_get(data->audio_packet_queue, &pkt)) {
					if (print) {
						printf("!packet_queue_get %d aw:%d\n", total_req_samples - num_samples, audio_wait);
					}
					if (total_req_samples == num_samples) {
						// if we haven't got any on-time audio yet, then the audio_time counter is meaningless.
						data->audio_time = NAN;
					} else {
						printf("!packet_queue_get (after samples) %d aw:%d", total_req_samples - num_samples, audio_wait);
					}
					return total_req_samples - num_samples;
				}
				ret = avcodec_send_packet(data->acodec_ctx, &pkt);
				if (ret < 0) {
					char msg[512];
					sprintf(msg, "avcodec_send_packet returns %d", ret);
					api->godot_print_error(msg, "godot_videodecoder_get_audio()", __FILE__, __LINE__);
					av_packet_unref(&pkt);
					return total_req_samples - num_samples;
				}
				av_packet_unref(&pkt);
				goto retry_audio;
			} else if (ret < 0) {
				char msg[512];
				sprintf(msg, "avcodec_receive_frame returns %d", ret);
				api->godot_print_error(msg, "godot_videodecoder_get_audio()", __FILE__, __LINE__);
				return total_req_samples - num_samples;
			}
			// only set the audio frame time if this is the first frame we've decoded this update.
			// any remaining frames are going into a buffer anyways
			ats = data->audio_frame->pts * av_q2d(data->format_ctx->streams[data->audiostream_idx]->time_base);
			if (first_frame) {
				data->audio_time = ats;
				first_frame = false;
				if (print) printf(" [SET AT %f] ", data->audio_time);
			}
			// decoded audio ready here
			data->num_decoded_samples = swr_convert(data->swr_ctx, (uint8_t **)&data->audio_buffer, data->audio_frame->nb_samples, (const uint8_t **)data->audio_frame->extended_data, data->audio_frame->nb_samples);
			// data->num_decoded_samples = _interleave_audio_frame(data->audio_buffer, data->audio_frame);
			data->audio_buffer_pos = 0;
		}

		skip = audio_wait && data->time - ats > diff_tolerance;
		if (skip) {
			// skip samples if the frame time is too far in the past
			data->num_decoded_samples = 0;
			printf(" [SKIP %d (%f > %f)] ", data->num_decoded_samples, data->time - ats, diff_tolerance);
		}
		// don't send any pcm data if the first frame hasn't started yet
		if (audio_wait && data->audio_time > data->time) {
			if (print) {
				if (skip) {
				} else {
					printf("\n\t[WAIT %f > %f] ", data->audio_time, data->time);
				}
			}
			to_send = 0;
			data->audio_time = NAN;
			break;
		}
		if (print && audio_wait) printf(" [!WAIT %f <= %f] ", data->audio_time, data->time);
		to_send = num_samples < data->num_decoded_samples ? num_samples : data->num_decoded_samples;
		if (to_send > 0) {
			if (print) _print_audio_buffer(data, to_send);
			memcpy(pcm + pcm_offset * data->acodec_ctx->channels, data->audio_buffer + data->acodec_ctx->channels * data->audio_buffer_pos, sizeof(float) * to_send * data->acodec_ctx->channels);
			// printf("Copy %i:%i from %i:%i\n", pcm_offset, pcm_offset + to_send, data->audio_buffer_pos, data->audio_buffer_pos + to_send);
			pcm_offset += to_send;
			num_samples -= to_send;
			data->num_decoded_samples -= to_send;
			data->audio_buffer_pos += to_send;
			to_send_total += to_send;
		}
	}

	if (print) {
		double v_pts = data->frame_yuv->pts * av_q2d(data->format_ctx->streams[data->videostream_idx]->time_base);
		double a_pts = data->audio_frame->pts * av_q2d(data->format_ctx->streams[data->audiostream_idx]->time_base);
		double duration = (double)to_send / (double)data->audio_frame->sample_rate;
		printf("\n\tds:%d send:%d t:%f at:%f (o:%f) vp:%f ap:%f + %f (o:%f) leftover dec:%d req_ns:%d goodtil:%ld\n",
			data->num_decoded_samples, to_send_total, data->time, data->audio_time, data->time - data->audio_time,
			v_pts, a_pts,
			duration, v_pts - a_pts,
			data->num_decoded_samples, num_samples, (uint64_t)(duration * 1000) + start);
	}

	return to_send_total;
}

godot_real godot_videodecoder_get_playback_position(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;

	if (data->format_ctx) {
		bool use_v_pts = data->frame_yuv->pts != AV_NOPTS_VALUE && data->position_type == POS_V_PTS;
		bool use_a_time = data->position_type == POS_A_TIME;
		data->position_type = POS_TIME;

		bool print = data->debug_audio > 0;
		if (use_v_pts) {
			double pts = (double)data->frame_yuv->pts;
			double audio_offset = 0;// isnan(data->audio_time) ? 0 : data->time - data->audio_time;
			pts *= av_q2d(data->format_ctx->streams[data->videostream_idx]->time_base);

			if (print) printf("%ld get_playback_pos:V %f\n", get_ticks_msec(), pts + audio_offset);
			return (godot_real)pts + audio_offset;
		} else {
			if (!isnan(data->audio_time) && use_a_time) {
				if (print) printf("%ld get_playback_pos:A %f\n", get_ticks_msec(), data->audio_time);
				return (godot_real)data->audio_time;
			}
			if (print) printf("%ld get_playback_pos:T %f\n", get_ticks_msec(), data->time);
			return (godot_real)data->time;
		}
	}
	return (godot_real)0;
}

static void flush_frames(AVCodecContext* ctx) {
	/**
	* from https://www.ffmpeg.org/doxygen/4.1/group__lavc__encdec.html
	* End of stream situations. These require "flushing" (aka draining) the codec, as the codec might buffer multiple frames or packets internally for performance or out of necessity (consider B-frames). This is handled as follows:
	* Instead of valid input, send NULL to the avcodec_send_packet() (decoding) or avcodec_send_frame() (encoding) functions. This will enter draining mode.
	* Call avcodec_receive_frame() (decoding) or avcodec_receive_packet() (encoding) in a loop until AVERROR_EOF is returned. The functions will not return AVERROR(EAGAIN), unless you forgot to enter draining mode.
	* Before decoding can be resumed again, the codec has to be reset with avcodec_flush_buffers().
	*/
	int ret = avcodec_send_packet(ctx, NULL);
	AVFrame frame = {0};
	if (ret <= 0) {
		do {
			ret = avcodec_receive_frame(ctx, &frame);
		} while (ret != AVERROR_EOF);
	}
}

void godot_videodecoder_seek(void *p_data, godot_real p_time) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// Hack to find the end of the video. Really VideoPlayer should expose this!
	if (p_time < 0) {
		p_time = _avtime_to_sec(data->format_ctx->duration);
	}
	int64_t seek_target = p_time * AV_TIME_BASE;
	// seek within 10 seconds of the selected spot.
	int64_t margin = 10 * AV_TIME_BASE;

	// printf("seek(): %fs = %lld\n", p_time, seek_target);
	int ret = avformat_seek_file(data->format_ctx, -1, seek_target - margin, seek_target, seek_target, 0);
	if (ret < 0) {
		api->godot_print_warning("avformat_seek_file() can't seek backward?", "godot_videodecoder_seek()\n", __FILE__, __LINE__);
		ret = avformat_seek_file(data->format_ctx, -1, seek_target - margin, seek_target, seek_target + margin, 0);
	}
	if (ret < 0) {
		api->godot_print_error("avformat_seek_file() failed", "godot_videodecoder_seek()\n", __FILE__, __LINE__);
	} else {
		packet_queue_flush(data->video_packet_queue);
		packet_queue_flush(data->audio_packet_queue);
		flush_frames(data->vcodec_ctx);
		avcodec_flush_buffers(data->vcodec_ctx);
		if (data->acodec_ctx) {
			flush_frames(data->acodec_ctx);
			avcodec_flush_buffers(data->acodec_ctx);
		}
		data->num_decoded_samples = 0;
		data->audio_buffer_pos = 0;
		// if we aren't seeking to the end, use the actual keyframe time.
		data->time = p_time;
		data->position_type = POS_A_TIME;
		data->audio_time = NAN;
		printf("seek %f\n", p_time);
	}
}

/* ---------------------- TODO ------------------------- */

void godot_videodecoder_set_audio_track(void *p_data, godot_int p_audiotrack) {
	//videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	//printf("set_audio_track(): NOT IMPLEMENTED\n");
}

godot_int godot_videodecoder_get_channels(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// printf("get_channels()\n");

	if (data->acodec_ctx != NULL) {
		return data->acodec_ctx->channels;
	}
	return 0;
}

godot_int godot_videodecoder_get_mix_rate(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// printf("get_mix_rate(): FIXED to 22050\n");

	if (data->acodec_ctx != NULL) {
		return 22050; // Sample rate of 22050 is standard on the decode.
	}
	return 0;
}

godot_vector2 godot_videodecoder_get_texture_size(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	godot_vector2 vec;

	if (data->vcodec_ctx != NULL) {
		api->godot_vector2_new(&vec, data->vcodec_ctx->width, data->vcodec_ctx->height);
	}
	return vec;
}

const godot_videodecoder_interface_gdnative plugin_interface = {
	GODOTAV_API_MAJOR, GODOTAV_API_MINOR,
	NULL,
	godot_videodecoder_constructor,
	godot_videodecoder_destructor,
	godot_videodecoder_get_plugin_name,
	godot_videodecoder_get_supported_ext,
	godot_videodecoder_open_file,
	godot_videodecoder_get_length,
	godot_videodecoder_get_playback_position,
	godot_videodecoder_seek,
	godot_videodecoder_set_audio_track,
	godot_videodecoder_update,
	godot_videodecoder_get_videoframe,
	godot_videodecoder_get_audio,
	godot_videodecoder_get_channels,
	godot_videodecoder_get_mix_rate,
	godot_videodecoder_get_texture_size
};
