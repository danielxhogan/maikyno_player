#pragma once

#include "packet_queue.h"
#include "thread/thread.h"

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#define VIDEO_PICTURE_QUEUE_SIZE 3
#define SUBPICTURE_QUEUE_SIZE 16
#define SAMPLE_QUEUE_SIZE 9

#define FRAME_QUEUE_SIZE FFMAX( \
    SAMPLE_QUEUE_SIZE, \
    FFMAX( \
        VIDEO_PICTURE_QUEUE_SIZE, \
        SUBPICTURE_QUEUE_SIZE \
    ) \
)

typedef struct FrameData {
    int64_t pkt_pos;
} FrameData;

typedef struct Frame {
    AVFrame *av_frame;
    AVSubtitle sub;
    int serial;
    double pts;
    double duration;
    int64_t pos;
    int width;
    int height;
    int format;
    AVRational sar;
    int uploaded;
    int flip_v;
} Frame;

typedef struct FrameQueue {
    Frame queue[FRAME_QUEUE_SIZE];
    int read_idx;
    int write_idx;
    int size;
    int max_size;
    int keep_last;
    int read_idx_shown;
    Mutex mutex;
    Cond cond;
    PacketQueue *pkt_q;
} FrameQueue;

void frame_queue_unref_item(Frame *frame);
int frame_queue_init(FrameQueue *frame_q, PacketQueue *pkt_q, int keep_last);
void frame_queue_destroy(FrameQueue *frame);
void frame_queue_signal(FrameQueue *frame);
Frame *frame_queue_peek(FrameQueue *frame);
Frame *frame_queue_peek_next(FrameQueue *frame);
Frame *frame_queue_peek_last(FrameQueue *frame);
Frame *frame_queue_peek_writable(FrameQueue *frame);
Frame *frame_queue_peek_readable(FrameQueue *frame);
void frame_queue_push(FrameQueue *frame);
void frame_queue_next(FrameQueue *frame);
int frame_queue_nb_remaining(FrameQueue *frame);
int64_t frame_queue_last_pos(FrameQueue *frame);
