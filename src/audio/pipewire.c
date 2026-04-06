#include "pipewire.h"
#include "audio.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

#include <libswresample/swresample.h>
#include <libavutil/time.h>

const struct AudioPlayerBackend pw_backend;

static int prepare_frame_data(AudioPlayer *a_player)
{
    int data_size;
    Frame *frame;

    do {
#if defined(_WIN32)
        while (frame_queue_nb_remaining(&a_player->frame_q) == 0) {
            if ((av_gettime_relative() - a_player->pw_ctx.cb_time) > 1000000LL * a_player->pw_ctx.buf_size / a_player->tgt_params.bytes_per_sec / 2)
                return -1;
            av_usleep (1000);
        }
#endif
        if (!(frame = frame_queue_peek_readable(&a_player->frame_q)))
            return -1;
        frame_queue_next(&a_player->frame_q);
    } while (frame->serial != a_player->dec.pkt_q.serial);

    if (frame->av_frame->format != a_player->src_params.fmt ||
        av_channel_layout_compare(&frame->av_frame->ch_layout,
            &a_player->src_params.ch_layout) ||
        frame->av_frame->sample_rate != a_player->src_params.freq
    ) {
        int ret;
        swr_free(&a_player->swr_ctx);

        ret = swr_alloc_set_opts2(&a_player->swr_ctx,
            &a_player->tgt_params.ch_layout,
            a_player->tgt_params.fmt,
            a_player->tgt_params.freq,
            &frame->av_frame->ch_layout,
            frame->av_frame->format,
            frame->av_frame->sample_rate,
            0, NULL);

        if (ret < 0 || swr_init(a_player->swr_ctx) < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Cannot create sample rate converter for conversion of "
                   "%d Hz %s %d channels to %d Hz %s %d channels!\n",
                    frame->av_frame->sample_rate,
                    av_get_sample_fmt_name(frame->av_frame->format),
                    frame->av_frame->ch_layout.nb_channels,
                    a_player->tgt_params.freq,
                    av_get_sample_fmt_name(a_player->tgt_params.fmt),
                    a_player->tgt_params.ch_layout.nb_channels);

            swr_free(&a_player->swr_ctx);
            return -1;
        }

        if (av_channel_layout_copy(&a_player->src_params.ch_layout,
            &frame->av_frame->ch_layout) < 0)
            return -1;
        a_player->src_params.freq = frame->av_frame->sample_rate;
        a_player->src_params.fmt = frame->av_frame->format;
    }

    if (a_player->swr_ctx) {
        const uint8_t **in = (const uint8_t **) frame->av_frame->extended_data;
        int out_count =
            (int64_t) frame->av_frame->nb_samples *
            a_player->tgt_params.freq /
            frame->av_frame->sample_rate +
            256;

        int out_size = av_samples_get_buffer_size(NULL,
            a_player->tgt_params.ch_layout.nb_channels,
            out_count, a_player->tgt_params.fmt, 0);

        int nb_samples;
        if (out_size < 0) {
            av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
            return -1;
        }

        unsigned int size;
        av_fast_malloc(&a_player->av_buf, &size, out_size);
        if (!a_player->av_buf)
            return AVERROR(ENOMEM);

        nb_samples = swr_convert(a_player->swr_ctx, &a_player->av_buf, out_count,
            in, frame->av_frame->nb_samples);
        if (nb_samples < 0) {
            av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
            return -1;
        }

        if (nb_samples == out_count) {
            av_log(NULL, AV_LOG_WARNING,
                "audio buffer player probably too small\n");
            if (swr_init(a_player->swr_ctx) < 0)
                swr_free(&a_player->swr_ctx);
        }

        data_size = nb_samples *
            a_player->tgt_params.ch_layout.nb_channels *
            av_get_bytes_per_sample(a_player->tgt_params.fmt);
    } else {
    data_size = av_samples_get_buffer_size(NULL,
        frame->av_frame->ch_layout.nb_channels,
        frame->av_frame->nb_samples,
        frame->av_frame->format, 1);

        a_player->av_buf = frame->av_frame->data[0];
    }

    /* update the audio clock with the pts */
    if (!isnan(frame->pts)) {
        a_player->clock_ts =
            frame->pts +
            (double) frame->av_frame->nb_samples /
            frame->av_frame->sample_rate;
    }
    else {
        a_player->clock_ts = NAN;
    }

    a_player->clock_serial = frame->serial;
    return data_size;
}

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

AudioPlayer *initialize_pipewire()
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
        return NULL;
    }

    pw_ctx->a_player.src_params = pw_ctx->a_player.tgt_params;
    pw_ctx->a_player.av_buf_size = 0;
    pw_ctx->a_player.av_buf_idx = 0;

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

    if (frame_queue_init(&pw_ctx->a_player.frame_q, &pw_ctx->a_player.dec.pkt_q, 1) != 0)
        printf("Failed to init audio frame queue.\n");

    return &pw_ctx->a_player;
}

void *start_pipewire(void *ctx)
{
    AudioPlayer *a_player = ctx;
    PipewireContext *pw_ctx = ctx;

    AVFrame *av_frame = av_frame_alloc();
    Frame *frame;
    int got_frame = 0;
    int ret = 0;

    if (!av_frame)
        return NULL;

    packet_queue_start(&a_player->dec.pkt_q);
    init_clock(&a_player->clock, &a_player->dec.pkt_q.serial);

    if (thread_create(&pw_ctx->main_loop_tid, pw_main_loop_run, pw_ctx->main_loop) != 0) {
        fprintf(stderr, "Failed to start pw_ctx main loop thread.\n");
        return NULL;
    }

    do {
        if ((got_frame = decode_frame(&a_player->dec, av_frame, NULL)) < 0)
            goto the_end;

        if (got_frame) {
            FrameData *fd = av_frame->opaque_ref ? (FrameData *) av_frame->opaque_ref->data : NULL;

            if (!(frame = frame_queue_peek_writable(&a_player->frame_q)))
                goto the_end;

            frame->pts = (av_frame->pts == AV_NOPTS_VALUE) ? NAN : av_frame->pts * av_q2d(frame->av_frame->time_base);
            frame->pos = fd ? fd->pkt_pos : -1;
            frame->serial = a_player->dec.pkt_serial;
            frame->duration = av_q2d((AVRational) {av_frame->nb_samples, av_frame->sample_rate});

            av_frame_move_ref(frame->av_frame, av_frame);
            frame_queue_push(&a_player->frame_q);

            if (a_player->dec.pkt_q.serial != a_player->dec.pkt_serial)
                break;

            if (ret == AVERROR_EOF)
                a_player->dec.finished = a_player->dec.pkt_serial;
        }
    } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);

 the_end:
    av_frame_free(&av_frame);
    return NULL;
}

const struct AudioPlayerBackend pw_backend = {
    .create = initialize_pipewire,
    .start = start_pipewire
};
