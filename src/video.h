#pragma once

#include "decode.h"
#include "clock.h"

#include <libplacebo/options.h>
#include <libplacebo/utils/frame_queue.h>

#include <libplacebo/vulkan.h>
#include <libplacebo/opengl.h>

typedef struct VideoRenderer {
    AVStream *stream;
    int stream_idx;
    int last_stream_idx;
    float aspect_ratio;

    void (*render_cb) (void *ctx);
    void *render_cb_ctx;

    Decoder dec;

    Clock clock;
    uint64_t ts_start;

    pl_opengl gl;

    pl_vulkan vk;
    pl_renderer renderer;
    pl_queue frame_q;
    struct pl_queue_params qparams;
    struct pl_frame_mix mix;
    pl_log log;
    pl_options opts;
} VideoRenderer;

int create_video_renderer(VideoRenderer *v_renderer, int stream_idx,
    void (*render_cb) (void *ctx), void *render_cb_ctx,
    pl_vulkan vk);
void *start_video_decoder(void *ctx);
