
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

PacketQueue *packet_queue_init();

int packet_queue_put(PacketQueue *q, AVPacket *pkt);

int packet_queue_get(PacketQueue *q, AVPacket *pkt);

void packet_queue_deinit(PacketQueue *q);

#endif /* _PACKET_QUEUE_H */