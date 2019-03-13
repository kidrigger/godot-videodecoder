
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

list_node_t *list_create_node(const char *str) {
	list_node_t *node = (list_node_t *)api->godot_alloc(sizeof(list_node_t));
	node->value = (char *)api->godot_alloc(strlen(str) + 1);
	strcpy(node->value, str);
	node->next = NULL;
	return node;
}

void list_append(list_t *list, const char *str) {
	if (list->end == NULL) {
		list->start = list_create_node(str);
		list->end = list->start;
		return;
	}
	list->end->next = list_create_node(str);
	list->end = list->end->next;
}

void list_join(list_t *first, list_t *second) {
	if (second->start == NULL) return;
	first->end->next = second->start;
	first->end = second->end;
	second->start = NULL;
	second->end = NULL;
}

void list_free_r(list_node_t *head) {
	if (head == NULL) {
		return;
	}
	list_free_r(head->next);
	if (head->value != NULL) {
		api->godot_free(head->value);
	}
	api->godot_free(head);
}

void list_free(list_t *head) {
	list_free_r(head->start);
	head->end = NULL;
	head->start = NULL;
}

int list_size(list_t *head) {
	list_node_t *l = head->start;
	int i = 0;
	while (l != NULL) {
		i++;
		l = l->next;
	}
	return i;
}

#endif /* _LINKED_LIST_H */
