
#include <gdnative_api_struct.gen.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <stdint.h>

typedef struct {
	godot_object *instance; // Don't clean
	uint8_t *io_buffer; // CLEANUP
	AVIOContext *io_ctx; // CLEANUP
} videodecoder_data_struct;

const godot_int IO_BUFFER_SIZE = 64 * 1024; // File reading buffer of 64 KiB?

const godot_gdnative_core_api_struct *api = NULL;
const godot_gdnative_ext_nativescript_api_struct *nativescript_api = NULL;
const godot_gdnative_ext_videodecoder_api_struct *videodecoder_api = NULL;

// Cleanup should empty the struct to the point where you can open a new file from.
static void _cleanup(videodecoder_data_struct *data) {
	if (data->io_buffer != NULL) {
		api->godot_free(data->io_buffer);
		data->io_buffer = NULL;
	}
	// if (data->io_ctx != NULL) {
	// 	avio_context_free(&data->io_ctx);
	// }
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
	videodecoder_data_struct *videodecoder_data = api->godot_alloc(sizeof(videodecoder_data_struct));

	videodecoder_data->instance = p_instance;

	videodecoder_data->io_buffer = NULL;
	videodecoder_data->io_ctx = NULL;

	// TODO: DEBUG
	char msg[] = "ctor()";
	godot_string str;
	api->godot_string_new(&str);
	api->godot_string_parse_utf8(&str, msg);
	api->godot_print(&str);
	api->godot_string_destroy(&str);
	// TODO: DEBUG

	return videodecoder_data;
}
void godot_videodecoder_destructor(void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	_cleanup(data);
	data->instance = NULL;
	api->godot_free(data);
	data = NULL; // Not needed, but just to be safe.

	// TODO: DEBUG
	char msg[] = "dtor()";
	godot_string str;
	api->godot_string_new(&str);
	api->godot_string_parse_utf8(&str, msg);
	api->godot_print(&str);
	api->godot_string_destroy(&str);
	// TODO: DEBUG
}

char *godot_videodecoder_get_plugin_name(void) {
	return plugin_name;
}

godot_bool godot_videodecoder_open_file(void *p_data, void *file) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;

	_cleanup(data);

	data->io_buffer = (uint8_t *)api->godot_alloc(IO_BUFFER_SIZE * sizeof(uint8_t));
	if (data->io_buffer == NULL) {
		_cleanup(data);
		api->godot_print_warning("Buffer couldn't be allocated", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	godot_int read_bytes = videodecoder_api->godot_videodecoder_file_read(file, data->io_buffer, IO_BUFFER_SIZE);

	if (read_bytes < IO_BUFFER_SIZE) {
		// something went wrong, we should be able to read atleast one buffer length.
		_cleanup(data);
		api->godot_print_warning("Couldn't read file beyond 64 KiB.", "godot_videodecoder_open_file()", __FILE__, __LINE__);
		return GODOT_FALSE;
	}

	// Rewind to 0
	videodecoder_api->godot_videodecoder_file_seek(file, 0, SEEK_SET);

	api->godot_print_warning("", "", "gdn", __LINE__);
	char hack[5] = "HACK"; // HACK: somehow solves a segfault in avutil

	// HACK[5] can be placed above this
	// Determine input format
	AVProbeData probe_data;

	probe_data.buf = data->io_buffer;
	probe_data.buf_size = IO_BUFFER_SIZE;
	probe_data.filename = "";

	// HACK[5] can't be placed below this.

	AVInputFormat *input_format = av_probe_input_format(&probe_data, 1);

	// data->io_ctx = avio_alloc_context(data->io_buffer, IO_BUFFER_SIZE, 0, file,
	// 		videodecoder_api->godot_videodecoder_file_read, NULL,
	// 		videodecoder_api->godot_videodecoder_file_seek);

	// if (data->io_ctx == NULL) {
	// 	_cleanup(data);
	// 	api->godot_print_warning("Could not allocate IO context.", "godot_videodecoder_open_file()", "gdnative_videodecoder.c", __LINE__);
	// 	return GODOT_FALSE;
	// }

	return GODOT_TRUE;
}

/* ---------------------- TODO ------------------------- */
godot_real godot_videodecoder_get_length(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// TODO: DEBUG
	char msg[] = "get_length()";
	godot_string str;
	api->godot_string_new(&str);
	api->godot_string_parse_utf8(&str, msg);
	api->godot_print(&str);
	api->godot_string_destroy(&str);
	// TODO: DEBUG
	return 0;
}
godot_real godot_videodecoder_get_playback_position(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// TODO: DEBUG
	char msg[] = "get_playback_position()";
	godot_string str;
	api->godot_string_new(&str);
	api->godot_string_parse_utf8(&str, msg);
	api->godot_print(&str);
	api->godot_string_destroy(&str);
	// TODO: DEBUG
	return 0;
}
void godot_videodecoder_seek(void *p_data, godot_real p_time) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// TODO: DEBUG
	char msg[] = "seek()";
	godot_string str;
	api->godot_string_new(&str);
	api->godot_string_parse_utf8(&str, msg);
	api->godot_print(&str);
	api->godot_string_destroy(&str);
	// TODO: DEBUG
}
void godot_videodecoder_set_audio_track(void *p_data, godot_int p_audiotrack) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// TODO: DEBUG
	char msg[] = "set_audio_track()";
	godot_string str;
	api->godot_string_new(&str);
	api->godot_string_parse_utf8(&str, msg);
	api->godot_print(&str);
	api->godot_string_destroy(&str);
	// TODO: DEBUG
}
void *godot_videodecoder_get_texture(void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// TODO: DEBUG
	char msg[] = "get_texture()";
	godot_string str;
	api->godot_string_new(&str);
	api->godot_string_parse_utf8(&str, msg);
	api->godot_print(&str);
	api->godot_string_destroy(&str);
	// TODO: DEBUG
	return NULL;
} // TODO: return Needs to be Ref<Texture>
void godot_videodecoder_update(void *p_data, godot_real p_delta) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// TODO: DEBUG
	/*
	char msg[] = "update()";
	godot_string str;
	api->godot_string_new(&str);
	api->godot_string_parse_utf8(&str, msg);
	api->godot_print(&str);
	api->godot_string_destroy(&str);
	*/
	// TODO: DEBUG
}
void godot_videodecoder_set_mix_callback(void *p_data, void *p_callback, void *p_userdata) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// TODO: DEBUG
	char msg[] = "set_mix_callback()";
	godot_string str;
	api->godot_string_new(&str);
	api->godot_string_parse_utf8(&str, msg);
	api->godot_print(&str);
	api->godot_string_destroy(&str);
	// TODO: DEBUG
} // TODO: p_callback Needs to be AudioMixCallback
godot_int godot_videodecoder_get_channels(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// TODO: DEBUG
	char msg[] = "get_channels()";
	godot_string str;
	api->godot_string_new(&str);
	api->godot_string_parse_utf8(&str, msg);
	api->godot_print(&str);
	api->godot_string_destroy(&str);
	// TODO: DEBUG
	return 0;
}
godot_int godot_videodecoder_get_mix_rate(const void *p_data) {
	videodecoder_data_struct *data = (videodecoder_data_struct *)p_data;
	// TODO: DEBUG
	char msg[] = "get_mix_rate()";
	godot_string str;
	api->godot_string_new(&str);
	api->godot_string_parse_utf8(&str, msg);
	api->godot_print(&str);
	api->godot_string_destroy(&str);
	// TODO: DEBUG
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