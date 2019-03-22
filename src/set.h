
#ifndef _STRING_SET_H
#define _STRING_SET_H

#include <gdnative_api_struct.gen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "linked_list.h"

extern const godot_gdnative_core_api_struct *api;

typedef struct set_bst_node_t {
	char *value;
	int priority;
	struct set_bst_node_t *left;
	struct set_bst_node_t *right;
} set_t;

set_t *set_create_node(const char *root_val);

set_t *cw_rot(set_t *root);

set_t *ccw_rot(set_t *root);

set_t *set_insert(set_t *root, const char *val);

void set_free(set_t *root);

void set_print(set_t *root, int depth);

list_t set_create_list(set_t *root);

#endif /* _STRING_SET_H */
