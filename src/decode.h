#pragma once

#include "packet_queue.h"
#include "thread/thread.h"

#include <libavcodec/avcodec.h>

typedef struct Decoder {
    AVPacket *av_pkt;
    int pkt_serial;
    PacketQueue *pkt_q;
    AVCodecContext *dec_ctx;
    int finished;
    int packet_pending;
    Cond empty_queue_cond;
    int64_t start_pts;
    AVRational start_pts_tb;
    int64_t next_pts;
    AVRational next_pts_tb;
    Thread decoder_tid;
} Decoder;

int initialize_decoder(Decoder *dec, AVCodecParameters *codecpar,
    PacketQueue *pkt_q, Cond empty_queue_cond);
int decode_frame(Decoder *dec, AVFrame *av_frame, AVSubtitle *sub);
void decoder_destroy(Decoder *dec);