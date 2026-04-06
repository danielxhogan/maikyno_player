#pragma once

#include "audio/audio.h"

typedef struct MkPlayer {
    char *src;

    AVFormatContext *av_fmt;
    Thread demux_tid;
    Cond need_pkts;

    int abort_request;
    int max_frame_duration;
    int eof;

    AudioPlayer *a_player;
    // VideoPlayer v_player;
} MkPlayer;
