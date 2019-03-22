
#ifndef _LINKED_LIST_H
#define _LINKED_LIST_H

#include <gdnative_api_struct.gen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const godot_gdnative_core_api_struct *api;

typedef struct linked_list_node_t {
	char *value;
	struct linked_list_node_t *next;
} list_node_t;

typedef struct linked_list_t {
	list_node_t *start;
	list_node_t *end;
} list_t;

list_node_t *list_create_node(const char *str);

void list_append(list_t *list, const char *str);

void list_join(list_t *first, list_t *second);

void list_free_r(list_node_t *head);

void list_free(list_t *head);

int list_size(list_t *head);

#endif /* _LINKED_LIST_H */
