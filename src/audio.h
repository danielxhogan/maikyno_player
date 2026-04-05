#pragma once

#include "./includes/libmkp/mkplayer.h"
#include "thread/thread.h"

#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>

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
    int buf_size;
    int64_t cb_time;
} PwContext;

void initialize_audio_output(MkPlayer *player);
void *audio_thread(void *ctx);
