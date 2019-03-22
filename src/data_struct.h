
#ifndef _DATA_STRUCT_H
#define _DATA_STRUCT_H

#include "packet_queue.h"
#include <gdnative_api_struct.gen.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

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

// Cleanup should empty the struct to the point where you can open a new file from.
void _cleanup(videodecoder_data_struct *data);

void *godot_videodecoder_constructor(godot_object *p_instance);

void godot_videodecoder_destructor(void *p_data);

#endif /* _DATA_STRUCT_H */