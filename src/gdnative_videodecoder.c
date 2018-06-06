
#include <gdnative_api_struct.gen.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <stdint.h>

typedef struct {
	godot_object *instance;
	godot_string name;
} videodecoder_data_struct;

const godot_gdnative_core_api_struct *api = NULL;
const godot_gdnative_ext_nativescript_api_struct *nativescript_api = NULL;
const godot_gdnative_ext_videodecoder_api_struct *videodecoder_api = NULL;

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
	videodecoder_data_struct *videodecoder_data = api->godot_alloc(sizeof(videodecoder_data_struct));
	videodecoder_data->instance = p_instance;
	api->godot_string_new(&videodecoder_data->name);
	char name[] = "FFMPEG";
	api->godot_string_parse_utf8(&videodecoder_data->name, name);
	return videodecoder_data;
}
void godot_videodecoder_destructor(void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	data->instance = NULL;
}

char *godot_videodecoder_get_plugin_name(void) {
	return plugin_name;
}

godot_bool godot_videodecoder_open_file(void *p_data, void *file) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	return true;
}
godot_real godot_videodecoder_get_length(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	return 0;
}
godot_real godot_videodecoder_get_playback_position(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	return 0;
}
void godot_videodecoder_seek(void *p_data, godot_real p_time) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
}
void godot_videodecoder_set_audio_track(void *p_data, godot_int p_audiotrack) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
}
void *godot_videodecoder_get_texture(void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	return NULL;
} // TODO: return Needs to be Ref<Texture>
void godot_videodecoder_update(void *p_data, godot_real p_delta) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
}
void godot_videodecoder_set_mix_callback(void *p_data, void *p_callback, void *p_userdata) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
} // TODO: p_callback Needs to be AudioMixCallback
godot_int godot_videodecoder_get_channels(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	return 0;
}
godot_int godot_videodecoder_get_mix_rate(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	return 0;
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
	godot_videodecoder_get_texture,
	godot_videodecoder_update,
	godot_videodecoder_set_mix_callback,
	godot_videodecoder_get_channels,
	godot_videodecoder_get_mix_rate
};