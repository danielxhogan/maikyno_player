#pragma once

#include "audio.h"

#include "../thread/thread.h"

typedef struct PipewireContext {
    AudioPlayer a_player;
    Thread main_loop_tid;
    struct pw_main_loop *main_loop;
    struct pw_loop *loop;
    struct pw_stream *stream;
} PipewireContext;
