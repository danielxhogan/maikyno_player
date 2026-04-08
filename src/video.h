#pragma once

#include "decode.h"
#include "clock.h"

#include <libplacebo/options.h>
#include <libplacebo/utils/frame_queue.h>
#include <libplacebo/vulkan.h>

typedef struct VideoRenderer {
    AVStream *stream;
    int stream_idx;
    int last_stream_idx;

    Decoder dec;

    Clock clock;
    uint64_t ts_start;

    pl_vulkan vk;
    pl_renderer renderer;
    pl_queue frame_q;
    struct pl_queue_params qparams;
    struct pl_frame_mix mix;
    pl_log log;
    pl_options opts;
} VideoRenderer;

int create_video_renderer(VideoRenderer *v_renderer,
    pl_vulkan vk, int stream_idx);
void *start_video_decoder(void *ctx);
