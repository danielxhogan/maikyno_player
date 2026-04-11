#include "pipewire.h"
#include "audio.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

#include <libswresample/swresample.h>
#include <libavutil/time.h>

const struct AudioPlayerBackend pw_backend;

static void on_process(void *ctx)
{
    PipewireContext *pw_ctx = ctx;
    struct pw_buffer *buffer;
    struct spa_buffer *buf;
    uint8_t *buf_data;

    pw_ctx->a_player.cb_time = av_gettime_relative();

    if ((buffer = pw_stream_dequeue_buffer(pw_ctx->stream)) == NULL) {
        pw_log_warn("out of buffers: %m");
        return;
    }
    
    buf = buffer->buffer;
    if ((buf_data = buf->datas[0].data) == NULL)
        return;

    uint32_t buf_data_size = buf->datas[0].maxsize;
    pw_ctx->a_player.ao_buf_size = buf_data_size;

    while (buf_data_size > 0) {
        if (pw_ctx->a_player.av_buf_idx >= pw_ctx->a_player.av_buf_size)
        {
            pw_ctx->a_player.av_buf_size = prepare_frame_data(&pw_ctx->a_player);
           if (pw_ctx->a_player.av_buf_size < 0) {
                /* if error, just output silence */
               pw_ctx->a_player.av_buf = NULL;
               pw_ctx->a_player.av_buf_size = 512 /
                    pw_ctx->a_player.tgt_params.frame_size *
                    pw_ctx->a_player.tgt_params.frame_size;
           }
           pw_ctx->a_player.av_buf_idx = 0;
        }

        int writing_data_len = pw_ctx->a_player.av_buf_size - pw_ctx->a_player.av_buf_idx;
        if (writing_data_len > buf_data_size)
            writing_data_len = buf_data_size;

        if (pw_ctx->a_player.av_buf)
            memcpy(buf_data,
                (uint8_t *) pw_ctx->a_player.av_buf + pw_ctx->a_player.av_buf_idx,
                writing_data_len);

        buf_data_size -= writing_data_len;
        buf_data += writing_data_len;
        pw_ctx->a_player.av_buf_idx += writing_data_len;
    }

    buf->datas[0].chunk->offset = 0;
    buf->datas[0].chunk->stride = pw_ctx->a_player.tgt_params.frame_size;
    buf->datas[0].chunk->size = buf->datas[0].maxsize;
    pw_stream_queue_buffer(pw_ctx->stream, buffer);

    pw_ctx->a_player.av_buf_written_size =
        pw_ctx->a_player.av_buf_size - pw_ctx->a_player.av_buf_idx;

    if (!isnan(pw_ctx->a_player.clock_ts)) {
        set_clock_at(&pw_ctx->a_player.clock,
            pw_ctx->a_player.clock_ts -
            (double) (2 * pw_ctx->a_player.ao_buf_size + pw_ctx->a_player.av_buf_written_size) /
            pw_ctx->a_player.tgt_params.bytes_per_sec,
            pw_ctx->a_player.clock_serial,
            pw_ctx->a_player.cb_time / 1000000.0);
    }
}

static void do_quit(void *ctx, int signal_number)
{
        PipewireContext *pw_ctx = ctx;
        pw_main_loop_quit(pw_ctx->main_loop);
}

static const struct pw_stream_events stream_events = {
        PW_VERSION_STREAM_EVENTS,
        .process = on_process,
};

static void stop_pipewire(AudioPlayer *a_player);
static void destroy_pipewire_context(AudioPlayer **a_player);

static AudioPlayer *initialize_pipewire()
{
    PipewireContext *pw_ctx = calloc(1, sizeof(PipewireContext));
    if (!pw_ctx)
        return NULL;

    pw_ctx->a_player.backend = &pw_backend;

    const struct spa_pod *params[1];
    uint32_t n_params = 0;
    uint8_t buffer[1024];
    struct pw_properties *props;
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

    pw_init(NULL, NULL);

    pw_ctx->main_loop = pw_main_loop_new(NULL);
    pw_ctx->loop = pw_main_loop_get_loop(pw_ctx->main_loop);

    pw_loop_add_signal(pw_ctx->loop, SIGINT, do_quit, pw_ctx);
    pw_loop_add_signal(pw_ctx->loop, SIGTERM, do_quit, pw_ctx);

    props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Playback",
        PW_KEY_MEDIA_ROLE, "Movie",
        NULL);

    pw_ctx->stream = pw_stream_new_simple(
        pw_ctx->loop,
        "maikyno-audio",
        props,
        &stream_events,
        pw_ctx);

    pw_ctx->a_player.tgt_params.fmt = AV_SAMPLE_FMT_S32;
    pw_ctx->a_player.tgt_params.freq = DEFAULT_RATE;
    av_channel_layout_default(&pw_ctx->a_player.tgt_params.ch_layout, DEFAULT_CHANNELS);

    pw_ctx->a_player.tgt_params.frame_size = av_samples_get_buffer_size(NULL,
        pw_ctx->a_player.tgt_params.ch_layout.nb_channels, 1, pw_ctx->a_player.tgt_params.fmt, 1);

    pw_ctx->a_player.tgt_params.bytes_per_sec = av_samples_get_buffer_size(NULL,
        pw_ctx->a_player.tgt_params.ch_layout.nb_channels, pw_ctx->a_player.tgt_params.freq,
        pw_ctx->a_player.tgt_params.fmt, 1);

    if (pw_ctx->a_player.tgt_params.bytes_per_sec <= 0 || pw_ctx->a_player.tgt_params.frame_size <= 0) {
        av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
        goto end;
    }

    params[n_params++] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat,
        &SPA_AUDIO_INFO_RAW_INIT(
            .format = SPA_AUDIO_FORMAT_S32,
            .channels = DEFAULT_CHANNELS,
            .rate = DEFAULT_RATE));

    pw_stream_connect(pw_ctx->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
        PW_STREAM_FLAG_AUTOCONNECT |
        PW_STREAM_FLAG_MAP_BUFFERS |
        PW_STREAM_FLAG_RT_PROCESS,
        params, n_params);

    return &pw_ctx->a_player;

end:
    AudioPlayer *a_player_ptr = &pw_ctx->a_player;
    destroy_pipewire_context(&a_player_ptr);
    return NULL;
}

static int start_pipewire(AudioPlayer *a_player)
{
    PipewireContext *pw_ctx = (PipewireContext *) a_player;
    if (thread_create(&pw_ctx->main_loop_tid, pw_main_loop_run, pw_ctx->main_loop) != 0) {
        fprintf(stderr, "Failed to start pw_ctx main loop thread.\n");
        return -1;
    }
    return 0;
}

static double current_ts(AudioPlayer *a_player)
{
    PipewireContext *pw_ctx = (PipewireContext *) a_player;
    if (!pw_ctx)
        return -1;

    struct pw_time pwt;
    if (pw_stream_get_time_n(pw_ctx->stream, &pwt, sizeof(pwt)) < 0)
        return -1;

    uint64_t now = pw_stream_get_nsec(pw_ctx->stream);
    int64_t diff = (int64_t) now - pwt.now;
    int64_t elapsed_ticks =
        (pwt.rate.denom * diff) / (pwt.rate.num * 1000000000LL);
    int64_t current_pos_ticks = pwt.ticks + elapsed_ticks - pwt.delay;
    return (double) current_pos_ticks * pwt.rate.num / pwt.rate.denom;
}

static void stop_pipewire(AudioPlayer *a_player)
{
    PipewireContext *pw_ctx = (PipewireContext *) a_player;
    if (!pw_ctx)
        return;
    pw_main_loop_quit(pw_ctx->main_loop);
    thread_join(pw_ctx->main_loop_tid);
}

static void destroy_pipewire_context(AudioPlayer **a_player)
{
    PipewireContext **pw_ctx = (PipewireContext **) a_player;
    if (!pw_ctx || !*pw_ctx)
        return;

    pw_stream_destroy((*pw_ctx)->stream);
    pw_deinit();
    free(*pw_ctx);
    *pw_ctx = NULL;
}

const struct AudioPlayerBackend pw_backend = {
    .create = initialize_pipewire,
    .start = start_pipewire,
    .current_ts = current_ts,
    .stop = stop_pipewire,
    .destroy = destroy_pipewire_context
};
