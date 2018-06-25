
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
	uint8_t *io_buffer; // CLEANUP
	AVIOContext *io_ctx; // CLEANUP
	AVFormatContext *format_ctx; // CLEANUP
	godot_bool input_open;
	int videostream_idx;
	AVCodecContext *vcodec_ctx; // CLEANUP
	godot_bool vcodec_open;
	AVFrame *frame_yuv; // CLEANUP
	AVFrame *frame_rgb; // CLEANUP
	struct SwsContext *sws_ctx; // CLEANUP
	uint8_t *frame_buffer;
	int frame_buffer_size;
	godot_pool_byte_array unwrapped_frame;
	AVPacket packet;
	godot_real time;

	int audiostream_idx;
	AVCodecContext *acodec_ctx;
	godot_bool acodec_open;
	AVFrame *audio_frame;
	GDNativeAudioMixCallback mix_callback;
	void *mix_udata;

	int audio_to_send;
	float *audio_buffer; // CLEANUP
	int audio_buffer_pos;

	SwrContext *swr_ctx;

	PacketQueue *packet_queue;
	bool packet_queue_alloc;

} videodecoder_data_struct;

const godot_int IO_BUFFER_SIZE = 64 * 1024; // File reading buffer of 64 KiB?
const godot_int AUDIO_BUFFER_MAX_SIZE = 192000;

const godot_gdnative_core_api_struct *api = NULL;
const godot_gdnative_ext_nativescript_api_struct *nativescript_api = NULL;
const godot_gdnative_ext_videodecoder_api_struct *videodecoder_api = NULL;

// Cleanup should empty the struct to the point where you can open a new file from.
static void _cleanup(videodecoder_data_struct *data) {

	if (data->packet_queue != NULL) {
		packet_queue_deinit(data->packet_queue);
		data->packet_queue = NULL;
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
		// swr_ctx sets to null
		data->swr_ctx = NULL;
	}

	data->time = 0;
	data->videostream_idx = -1;
	data->audiostream_idx = -1;
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

	int count = 0;
	for (int j = 0; j != audio_frame->nb_samples; j++) {
		for (int i = 0; i != audio_frame->channels; i++) {
			dest[count++] = audio_frame->data[i][j];
		}
	}
	return audio_frame->nb_samples;
}

static inline godot_real _avtime_to_sec(int64_t avtime) {
	return (1000 * avtime) / (godot_real)AV_TIME_BASE;
}

extern const godot_videodecoder_interface_gdnative plugin_interface;

static char *plugin_name = "test_plugin";

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

	videodecoder_api->godot_videodecoder_register_decoder(&plugin_interface);
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

	data->audio_to_send = 0;

	data->packet_queue = NULL;
	data->packet_queue_alloc = false;

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

char *godot_videodecoder_get_plugin_name(void) {

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
	// HACK: Avoids segfault, needs to be above probe_data. Minimum size of 5 bytes.
	// Probably alignment.
	uint64_t hack;
	AVProbeData probe_data; // NOTE: Size of probe_data is 32 bytes.

	probe_data.buf = data->io_buffer;
	probe_data.buf_size = read_bytes;
	probe_data.filename = "";

	AVInputFormat *input_format = NULL;
	input_format = av_probe_input_format(&probe_data, 1);
	input_format->flags |= AVFMT_SEEK_TO_PTS;

	if (input_format == NULL) {

		_cleanup(data);
		api->godot_print_warning("Format not recognized.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	godot_string str;
	api->godot_string_new(&str);
	api->godot_string_parse_utf8(&str, input_format->long_name);
	api->godot_print(&str);
	api->godot_string_destroy(&str);

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
	av_opt_set_int(data->swr_ctx, "out_sample_rate", data->acodec_ctx->sample_rate, 0);
	av_opt_set_sample_fmt(data->swr_ctx, "in_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);
	av_opt_set_sample_fmt(data->swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
	swr_init(data->swr_ctx);

	data->time = 0;
	data->audio_to_send = 0;

	data->packet_queue = packet_queue_init();
	data->packet_queue_alloc = true;

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

	return _avtime_to_sec(data->format_ctx->duration);
}

godot_pool_byte_array *godot_videodecoder_update(void *p_data, godot_real p_delta) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;

	data->time += p_delta;
	bool has_videoframe = false;

	while (!has_videoframe) {
		if (av_read_frame(data->format_ctx, &data->packet) >= 0) {
			if (data->packet.stream_index == data->videostream_idx) {
				// Decode Video
				int x = AVERROR(EAGAIN);
				while (x == AVERROR(EAGAIN)) {
					if (avcodec_send_packet(data->vcodec_ctx, &data->packet) >= 0) {
						x = avcodec_receive_frame(data->vcodec_ctx, data->frame_yuv);
						if (x != 0 && x != AVERROR(EAGAIN)) {
							return NULL;
						} else if (x == 0) {
							sws_scale(data->sws_ctx, (uint8_t const *const *)data->frame_yuv->data, data->frame_yuv->linesize, 0,
									data->vcodec_ctx->height, data->frame_rgb->data, data->frame_rgb->linesize);
						}
					}
				}

				_unwrap_video_frame(&data->unwrapped_frame, data->frame_rgb, data->vcodec_ctx->width, data->vcodec_ctx->height);
				av_packet_unref(&data->packet);
				// Stop decoding once a video frame has been found. Present it. TODO: Buffer this.
				has_videoframe = true;
			} else if (data->mix_callback && data->packet.stream_index == data->audiostream_idx) {
				packet_queue_put(data->packet_queue, &data->packet);

				// Save to buffer
				// NOTE: You recieve more than one packet of audio per update frame.
				// Check terminal output

			} else {
				av_packet_unref(&data->packet);
			}
		}
	}

	// Audio sending here.

	bool buffer_full = false;

	while (!buffer_full && data->packet_queue->nb_packets > 0) {

		// No decoded audio in buffer, decode
		if (data->audio_to_send <= 0) {
			AVPacket pkt;
			if (data->packet_queue->nb_packets < 0) {
				break;
			}
			if (!packet_queue_get(data->packet_queue, &pkt)) {
				break;
			}
			int x = AVERROR(EAGAIN);
			while (x == AVERROR(EAGAIN)) {
				if (avcodec_send_packet(data->acodec_ctx, &pkt) >= 0) {
					x = avcodec_receive_frame(data->acodec_ctx, data->audio_frame);
					if (x == 0) {
						// got data
						data->audio_to_send = swr_convert(data->swr_ctx, (uint8_t **)&data->audio_buffer, data->audio_frame->nb_samples, (const uint8_t **)data->audio_frame->extended_data, data->audio_frame->nb_samples);
					}
				}
			}
		}

		int mixed = data->mix_callback(data->mix_udata, data->audio_buffer, data->audio_to_send);
		data->audio_to_send -= mixed;
		// DEBUG
		printf("Pending audio: %i\tAvailable packets: %i\n", data->audio_to_send, data->packet_queue->nb_packets);
		buffer_full = data->audio_to_send > 0;
		// send audio until full
	}

	// DEBUG Yes. This function works. No more polluting the log.
	// printf("update()\n");
	return &data->unwrapped_frame;
}

godot_real godot_videodecoder_get_playback_position(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// DEBUG
	printf("get_playback_position()\n");

	if (data->frame_yuv == NULL) {
		return 0;
	}
	return _avtime_to_sec(data->frame_yuv->pts);
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

void godot_videodecoder_set_mix_callback(void *p_data, GDNativeAudioMixCallback p_callback, void *p_userdata) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;

	data->mix_callback = p_callback;
	data->mix_udata = p_userdata;
	// DEBUG
	printf("set_mix_callback()\n");
	// DEBUG
} // TODO: p_callback Needs to be AudioMixCallback

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

godot_vector2 godot_videodecoder_get_size(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	godot_vector2 vec;
	api->godot_vector2_new(&vec, data->vcodec_ctx->width, data->vcodec_ctx->height);
	return vec;
}

const godot_videodecoder_interface_gdnative plugin_interface = {
	godot_videodecoder_constructor,
	godot_videodecoder_destructor,
	godot_videodecoder_get_plugin_name,
	godot_videodecoder_open_file,
	godot_videodecoder_get_length,
	godot_videodecoder_get_playback_position,
	godot_videodecoder_seek,
	godot_videodecoder_set_audio_track,
	godot_videodecoder_update,
	godot_videodecoder_set_mix_callback,
	godot_videodecoder_get_channels,
	godot_videodecoder_get_mix_rate,
	godot_videodecoder_get_size
};