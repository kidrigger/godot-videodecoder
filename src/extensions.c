
#include "extensions.h"

#include "set.h"
#include <libavformat/avformat.h>

extern int num_supported_ext;
extern const char **supported_ext;

void _update_extensions() {
	if (num_supported_ext > 0) return;

	const AVInputFormat *current_fmt = NULL;
	set_t *sup_ext_set = NULL;
	void *iterator_opaque = NULL;
	while ((current_fmt = av_demuxer_iterate(&iterator_opaque)) != NULL) {
		if (current_fmt->extensions != NULL) {
			char *exts = (char *)api->godot_alloc(strlen(current_fmt->extensions) + 1);
			strcpy(exts, current_fmt->extensions);
			char *token = strtok(exts, ",");
			while (token != NULL) {
				sup_ext_set = set_insert(sup_ext_set, token);
				token = strtok(NULL, ", ");
			}
			api->godot_free(exts);
		}
	}

	list_t ext_list = set_create_list(sup_ext_set);
	num_supported_ext = list_size(&ext_list);
	supported_ext = (const char **)api->godot_alloc(sizeof(char *) * num_supported_ext);
	list_node_t *cur_node = ext_list.start;
	int i = 0;
	while (cur_node != NULL) {
		supported_ext[i] = cur_node->value;
		cur_node->value = NULL;
		cur_node = cur_node->next;
		i++;
	}
	list_free(&ext_list);
	set_free(sup_ext_set);
}