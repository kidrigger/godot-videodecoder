
#ifndef _PACKET_QUEUE_H
#define _PACKET_QUEUE_H

#define PACKET_QUEUE_THREADSAFE

#include <gdnative_api_struct.gen.h>
#include <libavformat/avformat.h>

#ifdef PACKET_QUEUE_THREADSAFE
#include <pthread.h>
#endif

extern const godot_gdnative_core_api_struct *api;

typedef struct PacketQueue {
	AVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
#ifdef PACKET_QUEUE_THREADSAFE
	pthread_mutex_t lock;
#endif
} PacketQueue;

PacketQueue *packet_queue_init();

int packet_queue_put(PacketQueue *q, AVPacket *pkt);

int packet_queue_get(PacketQueue *q, AVPacket *pkt);

void packet_queue_deinit(PacketQueue *q);

#endif /* _PACKET_QUEUE_H */