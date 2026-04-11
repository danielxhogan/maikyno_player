#include "video.h"
#include "mkp.h"

#include <libplacebo/renderer.h>
#include <libplacebo/utils/libav.h>
#include <libplacebo/gpu.h>

int create_video_renderer(VideoRenderer *v_renderer, int stream_idx,
    void (*render_cb) (void *ctx), void *render_cb_ctx,
    pl_vulkan vk)
{
    v_renderer->stream_idx = stream_idx;
    v_renderer->last_stream_idx = stream_idx;
    v_renderer->ts_start = 0;
    v_renderer->render_cb = render_cb;
    v_renderer->render_cb_ctx = render_cb_ctx;
    v_renderer->aspect_ratio = (float) v_renderer->stream->codecpar->width /
       (float) v_renderer->stream->codecpar->height;

    v_renderer->opts = pl_options_alloc(v_renderer->log);
    pl_options_reset(v_renderer->opts, &pl_render_default_params);

    v_renderer->log = pl_log_create(PL_API_VER, pl_log_params(
        .log_cb    = pl_log_color,
        .log_level = PL_LOG_WARN
    ));

    if (vk) {
        v_renderer->vk = vk;
        v_renderer->frame_q = pl_queue_create(vk->gpu);
        v_renderer->renderer = pl_renderer_create(v_renderer->log, vk->gpu);
    } else {
        v_renderer->gl = pl_opengl_create(v_renderer->log, pl_opengl_params(
            .allow_software = true,
        ));
        v_renderer->frame_q = pl_queue_create(v_renderer->gl->gpu);
        v_renderer->renderer = pl_renderer_create(v_renderer->log, v_renderer->gl->gpu);
    }

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

static uint64_t current_time()
{
    struct timespec tp = { .tv_sec = 0, .tv_nsec = 0 };
    timespec_get(&tp, TIME_UTC);
    return tp.tv_sec * UINT64_C(1000000000) + tp.tv_nsec;
}

static double time_diff(uint64_t a, uint64_t b)
{
    double frequency = 1e9;
    if (b > a)
        return (b - a) / -frequency;
    else
        return (a - b) / frequency;
}

int mkp_render_from_pl_frame(MkPlayer *player, struct pl_frame *frame)
{
    uint64_t ts_pre_update = current_time();
    if (!player->v_renderer.ts_start)
        player->v_renderer.ts_start = ts_pre_update;

    player->v_renderer.qparams.timeout = 0;
    player->v_renderer.qparams.pts =
        time_diff(ts_pre_update, player->v_renderer.ts_start);

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
        return -1;
    }

    if (player->v_renderer.render_cb)
        player->v_renderer.render_cb(player->v_renderer.render_cb_ctx);

    return 0;
}

int mkp_render_from_fbo(MkPlayer *player, unsigned int fbo, int width, int height)
{
    VideoRenderer *v_renderer = &player->v_renderer;

    struct pl_opengl_wrap_params wparams = {
        .framebuffer = fbo,
        .width = width,
        .height = height,
    };

    pl_tex tgt_tex =
        pl_opengl_wrap(player->v_renderer.gl->gpu, &wparams);

    float height_matched = (float) tgt_tex->params.w / v_renderer->aspect_ratio;
    float width_matched = (float) tgt_tex->params.h * v_renderer->aspect_ratio;
    float crop_w, crop_h, x_margin, y_margin;

    if (height_matched > (float) tgt_tex->params.h) {
        crop_h = (float) tgt_tex->params.h;
        crop_w = width_matched;
        x_margin = ((float) tgt_tex->params.w - width_matched) / 2;
        y_margin = 0;
    } else {
        crop_h = height_matched;
        crop_w = (float) tgt_tex->params.w;
        x_margin = 0;
        y_margin = ((float) tgt_tex->params.h - height_matched) / 2;
    }

    struct pl_frame frame = {
        .num_planes = 1,
        .planes[0] = {
            .texture = tgt_tex,
            .components = 3,
            .component_mapping = { 0, 1, 2 },
            .flipped = true
        },
        .crop = {
            .x0 = x_margin,
            .x1 = crop_w + x_margin,
            .y0 = y_margin,
            .y1 = crop_h + y_margin
        }
    };

    return mkp_render_from_pl_frame(player, &frame);
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
