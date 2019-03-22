
#include "data_struct.h"

extern int num_supported_ext;
extern const char **supported_ext;

// Cleanup should empty the struct to the point where you can open a new file from.
void _cleanup(videodecoder_data_struct *data) {

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

	if (num_supported_ext > 0) {
		for (int i = 0; i < num_supported_ext; i++) {
			if (supported_ext[i] != NULL) {
				api->godot_free((void *)supported_ext[i]);
			}
		}
		api->godot_free(supported_ext);
		num_supported_ext = 0;
	}

	// DEBUG
	printf("dtor()\n");
}
