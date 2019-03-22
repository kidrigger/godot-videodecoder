
#include "gdnative_functions.h"

extern const godot_gdnative_core_api_struct *api;
extern const godot_gdnative_ext_nativescript_api_struct *nativescript_api;
extern const godot_gdnative_ext_videodecoder_api_struct *videodecoder_api;
extern const godot_videodecoder_interface_gdnative plugin_interface;

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