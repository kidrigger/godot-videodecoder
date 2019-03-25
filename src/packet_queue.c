
#include "packet_queue.h"

void _lock(PacketQueue *q) {
#ifdef PACKET_QUEUE_THREAD_SAFE
	pthread_mutex_lock(&q->lock);
#endif
}

void _unlock(PacketQueue *q) {
#ifdef PACKET_QUEUE_THREAD_SAFE
	pthread_mutex_unlock(&q->lock);
#endif
}

PacketQueue *packet_queue_init() {
	PacketQueue *q;
	q = (PacketQueue *)api->godot_alloc(sizeof(PacketQueue));
	if (q != NULL) {
		memset(q, 0, sizeof(PacketQueue));
	}

#ifdef PACKET_QUEUE_THREAD_SAFE
	pthread_mutex_init(&q->lock, NULL);
#endif

	return q;
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt) {

	AVPacketList *pkt1;
	if (av_packet_ref(pkt, pkt) < 0) {
		return -1;
	}
	pkt1 = (AVPacketList *)api->godot_alloc(sizeof(AVPacketList));
	if (!pkt1)
		return -1;
	pkt1->pkt = *pkt;
	pkt1->next = NULL;

	_lock(q);
	if (!q->last_pkt)
		q->first_pkt = pkt1;
	else
		q->last_pkt->next = pkt1;
	q->last_pkt = pkt1;
	q->nb_packets++;
	q->size += pkt1->pkt.size;
	_unlock(q);
	return 0;
}

int packet_queue_get(PacketQueue *q, AVPacket *pkt) {
	AVPacketList *pkt1;
	int ret;

	_lock(q);
	pkt1 = q->first_pkt;
	if (pkt1) {

		q->first_pkt = pkt1->next;
		if (!q->first_pkt)
			q->last_pkt = NULL;
		q->nb_packets--;
		q->size -= pkt1->pkt.size;
		_unlock(q);
		*pkt = pkt1->pkt;
		api->godot_free(pkt1);
		return 1;
	} else {
		_unlock(q);
		return 0;
	}
}

void packet_queue_deinit(PacketQueue *q) {
	AVPacket pt;
	_lock(q);
	while (packet_queue_get(q, &pt)) {
		av_packet_unref(&pt);
	}
	_unlock(q);

#ifdef PACKET_QUEUE_THREAD_SAFE
	pthread_mutex_destroy(&q->lock);
#endif

	api->godot_free(q);
}