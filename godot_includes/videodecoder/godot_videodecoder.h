/*************************************************************************/
/*  godot_videodecoder.h                                                         */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2018 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2018 Godot Engine contributors (cf. AUTHORS.md)    */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#ifndef GODOT_NATIVEVIDEODECODER_H
#define GODOT_NATIVEVIDEODECODER_H

#include <gdnative/gdnative.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	void *(*constructor)(godot_object *);
	void (*destructor)(void *);
	godot_bool (*open_file)(void *, godot_string);
	void (*stop)(void *);
	void (*play)(void *);
	godot_bool (*is_playing)(const void *);
	void (*set_paused)(void *, godot_bool);
	godot_bool (*is_paused)(const void *);
	void (*set_loop)(void *, godot_bool p_enable);
	godot_bool (*has_loop)(const void *);
	godot_real (*get_length)(const void *);
	godot_real (*get_playback_position)(const void *);
	void (*seek)(void *, godot_real);
	void (*set_audio_track)(void *, godot_int);
	void *get_texture(void *); // TODO: Needs to be Ref<Texture>
	void (*update)(void *, godot_real);
	void set_mix_callback(void *p_callback, void *p_userdata); // TODO: Needs to be AudioMixCallback
	godot_int (*get_channels)(const void *);
	godot_int (*get_mix_rate)(const void *);
} godot_videodecoder_interface_gdnative;

// FileAccess wrappers for custom FFmpeg IO
godot_int GDAPI godot_videodecoder_read_packet(void *file_ptr, uint8_t *buf, int buf_size);
int64_t GDAPI godot_videodecoder_seek_packet(void *file_ptr, int64_t pos, int whence);

#ifdef __cplusplus
}
#endif

#endif /* !GODOT_NATIVEVIDEODECODER_H */
