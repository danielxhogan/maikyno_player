#pragma once

#include "frame_queue.h"
#include "packet_queue.h"
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

typedef struct Decoder {
    AVPacket *pkt;
    PacketQueue *pkt_q;
    AVCodecContext *dec_ctx;
    int pkt_serial;
    int finished;
    int packet_pending;
    Cond empty_queue_cond;
    int64_t start_pts;
    AVRational start_pts_tb;
    int64_t next_pts;
    AVRational next_pts_tb;
    Thread decoder_tid;
} Decoder;

typedef struct AudioParams {
    int freq;
    AVChannelLayout ch_layout;
    enum AVSampleFormat fmt;
    int frame_size;
    int bytes_per_sec;
} AudioParams;

typedef struct PwContext {
    Thread main_loop_tid;
    struct pw_main_loop *main_loop;
    struct pw_loop *loop;
    struct pw_stream *stream;
    float accumulator;
    struct spa_source *refill_event;
    struct spa_ringbuffer ring;
    float buffer[BUFFER_SIZE * DEFAULT_CHANNELS];
} PwContext;

typedef struct MkPlayer {
    char *src;

    PwContext pw;
    int64_t audio_callback_time;

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

    Clock ext_clock;
    
    uint8_t *audio_buf;
    int audio_buf_index;
    unsigned int audio_buf_size;
    int audio_write_buf_size;
    uint8_t *audio_buf1;
    unsigned int audio_buf1_size;
    int audio_hw_buf_size;

    int abort_request;
    int max_frame_duration;
    int eof;


} MkPlayer;
