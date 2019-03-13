
#include <gdnative_api_struct.gen.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <stdint.h>

#include "packet_queue.h"

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

	godot_real video_pts;
	godot_real audio_pts;

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

} videodecoder_data_struct;

const godot_int IO_BUFFER_SIZE = 64 * 1024; // File reading buffer of 64 KiB?
const godot_int AUDIO_BUFFER_MAX_SIZE = 192000;

const godot_gdnative_core_api_struct *api = NULL;
const godot_gdnative_ext_nativescript_api_struct *nativescript_api = NULL;
const godot_gdnative_ext_videodecoder_api_struct *videodecoder_api = NULL;

extern const godot_videodecoder_interface_gdnative plugin_interface;

static const char *plugin_name = "test_plugin";
static const int num_supported_ext = 2;
static const char *supported_ext[] = { "mp4", "mov" };

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
		}
		avcodec_free_context(&data->acodec_ctx);
		data->acodec_ctx = NULL;
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

static bool _decode_packet(AVFrame *dest, AVPacket *pkt, AVCodecContext *ctx) {
	int x = AVERROR(EAGAIN);
	while (x == AVERROR(EAGAIN)) {
		if (avcodec_send_packet(ctx, pkt) >= 0) {
			x = avcodec_receive_frame(ctx, dest);
		}
	}
	return !x;
}

static inline godot_real _avtime_to_sec(int64_t avtime) {
	return avtime / (godot_real)AV_TIME_BASE;
}

void GDN_EXPORT godot_gdnative_init(godot_gdnative_init_options *p_options) {
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

	data->time = 0;

	api->godot_pool_byte_array_new(&data->unwrapped_frame);

	// DEBUG
	printf("ctor()\n");

	return data;
}

void godot_videodecoder_destructor(void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	_cleanup(data);

	data->instance = NULL;
	api->godot_pool_byte_array_destroy(&data->unwrapped_frame);

	api->godot_free(data);
	data = NULL; // Not needed, but just to be safe.

	// DEBUG
	printf("dtor()\n");
}

const char **godot_videodecoder_get_supported_ext(int *p_count) {
	*p_count = num_supported_ext;
	return supported_ext;
}

const char *godot_videodecoder_get_plugin_name(void) {
	return plugin_name;
}

godot_bool godot_videodecoder_open_file(void *p_data, void *file) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;

	// DEBUG
	printf("open_file()\n");

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
		api->godot_print_warning("File less that minimum buffer.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
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
		api->godot_print_warning("Video Stream not found.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}
	if (data->audiostream_idx == -1) {
		_cleanup(data);
		api->godot_print_warning("Audio Stream not found.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	AVCodecParameters *vcodec_param = data->format_ctx->streams[data->videostream_idx]->codecpar;
	AVCodecParameters *acodec_param = data->format_ctx->streams[data->audiostream_idx]->codecpar;

	AVCodec *vcodec = NULL;
	vcodec = avcodec_find_decoder(vcodec_param->codec_id);
	if (vcodec == NULL) {
		_cleanup(data);
		api->godot_print_warning("Videodecoder not found.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	AVCodec *acodec = NULL;
	acodec = avcodec_find_decoder(acodec_param->codec_id);
	if (acodec == NULL) {
		_cleanup(data);
		api->godot_print_warning("Audiodecoder not found.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
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

	if (avcodec_open2(data->vcodec_ctx, vcodec, NULL) < 0) {
		_cleanup(data);
		api->godot_print_warning("Videocodec failed to open.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}
	data->vcodec_open = GODOT_TRUE;

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

	data->audio_frame = av_frame_alloc();
	if (data->audio_frame == NULL) {
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

	data->audio_buffer = (float *)api->godot_alloc(AUDIO_BUFFER_MAX_SIZE * sizeof(float));
	if (data->audio_buffer == NULL) {
		_cleanup(data);
		api->godot_print_warning("Audio buffer alloc failed.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
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

	data->time = 0;
	data->num_decoded_samples = 0;

	data->audio_packet_queue = packet_queue_init();
	data->video_packet_queue = packet_queue_init();

	// printf("AUDIO: %i\tVIDEO: %i\n", data->audiostream_idx, data->videostream_idx);

	return GODOT_TRUE;
}

godot_real godot_videodecoder_get_length(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;

	// DEBUG
	printf("get_length()\n");

	if (data->format_ctx == NULL) {
		api->godot_print_warning("Format context is null.", "godot_videodecoder_get_length()", __FILE__, __LINE__);
		return -1;
	}

	return data->format_ctx->streams[data->videostream_idx]->duration * av_q2d(data->format_ctx->streams[data->videostream_idx]->time_base);
}

void godot_videodecoder_update(void *p_data, godot_real p_delta) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;

	data->time += p_delta;

	while (data->video_packet_queue->nb_packets < 3) {
		AVPacket pkt;
		if (av_read_frame(data->format_ctx, &pkt) >= 0) {
			if (pkt.stream_index == data->videostream_idx) {
				packet_queue_put(data->video_packet_queue, &pkt);
			} else if (pkt.stream_index == data->audiostream_idx) {
				packet_queue_put(data->audio_packet_queue, &pkt);
			} else {
				av_packet_unref(&pkt);
			}
		} else {
			break;
		}
	}

	// printf("A: %i\t", data->audio_packet_queue->nb_packets);

	// DEBUG Yes. This function works. No more polluting the log.
	// printf("update()\n");
}

godot_pool_byte_array *godot_videodecoder_get_videoframe(void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	AVPacket pkt;

	if (!packet_queue_get(data->video_packet_queue, &pkt)) {
		return NULL;
	}

	_decode_packet(data->frame_yuv, &pkt, data->vcodec_ctx);

	sws_scale(data->sws_ctx, (uint8_t const *const *)data->frame_yuv->data, data->frame_yuv->linesize, 0,
			data->vcodec_ctx->height, data->frame_rgb->data, data->frame_rgb->linesize);

	_unwrap_video_frame(&data->unwrapped_frame, data->frame_rgb, data->vcodec_ctx->width, data->vcodec_ctx->height);
	av_packet_unref(&pkt);

	return &data->unwrapped_frame;
}

godot_int godot_videodecoder_get_audio(void *p_data, float *pcm, int num_samples) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;

	const int total_to_send = num_samples;
	int pcm_offset = 0;

	int to_send = (num_samples < data->num_decoded_samples) ? num_samples : data->num_decoded_samples;
	if (to_send > 0) {
		memcpy(pcm, data->audio_buffer + data->acodec_ctx->channels * data->audio_buffer_pos, sizeof(float) * to_send * data->acodec_ctx->channels);
		// printf("Copy 0:%i from %i:%i\n", to_send, data->audio_buffer_pos, data->audio_buffer_pos + to_send);
		pcm_offset += to_send;
		num_samples -= to_send;
		data->num_decoded_samples -= to_send;
		data->audio_buffer_pos += to_send;
	}

	while (num_samples > 0) {
		if (data->num_decoded_samples <= 0) {
			AVPacket pkt;
			if (packet_queue_get(data->audio_packet_queue, &pkt) && _decode_packet(data->audio_frame, &pkt, data->acodec_ctx)) {
				data->num_decoded_samples = swr_convert(data->swr_ctx, (uint8_t **)&data->audio_buffer, data->audio_frame->nb_samples, (const uint8_t **)data->audio_frame->extended_data, data->audio_frame->nb_samples);
				// data->num_decoded_samples = _interleave_audio_frame(data->audio_buffer, data->audio_frame);
				data->audio_buffer_pos = 0;
			} else {
				return total_to_send - num_samples;
			}
		}

		to_send = (num_samples < data->num_decoded_samples) ? num_samples : data->num_decoded_samples;
		if (to_send > 0) {
			memcpy(pcm + pcm_offset * data->acodec_ctx->channels, data->audio_buffer + data->acodec_ctx->channels * data->audio_buffer_pos, sizeof(float) * to_send * data->acodec_ctx->channels);
			// printf("Copy %i:%i from %i:%i\n", pcm_offset, pcm_offset + to_send, data->audio_buffer_pos, data->audio_buffer_pos + to_send);
			pcm_offset += to_send;
			num_samples -= to_send;
			data->num_decoded_samples -= to_send;
			data->audio_buffer_pos += to_send;
		}
	}

	return total_to_send;
}

godot_real godot_videodecoder_get_playback_position(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// DEBUG
	// printf("get_playback_position()\n");

	if (data->format_ctx) {
		double pts = (double)data->frame_yuv->pts;
		pts *= av_q2d(data->format_ctx->streams[data->videostream_idx]->time_base);
		return (godot_real)pts;
	}
	return (godot_real)0;
}

void godot_videodecoder_seek(void *p_data, godot_real p_time) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// DEBUG
	printf("seek()\n");
	// DEBUG
}

/* ---------------------- TODO ------------------------- */

void godot_videodecoder_set_audio_track(void *p_data, godot_int p_audiotrack) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// DEBUG
	printf("set_audio_track()\n");
	// DEBUG
}

godot_int godot_videodecoder_get_channels(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// DEBUG
	printf("get_channels()\n");

	if (data->acodec_ctx != NULL) {
		return data->acodec_ctx->channels;
	}
	// DEBUG
	return 0;
}

godot_int godot_videodecoder_get_mix_rate(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// DEBUG
	printf("get_mix_rate()\n");

	if (data->acodec_ctx != NULL) {
		return data->acodec_ctx->sample_rate;
	}
	// DEBUG
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
