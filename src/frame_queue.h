
#ifndef _FRAME_QUEUE_H
#define _FRAME_QUEUE_H

#include <gdnative_api_struct.gen.h>
#include <libavformat/avformat.h>

extern const godot_gdnative_core_api_struct *api;

typedef struct FrameList {
	AVFrame *frm;
	struct FrameList *next;
} FrameList;

typedef struct FrameQueue {
	FrameList *first_frm, *last_frm;
	int nb_frames;
} FrameQueue;

FrameQueue *frame_queue_init() {
	FrameQueue *q = (FrameQueue *)api->godot_alloc(sizeof(FrameQueue));
	memset(q, 0, sizeof(FrameQueue));
	if (q != NULL) {
		memset(q, 0, sizeof(FrameQueue));
	}
	return q;
}

bool frame_queue_put(FrameQueue *q, AVFrame *frm) {

	FrameList *frm1;
	frm1 = (FrameList *)api->godot_alloc(sizeof(FrameList));
	if (!frm1) {
		return false;
	}
	frm1->frm = av_frame_alloc();
	av_frame_ref(frm1->frm, frm);
	frm1->next = NULL;

	if (!q->last_frm)
		q->first_frm = frm1;
	else
		q->last_frm->next = frm1;
	q->last_frm = frm1;
	q->nb_frames++;
	return true;
}

bool frame_queue_get(FrameQueue *q, AVFrame *frm) {

	FrameList *frm1;
	frm1 = q->first_frm;
	if (frm1) {
		q->first_frm = frm1->next;
		if (!q->first_frm)
			q->last_frm = NULL;
		q->nb_frames--;
		if (av_frame_ref(frm, frm1->frm) == 0) {
			av_frame_unref(frm1->frm);
			av_frame_free(&frm1->frm);
		} else {
			api->godot_print_warning("av_frame_ref() failed.", "frame_queue_get()", __FILE__, __LINE__);
		}
		api->godot_free(frm1);
		return true;
	} else {
		return false;
	}
}

void frame_queue_deinit(FrameQueue *q) {
	AVFrame *frame;
	frame = av_frame_alloc();
	while (frame_queue_get(q, frame)) {
		av_frame_unref(frame);
	}
	av_frame_free(&frame);
	api->godot_free(q);
}

#endif /* _FRAME_QUEUE_H */