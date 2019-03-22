
#include <gdnative_api_struct.gen.h>
#include <stdint.h>
#include <string.h>

#include "api_functions.h"
#include "data_struct.h"
#include "extensions.h"
#include "gdnative_functions.h"
#include "packet_queue.h"
#include "set.h"

const godot_gdnative_core_api_struct *api = NULL;
const godot_gdnative_ext_nativescript_api_struct *nativescript_api = NULL;
const godot_gdnative_ext_videodecoder_api_struct *videodecoder_api = NULL;

extern const godot_videodecoder_interface_gdnative plugin_interface;

const char *plugin_name = "test_plugin";
int num_supported_ext = 0;
const char **supported_ext = NULL;

const char **godot_videodecoder_get_supported_ext(int *p_count) {
	_update_extensions();
	*p_count = num_supported_ext;
	return supported_ext;
}

const char *godot_videodecoder_get_plugin_name(void) {
	return plugin_name;
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
