#pragma once

#include "../decode.h"
#include "../frame_queue.h"
#include "../clock.h"
#include "../thread/thread.h"

#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>

#define DEFAULT_RATE 48000
#define DEFAULT_CHANNELS 2
#define BUFFER_SIZE (16*1024)

typedef struct AudioParams {
    int freq;
    AVChannelLayout ch_layout;
    enum AVSampleFormat fmt;
    int frame_size;
    int bytes_per_sec;
} AudioParams;

struct AudioPlayerBackend;

typedef struct AudioPlayer {
    Thread tid;

    struct SwrContext *swr_ctx;

    int stream_idx;
    int last_stream_idx;

    AudioParams src_params;
    AudioParams tgt_params;

    Decoder dec;
    FrameQueue frame_q;

    Clock clock;
    double clock_ts;
    int clock_serial;

    int ao_buf_size;
    int64_t cb_time;

    uint8_t *av_buf;
    int av_buf_idx;
    unsigned int av_buf_size;
    int av_buf_written_size;

    const struct AudioPlayerBackend *backend;
} AudioPlayer;

AudioPlayer *create_audio_player();
void *start_audio_player(void *ctx);

struct AudioPlayerBackend {
    __typeof__(create_audio_player) *create;
    __typeof__(start_audio_player) *start;
};
