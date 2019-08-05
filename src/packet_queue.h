
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

void packet_queue_flush(PacketQueue *q) {
	AVPacketList *pkt, *pkt1;

	for (pkt = q->first_pkt; pkt; pkt = pkt1) {
		pkt1 = pkt->next;
		av_packet_unref(&pkt->pkt);
		api->godot_free(pkt);
	}
	q->last_pkt = NULL;
	q->first_pkt = NULL;
	q->nb_packets = 0;
	q->size = 0;
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt) {

	AVPacketList *pkt1;
	pkt1 = (AVPacketList *)api->godot_alloc(sizeof(AVPacketList));
	if (!pkt1)
		return -1;
	pkt1->pkt = *pkt;
	pkt1->next = NULL;

	if (!q->last_pkt)
		q->first_pkt = pkt1;
	else
		q->last_pkt->next = pkt1;
	q->last_pkt = pkt1;
	q->nb_packets++;
	q->size += pkt1->pkt.size;
	return 0;
}

int packet_queue_get(PacketQueue *q, AVPacket *pkt) {
	AVPacketList *pkt1;
	int ret;

	pkt1 = q->first_pkt;
	if (pkt1) {

		q->first_pkt = pkt1->next;
		if (!q->first_pkt)
			q->last_pkt = NULL;
		q->nb_packets--;
		q->size -= pkt1->pkt.size;
		*pkt = pkt1->pkt;
		api->godot_free(pkt1);
		return 1;
	} else {
		return 0;
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
