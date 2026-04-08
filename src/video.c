#include "video.h"
#include "mkp.h"

// #include <libplacebo/renderer.h>
#include <libplacebo/utils/libav.h>

int create_video_renderer(VideoRenderer *v_renderer,
    pl_vulkan vk, int stream_idx)
{
    v_renderer->stream_idx = stream_idx;
    v_renderer->last_stream_idx = stream_idx;
    v_renderer->ts_start = 0;

    v_renderer->opts = pl_options_alloc(v_renderer->log);
    pl_options_reset(v_renderer->opts, &pl_render_default_params);

    v_renderer->log = pl_log_create(PL_API_VER, pl_log_params(
        .log_cb    = pl_log_color,
        .log_level = PL_LOG_WARN
    ));

    v_renderer->vk = vk;
    v_renderer->frame_q = pl_queue_create(vk->gpu);
    v_renderer->renderer = pl_renderer_create(v_renderer->log, vk->gpu);

    v_renderer->qparams = *pl_queue_params(
        .interpolation_threshold = 0.01,
        .timeout = UINT64_MAX
    );

    return 0;
}

static bool map_frame(pl_gpu gpu, pl_tex *tex,
                      const struct pl_source_frame *src,
                      struct pl_frame *out_frame)
{
    AVFrame *frame = src->frame_data;
    VideoRenderer *v_renderer = frame->opaque;
    bool ok = pl_map_avframe_ex(gpu, out_frame, pl_avframe_params(
        .frame = frame,
        .tex = tex,
        .map_dovi = true
    ));

    av_frame_free(&frame);
    if (!ok) {
        fprintf(stderr, "Failed mapping avframe.\n");
        return false;
    }

    pl_frame_copy_stream_props(out_frame, v_renderer->stream);
    return true;
}

static void unmap_frame(pl_gpu gpu, struct pl_frame *frame,
                        const struct pl_source_frame *src)
{
    pl_unmap_avframe(gpu, frame);
}

static void discard_frame(const struct pl_source_frame *src)
{
    AVFrame *frame = src->frame_data;
    av_frame_free(&frame);
    printf("Dropped frame with pts %.3f.\n", src->pts);
}

uint64_t current_time()
{
    struct timespec tp = { .tv_sec = 0, .tv_nsec = 0 };
    timespec_get(&tp, TIME_UTC);
    return tp.tv_sec * UINT64_C(1000000000) + tp.tv_nsec;
}

double time_diff(uint64_t a, uint64_t b)
{
    double frequency = 1e9;
    if (b > a)
        return (b - a) / -frequency;
    else
        return (a - b) / frequency;
}

int render_from_pl_frame(MkPlayer *player, struct pl_frame *frame)
{
    uint64_t ts_pre_update = current_time();
    if (!player->v_renderer.ts_start)
        player->v_renderer.ts_start = ts_pre_update;

    player->v_renderer.qparams.timeout = 0;
    player->v_renderer.qparams.pts = time_diff(ts_pre_update, player->v_renderer.ts_start);

retry:
    switch (pl_queue_update(player->v_renderer.frame_q,
        &player->v_renderer.mix, &player->v_renderer.qparams)) {
    case PL_QUEUE_ERR:
        return -1;
    case PL_QUEUE_EOF:
        printf("End of file reached.\n");
        return true;
    case PL_QUEUE_OK:
        break;
    case PL_QUEUE_MORE:
        player->v_renderer.qparams.timeout = UINT64_MAX;
        goto retry;
    }

    uint64_t ts_post_update = current_time();

    if (player->v_renderer.qparams.timeout) {
        player->v_renderer.ts_start += ts_post_update - ts_pre_update;
    }

    if (!pl_render_image_mix(player->v_renderer.renderer,
        &player->v_renderer.mix, frame, &player->v_renderer.opts->params)) {
        printf("here\n");
        return -1;
        }

    return 0;
}

void *start_video_decoder(void *ctx)
{
    VideoRenderer *v_renderer = ctx;
    int ret = 0, got_frame = 0;
    double first_pts = 0.0, base_pts = 0.0, last_pts = 0.0;
    uint64_t num_frames = 0;

    AVFrame *av_frame = av_frame_alloc();
    if (!av_frame)
        return NULL;

    packet_queue_start(&v_renderer->dec.pkt_q);
    init_clock(&v_renderer->clock, &v_renderer->dec.pkt_q.serial);

    do {
        if ((got_frame = decode_frame(&v_renderer->dec, av_frame, NULL)) < 0)
            goto end;

        if (got_frame) {
            last_pts = av_frame->pts * av_q2d(v_renderer->stream->time_base);
            if (num_frames++ == 0)
                first_pts = last_pts;
            av_frame->opaque = v_renderer;
            pl_queue_push_block(v_renderer->frame_q,
                UINT64_MAX, &(struct pl_source_frame)
            {
                .pts = last_pts - first_pts + base_pts,
                .map = map_frame,
                .unmap = unmap_frame,
                .discard = discard_frame,
                .frame_data = av_frame
            });
            av_frame = av_frame_alloc();
        }
    } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);

end:
    pl_queue_push(v_renderer->frame_q, NULL);
    av_frame_free(&av_frame);
    return NULL;
}
