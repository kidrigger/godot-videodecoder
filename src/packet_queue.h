
#ifndef _PACKET_QUEUE_H
#define _PACKET_QUEUE_H

#include <gdnative_api_struct.gen.h>
#include <libavformat/avformat.h>

extern const godot_gdnative_core_api_struct *api;

typedef struct PacketQueue {
	AVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
} PacketQueue;

int quit = 0;

PacketQueue *packet_queue_init() {
	PacketQueue *q;
	q = (PacketQueue *)api->godot_alloc(sizeof(PacketQueue));
	if (q != NULL) {
		memset(q, 0, sizeof(PacketQueue));
	}
	return q;
}

bool packet_queue_put(PacketQueue *q, AVPacket *pkt) {

	AVPacketList *pkt1;
	if (av_packet_ref(pkt, pkt) < 0) {
		return false;
	}
	pkt1 = (AVPacketList *)api->godot_alloc(sizeof(AVPacketList));
	if (!pkt1)
		return false;
	pkt1->pkt = *pkt;
	pkt1->next = NULL;

	if (!q->last_pkt)
		q->first_pkt = pkt1;
	else
		q->last_pkt->next = pkt1;
	q->last_pkt = pkt1;
	q->nb_packets++;
	q->size += pkt1->pkt.size;
	return true;
}

bool packet_queue_get(PacketQueue *q, AVPacket *pkt) {
	AVPacketList *pkt1;

	pkt1 = q->first_pkt;
	if (pkt1) {
		q->first_pkt = pkt1->next;
		if (!q->first_pkt)
			q->last_pkt = NULL;
		q->nb_packets--;
		q->size -= pkt1->pkt.size;
		*pkt = pkt1->pkt;
		api->godot_free(pkt1);
		return true;
	} else {
		return false;
	}
}

bool packet_queue_peek(PacketQueue *q, AVPacket *pkt) {
	AVPacketList *pkt1;

	pkt1 = q->first_pkt;
	if (pkt1) {
		*pkt = pkt1->pkt;
		return true;
	} else {
		return false;
	}
}

void packet_queue_deinit(PacketQueue *q) {
	AVPacket pt;
	while (packet_queue_get(q, &pt)) {
		av_packet_unref(&pt);
	}
	api->godot_free(q);
}

#endif /* _PACKET_QUEUE_H */