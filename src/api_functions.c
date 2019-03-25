
#include "api_functions.h"
#include "data_struct.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

const godot_int IO_BUFFER_SIZE = 64 * 1024; // File reading buffer of 64 KiB?
const godot_int AUDIO_BUFFER_MAX_SIZE = 192000;

extern const godot_gdnative_ext_videodecoder_api_struct *videodecoder_api;

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

static bool _buffer_packets(videodecoder_data_struct *data) {
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
			return false;
		}
	}
	return true;
}

static int _decode_packet(AVFrame *dest, PacketQueue *pktq, AVCodecContext *ctx) {
	AVPacket pkt;
	int x = AVERROR(EAGAIN);
	while (x == AVERROR(EAGAIN)) {
		if (!packet_queue_get(pktq, &pkt)) {
			return 2;
		}
		if (avcodec_send_packet(ctx, &pkt) >= 0) {
			x = avcodec_receive_frame(ctx, dest);
		}
		av_packet_unref(&pkt);
	}
	return !!x;
}

static inline godot_real _avtime_to_sec(int64_t avtime) {
	return avtime / (godot_real)AV_TIME_BASE;
}

bool _setup_io_format(videodecoder_data_struct *data, void *file) {
	data->io_buffer = (uint8_t *)api->godot_alloc(IO_BUFFER_SIZE * sizeof(uint8_t));
	if (data->io_buffer == NULL) {
		_cleanup(data);
		api->godot_print_warning("Buffer alloc error", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return false;
	}

	godot_int read_bytes = videodecoder_api->godot_videodecoder_file_read(file, data->io_buffer, IO_BUFFER_SIZE);

	if (read_bytes < IO_BUFFER_SIZE) {
		// something went wrong, we should be able to read atleast one buffer length.
		_cleanup(data);
		api->godot_print_warning("File less that minimum buffer.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return false;
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
		return false;
	}
	input_format->flags |= AVFMT_SEEK_TO_PTS;

	printf("Format: %s\n", input_format->long_name);

	data->io_ctx = avio_alloc_context(data->io_buffer, IO_BUFFER_SIZE, 0, file,
			videodecoder_api->godot_videodecoder_file_read, NULL,
			videodecoder_api->godot_videodecoder_file_seek);
	if (data->io_ctx == NULL) {
		_cleanup(data);
		api->godot_print_warning("IO context alloc error.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return false;
	}

	data->format_ctx = avformat_alloc_context();
	if (data->format_ctx == NULL) {
		_cleanup(data);
		api->godot_print_warning("Format context alloc error.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return false;
	}

	data->format_ctx->pb = data->io_ctx;
	data->format_ctx->flags = AVFMT_FLAG_CUSTOM_IO;
	data->format_ctx->iformat = input_format;

	if (avformat_open_input(&data->format_ctx, "", NULL, NULL) != 0) {
		_cleanup(data);
		api->godot_print_warning("Input stream failed to open", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return false;
	}
	data->input_open = true;

	return true;
}

bool _setup_input_streams(videodecoder_data_struct *data) {
	if (avformat_find_stream_info(data->format_ctx, NULL) < 0) {
		_cleanup(data);
		api->godot_print_warning("Could not find stream info.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return false;
	}

	data->videostream_idx = -1; // should be -1 anyway, just being paranoid.
	data->audiostream_idx = -1;
	// find stream
	for (int i = 0; i < data->format_ctx->nb_streams; i++) {
		if (data->format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && data->videostream_idx == -1) {
			data->videostream_idx = i;
		} else if (data->format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && data->audiostream_idx == -1) {
			data->audiostream_idx = i;
		}
	}
	if (data->videostream_idx == -1) {
		_cleanup(data);
		api->godot_print_warning("Video Stream not found.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return false;
	}
	if (data->audiostream_idx == -1) {
		_cleanup(data);
		api->godot_print_warning("Audio Stream not found.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return false;
	}

	return true;
}

bool _setup_codecs(videodecoder_data_struct *data) {
	AVCodecParameters *vcodec_param = data->format_ctx->streams[data->videostream_idx]->codecpar;

	AVCodec *vcodec = NULL;
	vcodec = avcodec_find_decoder(vcodec_param->codec_id);
	if (vcodec == NULL) {
		_cleanup(data);
		api->godot_print_warning("Videodecoder not found.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return false;
	}

	data->vcodec_ctx = avcodec_alloc_context3(vcodec);
	if (data->vcodec_ctx == NULL) {
		_cleanup(data);
		api->godot_print_warning("Videocodec allocation error.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return false;
	}

	if (avcodec_parameters_to_context(data->vcodec_ctx, vcodec_param) < 0) {
		_cleanup(data);
		api->godot_print_warning("Videocodec context init error.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return false;
	}

	if (avcodec_open2(data->vcodec_ctx, vcodec, NULL) < 0) {
		_cleanup(data);
		api->godot_print_warning("Videocodec failed to open.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return false;
	}
	data->vcodec_open = GODOT_TRUE;

	AVCodecParameters *acodec_param = data->format_ctx->streams[data->audiostream_idx]->codecpar;
	AVCodec *acodec = NULL;
	acodec = avcodec_find_decoder(acodec_param->codec_id);
	if (acodec == NULL) {
		_cleanup(data);
		api->godot_print_warning("Audiodecoder not found.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return false;
	}

	data->acodec_ctx = avcodec_alloc_context3(acodec);
	if (data->acodec_ctx == NULL) {
		_cleanup(data);
		api->godot_print_warning("Audiocodec allocation error.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return false;
	}

	if (avcodec_parameters_to_context(data->acodec_ctx, acodec_param) < 0) {
		_cleanup(data);
		api->godot_print_warning("Audiocodec context init error.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return false;
	}

	if (avcodec_open2(data->acodec_ctx, acodec, NULL) < 0) {
		_cleanup(data);
		api->godot_print_warning("Audiocodec failed to open.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return false;
	}
	data->acodec_open = GODOT_TRUE;

	return true;
}

godot_bool godot_videodecoder_open_file(void *p_data, void *file) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;

	// DEBUG
	printf("open_file()\n");

	// Clean up the previous file.
	_cleanup(data);

	if (!_setup_io_format(data, file)) {
		return GODOT_FALSE;
	}

	if (!_setup_input_streams(data)) {
		return GODOT_FALSE;
	}

	if (!_setup_codecs(data)) {
		return GODOT_FALSE;
	}

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

	_buffer_packets(data);

	// printf("A: %i\t", data->audio_packet_queue->nb_packets);

	// DEBUG Yes. This function works. No more polluting the log.
	// printf("update()\n");
}

godot_pool_byte_array *godot_videodecoder_get_videoframe(void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;

	int x;
	while ((x = _decode_packet(data->frame_yuv, data->video_packet_queue, data->vcodec_ctx)) == 2) {
		if (!_buffer_packets(data)) {
			return NULL;
		}
	}
	if (x == 1) {
		return NULL;
	}

	sws_scale(data->sws_ctx, (uint8_t const *const *)data->frame_yuv->data, data->frame_yuv->linesize, 0,
			data->vcodec_ctx->height, data->frame_rgb->data, data->frame_rgb->linesize);

	_unwrap_video_frame(&data->unwrapped_frame, data->frame_rgb, data->vcodec_ctx->width, data->vcodec_ctx->height);

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
			if (!_decode_packet(data->audio_frame, data->audio_packet_queue, data->acodec_ctx)) {
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
		return 22050; // Sample rate of 22050 is standard on the decode.
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
