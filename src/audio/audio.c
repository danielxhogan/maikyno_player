#include "audio.h"
#include <libavcodec/avcodec.h>

extern const struct AudioPlayerBackend pw_backend;

const struct AudioPlayerBackend *a_backend = &pw_backend;

AudioPlayer *create_audio_player(int stream_idx)
{
    AudioPlayer *a_player = NULL;
    a_player = a_backend->create(stream_idx);
    if (!a_player)
        goto end;

    a_player->src_params = a_player->tgt_params;
    a_player->stream_idx = stream_idx;
    a_player->last_stream_idx = stream_idx;

    if (frame_queue_init(&a_player->frame_q, &a_player->dec.pkt_q, 1) != 0) {
        printf("Failed to init audio frame queue.\n");
        goto end;
    }

    return a_player;

end:
    destroy_audio_player(&a_player);
    return NULL;
}

void *start_audio_player(void *ctx)
{
    AudioPlayer *a_player = ctx;

    if (a_player->backend->start(a_player) != 0) {
        a_player->dec.pkt_q.abort_request = 1;
        return NULL;
    }

    AVFrame *av_frame = av_frame_alloc();
    Frame *frame;
    int got_frame = 0;
    int ret = 0;

    if (!av_frame)
        return NULL;

    packet_queue_start(&a_player->dec.pkt_q);
    init_clock(&a_player->clock, &a_player->dec.pkt_q.serial);

    do {
        if ((got_frame = decode_frame(&a_player->dec, av_frame, NULL)) < 0)
            goto the_end;

        if (got_frame) {
            FrameData *fd = av_frame->opaque_ref
                ? (FrameData *) av_frame->opaque_ref->data
                : NULL;

            if (!(frame = frame_queue_peek_writable(&a_player->frame_q)))
                goto the_end;

            frame->pts = (av_frame->pts == AV_NOPTS_VALUE)
                ? NAN
                : av_frame->pts * av_q2d(frame->av_frame->time_base);

            frame->pos = fd ? fd->pkt_pos : -1;
            frame->serial = a_player->dec.pkt_serial;
            frame->duration =
                av_q2d((AVRational) {av_frame->nb_samples, av_frame->sample_rate});

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

int prepare_frame_data(AudioPlayer *a_player)
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

void stop_audio_player(AudioPlayer *a_player)
{
    packet_queue_abort(&a_player->dec.pkt_q);
    thread_join(a_player->tid);
    a_player->backend->stop(a_player);
}

void destroy_audio_player(AudioPlayer **a_player)
{
    swr_free(&(*a_player)->swr_ctx);
    av_freep(&(*a_player)->av_buf);
    destroy_decoder(&(*a_player)->dec, &(*a_player)->frame_q);
    frame_queue_destroy(&(*a_player)->frame_q);

    (*a_player)->backend->destroy(a_player);
}

double get_ts(AudioPlayer *a_player)
{
    return a_player->backend->current_ts(a_player);
}
