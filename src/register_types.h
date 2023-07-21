#ifndef REGISTER_TYPES_H
#define REGISTER_TYPES_H

#include <godot_cpp/godot.hpp>

void register_types(godot::ModuleInitializationLevel init_level);
void unregister_types(godot::ModuleInitializationLevel init_level);

#endif // REGISTER_TYPES_H