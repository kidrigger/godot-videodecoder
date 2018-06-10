
#include <gdnative_api_struct.gen.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <stdint.h>

typedef struct videodecoder_data_struct {

	godot_object *instance; // Don't clean
	uint8_t *io_buffer; // CLEANUP
	AVIOContext *io_ctx; // CLEANUP
	AVFormatContext *format_ctx; // CLEANUP
	godot_bool input_open;
	int videostream_idx;
	AVCodecContext *codec_ctx; // CLEANUP
	godot_bool codec_open;
	AVFrame *frame_yuv; // CLEANUP
	AVFrame *frame_rgb; // CLEANUP
	struct SwsContext *sws_ctx; // CLEANUP
	uint8_t *frame_buffer;
	int frame_buffer_size;
	godot_pool_byte_array unwrapped_frame;
	AVPacket packet;

} videodecoder_data_struct;

const godot_int IO_BUFFER_SIZE = 64 * 1024; // File reading buffer of 64 KiB?

const godot_gdnative_core_api_struct *api = NULL;
const godot_gdnative_ext_nativescript_api_struct *nativescript_api = NULL;
const godot_gdnative_ext_videodecoder_api_struct *videodecoder_api = NULL;

// Cleanup should empty the struct to the point where you can open a new file from.
static void _cleanup(videodecoder_data_struct *data) {

	if (data->sws_ctx != NULL) {

		sws_freeContext(data->sws_ctx);
		data->sws_ctx = NULL;
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

	if (data->codec_ctx != NULL) {

		if (data->codec_open) {

			avcodec_close(data->codec_ctx);
			data->codec_open = GODOT_FALSE;
		}
		avcodec_free_context(&data->codec_ctx);
		data->codec_ctx = NULL;
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

	data->videostream_idx = 0;
}

static void _unwrap(godot_pool_byte_array *dest, AVFrame *frame, int width, int height) {

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
	data->codec_ctx = NULL;
	data->codec_open = GODOT_FALSE;

	data->frame_rgb = NULL;
	data->frame_yuv = NULL;
	data->sws_ctx = NULL;

	data->frame_buffer = NULL;
	data->frame_buffer_size = 0;

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
	// find stream
	for (int i = 0; i < data->format_ctx->nb_streams; i++) {

		if (data->format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {

			data->videostream_idx = i;
			break;
		}
	}

	if (data->videostream_idx == -1) {

		_cleanup(data);
		api->godot_print_warning("Video Stream not found.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	AVCodecParameters *codec_param = data->format_ctx->streams[data->videostream_idx]->codecpar;

	AVCodec *codec = NULL;
	codec = avcodec_find_decoder(codec_param->codec_id);

	if (codec == NULL) {

		_cleanup(data);
		api->godot_print_warning("Decoder not found.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	data->codec_ctx = avcodec_alloc_context3(codec);

	if (data->codec_ctx == NULL) {

		_cleanup(data);
		api->godot_print_warning("Codec allocation error.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	if (avcodec_parameters_to_context(data->codec_ctx, codec_param) < 0) {

		_cleanup(data);
		api->godot_print_warning("Codec context init error.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	if (avcodec_open2(data->codec_ctx, codec, NULL) < 0) {

		_cleanup(data);
		api->godot_print_warning("Codec failed to open.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}
	data->codec_open = GODOT_TRUE;

	// NOTE: Align of 1 (I think it is for 32 bit alignment.) Doesn't work otherwise
	data->frame_buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGB32,
			data->codec_ctx->width, data->codec_ctx->height, 1);

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

	int width = data->codec_ctx->width;
	int height = data->codec_ctx->height;
	if (av_image_fill_arrays(data->frame_rgb->data, data->frame_rgb->linesize, data->frame_buffer,
				AV_PIX_FMT_RGB32, width, height, 1) < 0) {

		_cleanup(data);
		api->godot_print_warning("Frame fill.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	data->sws_ctx = sws_getContext(width, height, data->codec_ctx->pix_fmt,
			width, height, AV_PIX_FMT_RGB32, SWS_BILINEAR,
			NULL, NULL, NULL);

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

	return (data->format_ctx->duration / (godot_real)AV_TIME_BASE);
}

godot_object *godot_videodecoder_update(void *p_data, godot_real p_delta) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;

	do {
		if (av_read_frame(data->format_ctx, &data->packet) < 0) {
			return NULL;
		}
	} while (data->packet.stream_index != data->videostream_idx);
	printf("Read frame.\n");

	int x;
	do {
		if (avcodec_send_packet(data->codec_ctx, &data->packet) >= 0) {
			x = avcodec_receive_frame(data->codec_ctx, data->frame_yuv);
			if (x != 0 && x != AVERROR(EAGAIN)) {
				return NULL;
			} else if (x == 0) {
				sws_scale(data->sws_ctx, (uint8_t const *const *)data->frame_yuv->data, data->frame_yuv->linesize, 0,
						data->codec_ctx->height, data->frame_rgb->data, data->frame_rgb->linesize);
			}
		}
		printf("EAGAIN\n");
	} while (x == AVERROR(EAGAIN));

	printf("Go for image\n");

	godot_object *img = NULL;
	_unwrap(&data->unwrapped_frame, data->frame_rgb, data->codec_ctx->width, data->codec_ctx->height);
	img = videodecoder_api->godot_videodecoder_create_image(&data->unwrapped_frame, data->codec_ctx->width, data->codec_ctx->height);
	av_packet_unref(&data->packet);
	// DEBUG
	printf("update()\n");

	return img;
}

/* ---------------------- TODO ------------------------- */

godot_real godot_videodecoder_get_playback_position(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// DEBUG
	printf("get_playback_position()\n");
	// DEBUG
	return 0;
}

void godot_videodecoder_seek(void *p_data, godot_real p_time) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// DEBUG
	printf("seek()\n");
	// DEBUG
}

void godot_videodecoder_set_audio_track(void *p_data, godot_int p_audiotrack) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// DEBUG
	printf("set_audio_track()\n");
	// DEBUG
}

void godot_videodecoder_set_mix_callback(void *p_data, void *p_callback, void *p_userdata) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// DEBUG
	printf("set_mix_callback()\n");
	// DEBUG
} // TODO: p_callback Needs to be AudioMixCallback

godot_int godot_videodecoder_get_channels(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// DEBUG
	printf("get_channels()\n");
	// DEBUG
	return 0;
}

godot_int godot_videodecoder_get_mix_rate(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// DEBUG
	printf("get_mix_rate()\n");
	// DEBUG
	return 0;
}

godot_vector2 godot_videodecoder_get_size(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	godot_vector2 vec;
	api->godot_vector2_new(&vec, data->codec_ctx->width, data->codec_ctx->height);
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