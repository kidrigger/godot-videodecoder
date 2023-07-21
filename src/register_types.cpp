#include "register_types.h"


#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "video_stream_ffmpeg.h"

#include <godot_cpp/classes/resource_loader.hpp>

using namespace godot;

Ref<VideoStreamFFMPEGLoader> loader;

void initialize_videodecoder(godot::ModuleInitializationLevel init_level) {
	if (init_level != godot::ModuleInitializationLevel::MODULE_INITIALIZATION_LEVEL_SCENE) return;

	GDREGISTER_CLASS(VideoStreamFFMPEGLoader);
	GDREGISTER_CLASS(VideoStreamFFMPEG);
	GDREGISTER_CLASS(VideoStreamPlaybackFFMPEG);

	loader.instantiate();
	ResourceLoader::get_singleton()->add_resource_format_loader(loader, true);
	std::cout << "VideoDecoder initialized" << std::endl;
}

void uninitialize_videodecoder(godot::ModuleInitializationLevel init_level) {
	if (init_level != godot::ModuleInitializationLevel::MODULE_INITIALIZATION_LEVEL_SCENE) return;

	ResourceLoader::get_singleton()->remove_resource_format_loader(loader);
	loader.unref();
	std::cout << "VideoDecoder uninitialized" << std::endl;
}

extern "C" {
	GDExtensionBool GDE_EXPORT init(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
		godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

		init_obj.register_initializer(initialize_videodecoder);
		init_obj.register_terminator(uninitialize_videodecoder);
		init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

		return init_obj.init();
	}
}
