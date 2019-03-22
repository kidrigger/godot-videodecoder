
#ifndef _API_FUNCTIONS_H
#define _API_FUNCTIONS_H

#include <gdnative_api_struct.gen.h>

godot_bool godot_videodecoder_open_file(void *p_data, void *file);

godot_real godot_videodecoder_get_length(const void *p_data);

void godot_videodecoder_update(void *p_data, godot_real p_delta);

godot_pool_byte_array *godot_videodecoder_get_videoframe(void *p_data);

godot_int godot_videodecoder_get_audio(void *p_data, float *pcm, int num_samples);

godot_real godot_videodecoder_get_playback_position(const void *p_data);

void godot_videodecoder_seek(void *p_data, godot_real p_time);

/* ---------------------- TODO ------------------------- */

void godot_videodecoder_set_audio_track(void *p_data, godot_int p_audiotrack);

godot_int godot_videodecoder_get_channels(const void *p_data);

godot_int godot_videodecoder_get_mix_rate(const void *p_data);

godot_vector2 godot_videodecoder_get_texture_size(const void *p_data);

#endif /* _API_FUNCTIONS_H */