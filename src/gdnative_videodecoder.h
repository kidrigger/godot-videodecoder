/*
 * This won't compile yet.
 */

#ifndef FFMPEG_GDNATIVE_VIDEODECODER_H
#define FFMPEG_GDNATIVE_VIDEODECODER_H

#include "thirdparty/include/libavcodec/avcodec.h"
#include "thirdparty/include/libavformat/avformat.h"
#include "thirdparty/include/libavutil/avutil.h"
#include "thirdparty/include/libswscale/swscale.h"
#include <godot_headers/gdnative_api_struct.gen.h>
#include <stdint.h>

typedef struct {
	godot_object *instance;

	// TODO: WORK OUT THIS PART
	// Ref<ImageTexture> texture;

	void *file;

	AVIOContext *pIOCtx;
	AVFormatContext *pFormatCtx;
	AVCodecContext *pCodecCtx;
	AVFrame *pFrameYUV;
	AVFrame *pFrameRGB;
	SwsContext *pSwsCtx;
	AVPacket packet;
	godot_pool_byte_array *io_buffer;
	godot_pool_array_write_access *io_write;
	godot_pool_byte_array *frame_buffer;
	godot_pool_array_write_access *frame_write;
	godot_int videostream_idx;
} godot_videodecoder_data;

const godot_gdnative_core_api_struct *api = NULL;
const godot_gdnative_ext_nativescript_api_struct *nativescript_api = NULL;

#ifdef __cplusplus
extern "C" {
#endif

void GDN_EXPORT godot_ffmpeg_gdnative_init(godot_gdnative_init_options *p_options);
void GDN_EXPORT godot_ffmpeg_gdnative_terminate(godot_gdnative_terminate_options *p_options);
void GDN_EXPORT godot_ffmpeg_nativescript_init(void *p_handle);

void *gdnative_videodecoder_constructor(godot_object *p_instance) {
	godot_videodecoder_data *data = api->godot_alloc(sizeof(godot_videodecoder_data));

	data->instance = p_instance;
	data->pIOCtx = NULL;
	data->pFormatCtx = NULL;
	data->pCodecCtx = NULL;
	data->pFrameYUV = NULL;
	data->pFrameRGB = NULL;
	data->pSwsCtx = NULL;
	api->godot_pool_byte_array_new(data->io_buffer);
	api->godot_pool_byte_array_new(data->frame_buffer);
}

void gdnative_videodecoder_destructor(void *p_data) {
	godot_videodecoder_data *data = (godot_videodecoder_data *)p_data;
	if (data->sws_ctx != NULL) {
		sws_freeContext(data->sws_ctx);
		data->sws_ctx = NULL;
	}
	if (data->pFrameRGB != NULL) {
		av_free(data->pFrameRGB);
		data->pFrameRGB = NULL;
	}
	if (data->pFrameYUV != NULL) {
		av_free(data->pFrameYUV);
		data->pFrameYUV = NULL;
	}
	if (data->pCodecCtx != NULL) {
		avcodec_close(data->pCodecCtx);
		data->pCodecCtx = NULL;
	}
	if (data->data->pFormatCtx != NULL) {
		avformat_close_input(data->data->pFormatCtx);
		data->data->pFormatCtx = NULL;
	}
	if (data->pIOCtx != NULL) {
		av_free(data->pIOCtx);
		data->pIOCtx = NULL;
	}
	if (data->io_buffer != NULL) {
		api->godot_pool_byte_array_write_access_destroy(data->io_write);
		api->godot_pool_byte_array_destroy(data->io_buffer);
	}
	if (data->frame_buffer != NULL) {
		api->godot_pool_byte_array_write_access_destroy(data->frame_write);
		api->godot_pool_byte_array_destroy(data->frame_buffer);
	}
}

godot_bool open_file(void *p_data, void *p_fileAccess) {

	// Get the data_struct
	godot_videodecoder_data *data = (godot_videodecoder_data *)p_data;

	if (p_fileAccess == NULL) {
		return false;
	}

	// TODO: Buffer length? 3KB
	const int buffer_size = 3 * 1024;

	api->godot_pool_byte_array_resize(data->io_buffer, buffer_size);
	data->io_write = api->godot_pool_byte_array_write(data->io_buffer);
	uint8_t *io_buffer_ptr = api->godot_pool_byte_array_write_access_ptr(data->io_buffer);

	// TODO: Need the API for the read and seek packet
	data->pIOCtx = avio_alloc_context(io_buffer_ptr, buffer_size, 0, data->file,
			ffmpeg_read_packet, NULL, ffmpeg_seek_packet);

	// FFMPEG format recognition
	data->pFormatCtx = avformat_alloc_context();
	data->pFormatCtx->pb = data->pIOCtx;

	// Recognize the input format (or FFMPEG will kill the io_buffer by loading 5 MB : Should I try 5MB io_buffer?)

	// TODO: No protocol for reading into the buffer.
	// READ THIS INTO THE BUFFER; THIS METHOD IS WRONG
	int read_bytes = file->get_buffer(data->io_buffer, buffer_size);
	if (read_bytes < buffer_size) {
		// Shouldn't happen.
		destructor();
		return false;
	}

	AVProbeData probe_data;
	probe_data.buf = io_buffer_ptr;
	probe_data.buf_size = read_bytes;
	probe_data.filename = "";
	data->pFormatCtx->iformat = av_probe_input_format(&probe_data, 1);

	data->pFormatCtx->flags = AVFMT_FLAG_CUSTOM_IO;

	if (avformat_open_input(&data->pFormatCtx, "", 0, 0) != 0) {
		destructor();
		return false;
	}

	if (avformat_find_stream_info(data->pFormatCtx, NULL) < 0) {
		destructor();
		return false;
	}

	// Find the video stream
	for (int i = 0; i != data->pFormatCtx->nb_streams; i++) {
		if (data->pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			data->videostream_idx = i;
		}
	}
	if (data->videostream_idx == -1) {
		destructor();
		return false;
	}

	// FIX: Possible Memory Leak
	AVCodecParameters *pCodecParam = data->pFormatCtx->streams[data->videostream_idx]->codecpar;

	// FIX: Possible Memory Leak
	AVCodec *pCodec = avcodec_find_decoder(pCodecParam->codec_id);
	if (pCodec == NULL) {
		destructor();
		return false;
	}

	data->pCodecCtx = avcodec_alloc_context3(pCodec);
	if (data->pCodecCtx == NULL) {
		destructor();
		return false;
	}

	avcodec_parameters_to_context(data->pCodecCtx, pCodecParam);

	if (avcodec_open2(data->pCodecCtx, pCodec, NULL) < 0) {
		destructor();
		return false;
	}

	data->pFrameYUV = av_frame_alloc();
	data->pFrameRGB = av_frame_alloc();

	if (data->pFrameRGB == NULL || data->pFrameYUV == NULL) {
		destructor();
		return false;
	}

	// TODO: Workout texture
	texture->create(data->pCodecCtx->width, data->pCodecCtx->height, Image::FORMAT_RGBA8,
			Texture::FLAG_FILTER | Texture::FLAG_VIDEO_SURFACE);

	int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, data->pCodecCtx->width, data->pCodecCtx->height, 1);
	api->godot_pool_byte_array_resize(data->frame_buffer);
	data->frame_write = api->godot_pool_byte_array_write(data->frame_buffer);
	uint8_t *frame_buffer_ptr = api->godot_pool_byte_array_write_access_ptr(data->frame_write);

	// We desperately need the write_access alive!!

	av_image_fill_arrays(data->pFrameRGB->data, data->pFrameRGB->linesize, frame_buffer_ptr,
			AV_PIX_FMT_RGB24, data->pCodecCtx->width, data->pCodecCtx->height, 1);

	// For scaling TODO: Get custom sizes.
	data->pSwsCtx = sws_getContext(data->pCodecCtx->width, data->pCodecCtx->height,
			data->pCodecCtx->pix_fmt, data->pCodecCtx->width, data->pCodecCtx->height,
			AV_PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL);

	return true;
}

godot_real (*get_length)(const void *);
void (*seek)(void *, godot_real);
void (*set_audio_track)(void *, godot_int);
Ref<Texture> get_texture(void *); // TODO Work out about the texture reference.
void (*update)(void *, godot_real);
void set_mix_callback(AudioMixCallback p_callback, void *p_userdata); // TODO
godot_int (*get_channels)(const void *);
godot_int (*get_mix_rate)(const void *);

#ifdef __cplusplus
}
#endif

#endif /* FFMPEG_GDNATIVE_VIDEODECODER_H */