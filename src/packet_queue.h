#pragma once

#include "thread/thread.h"

#include <libavutil/fifo.h>
#include <libavcodec/packet.h>

typedef struct Packet {
    AVPacket *av_pkt;
    int serial;
} Packet;

typedef struct PacketQueue {
    AVFifo *pkt_list;
    int nb_packets;
    int size;
    int64_t duration;
    int abort_request;
    int serial;
    Mutex mutex;
    Cond cond;
} PacketQueue;

int packet_queue_put_private(PacketQueue *pkt_q, AVPacket *av_pkt);
int packet_queue_put(PacketQueue *pkt_q, AVPacket *av_pkt);
int packet_queue_put_nullpacket(PacketQueue *pkt_q, AVPacket *av_pkt, int stream_index);
int packet_queue_init(PacketQueue *pkt_q);
void packet_queue_flush(PacketQueue *pkt_q);
void packet_queue_destroy(PacketQueue *pkt_q);
void packet_queue_abort(PacketQueue *pkt_q);
void packet_queue_start(PacketQueue *pkt_q);
int packet_queue_get(PacketQueue *pkt_q, AVPacket *av_pkt, int block, int *serial);
