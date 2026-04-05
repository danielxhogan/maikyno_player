#pragma once

#include "decode.h"
#include "audio.h"
#include "frame_queue.h"
#include "clock.h"

#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>

#include <spa/utils/ringbuffer.h>

#include <stdint.h>

#define MAX_QUEUE_SIZE (15 * 1024 * 1024)
#define MIN_FRAMES 25

#define DEFAULT_RATE 48000
#define DEFAULT_CHANNELS 2
#define BUFFER_SIZE (16*1024)

typedef struct MkPlayer {
    char *src;

    PwContext pw_ctx;

    AVFormatContext *fmt_ctx;
    struct SwrContext *swr_ctx;
    AVStream *av_a_stream;
    int a_stream_idx;
    int last_a_stream_idx;

    Thread read_tid;
    Cond continue_read_thread;

    AudioParams src_a_params;
    AudioParams tgt_a_params;

    Decoder a_dec;
    FrameQueue a_frame_q;
    PacketQueue a_pkt_q;

    Clock a_clock;
    double audio_clock;
    int audio_clock_serial;

    uint8_t *audio_buf;
    int audio_buf_index;
    unsigned int audio_buf_size;
    int audio_write_buf_size;

    int abort_request;
    int max_frame_duration;
    int eof;
} MkPlayer;
