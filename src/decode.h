#pragma once

#include "frame_queue.h"

#include <libavcodec/avcodec.h>

typedef struct Decoder {
    Thread tid;
    AVCodecContext *av_dec;

    PacketQueue pkt_q;
    AVPacket *av_pkt;
    int pkt_serial;
    Cond need_pkts;

    int finished;
    int packet_pending;
    int64_t start_pts;
    AVRational start_pts_tb;
    int64_t next_pts;
    AVRational next_pts_tb;
} Decoder;

int initialize_decoder(Decoder *dec, AVCodecParameters *codecpar, Cond need_pkts);
int decode_frame(Decoder *dec, AVFrame *av_frame, AVSubtitle *sub);
void destroy_decoder(Decoder *dec, FrameQueue *frame_q);
