
#ifndef _GDNATIVE_FUNCTIONS_H
#define _GDNATIVE_FUNCTIONS_H

#include <gdnative_api_struct.gen.h>

void GDN_EXPORT godot_gdnative_init(godot_gdnative_init_options *p_options);

void GDN_EXPORT godot_gdnative_terminate(godot_gdnative_terminate_options *p_options);

void GDN_EXPORT godot_gdnative_singleton();

#endif /* _GDNATIVE_FUNCTIONS_H */