#pragma once

#include "../decode.h"
#include "../frame_queue.h"
#include "../clock.h"
#include "../thread/thread.h"

#include <libswresample/swresample.h>
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

    int stream_idx;
    int last_stream_idx;

    struct SwrContext *swr_ctx;
    AudioParams src_params;
    AudioParams tgt_params;

    Decoder dec;
    FrameQueue frame_q;

    Clock clock;
    double clock_ts;
    int clock_serial;
    // uint64_t nb_samples;
    // double next_pts;

    const struct AudioPlayerBackend *backend;
    int ao_buf_size;
    int64_t cb_time;

    uint8_t *av_buf;
    int av_buf_idx;
    unsigned int av_buf_size;
    int av_buf_written_size;
} AudioPlayer;

int prepare_frame_data(AudioPlayer *a_player);

AudioPlayer *create_audio_player(int stream_idx);
void *start_audio_player(void *ctx);
double get_ts(AudioPlayer *a_player);
void stop_audio_player(AudioPlayer *a_player);
void destroy_audio_player(AudioPlayer **a_player);

struct AudioPlayerBackend {
    AudioPlayer *(*create) ();
    int (*start) (AudioPlayer *a_player);
    __typeof__(get_ts) *current_ts;
    __typeof__(stop_audio_player) *stop;
    __typeof__(destroy_audio_player) *destroy;
};
