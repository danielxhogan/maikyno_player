#include "mkp.h"
#include "frame_queue.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

#include <libswresample/swresample.h>
#include <libavutil/time.h>

int decoder_decode_frame(Decoder *dec, AVFrame *frame, AVSubtitle *sub);

static int prepare_frame_data(MkPlayer *player)
{
    int data_size, resampled_data_size;
    av_unused double audio_clock0;
    Frame *frame;

    do {
#if defined(_WIN32)
        while (frame_queue_nb_remaining(&player->a_frame_q) == 0) {
            if ((av_gettime_relative() - audio_callback_time) > 1000000LL * player->audio_hw_buf_size / player->tgt_a_params.bytes_per_sec / 2)
                return -1;
            av_usleep (1000);
        }
#endif
        if (!(frame = frame_queue_peek_readable(&player->a_frame_q)))
            return -1;
        frame_queue_next(&player->a_frame_q);
    } while (frame->serial != player->a_pkt_q.serial);

    data_size = av_samples_get_buffer_size(NULL,
        frame->av_frame->ch_layout.nb_channels,
        frame->av_frame->nb_samples,
        frame->av_frame->format, 1);

    if (frame->av_frame->format != player->src_a_params.fmt ||
        av_channel_layout_compare(&frame->av_frame->ch_layout, &player->src_a_params.ch_layout) ||
        frame->av_frame->sample_rate != player->src_a_params.freq
    ) {
        int ret;
        swr_free(&player->swr_ctx);

        ret = swr_alloc_set_opts2(&player->swr_ctx,
            &player->tgt_a_params.ch_layout,
            player->tgt_a_params.fmt,
            player->tgt_a_params.freq,
            &frame->av_frame->ch_layout,
            frame->av_frame->format,
            frame->av_frame->sample_rate,
            0, NULL);

        if (ret < 0 || swr_init(player->swr_ctx) < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "Cannot create sample rate converter for conversion of "
                   "%d Hz %s %d channels to %d Hz %s %d channels!\n",
                    frame->av_frame->sample_rate,
                    av_get_sample_fmt_name(frame->av_frame->format),
                    frame->av_frame->ch_layout.nb_channels,
                    player->tgt_a_params.freq,
                    av_get_sample_fmt_name(player->tgt_a_params.fmt),
                    player->tgt_a_params.ch_layout.nb_channels);

            swr_free(&player->swr_ctx);
            return -1;
        }

        if (av_channel_layout_copy(&player->src_a_params.ch_layout,
            &frame->av_frame->ch_layout) < 0)
            return -1;
        player->src_a_params.freq = frame->av_frame->sample_rate;
        player->src_a_params.fmt = frame->av_frame->format;
    }

    if (player->swr_ctx) {
        const uint8_t **in = (const uint8_t **) frame->av_frame->extended_data;
        uint8_t **out = &player->audio_buf1;
        int out_count =
            (int64_t) frame->av_frame->nb_samples *
            player->tgt_a_params.freq /
            frame->av_frame->sample_rate +
            256;

        int out_size = av_samples_get_buffer_size(NULL,
            player->tgt_a_params.ch_layout.nb_channels,
            out_count, player->tgt_a_params.fmt, 0);

        int len2;
        if (out_size < 0) {
            av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
            return -1;
        }

        av_fast_malloc(&player->audio_buf1, &player->audio_buf1_size, out_size);
        if (!player->audio_buf1)
            return AVERROR(ENOMEM);

        len2 = swr_convert(player->swr_ctx, out, out_count,
            in, frame->av_frame->nb_samples);
        if (len2 < 0) {
            av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
            return -1;
        }

        if (len2 == out_count) {
            av_log(NULL, AV_LOG_WARNING,
                "audio buffer player probably too small\n");
            if (swr_init(player->swr_ctx) < 0)
                swr_free(&player->swr_ctx);
        }

        player->audio_buf = player->audio_buf1;

        resampled_data_size =
            len2 *
            player->tgt_a_params.ch_layout.nb_channels *
            av_get_bytes_per_sample(player->tgt_a_params.fmt);
    } else {
        player->audio_buf = frame->av_frame->data[0];
        resampled_data_size = data_size;
    }

    audio_clock0 = player->audio_clock;

    /* update the audio clock with the pts */
    if (!isnan(frame->pts)) {
        player->audio_clock =
            frame->pts +
            (double) frame->av_frame->nb_samples /
            frame->av_frame->sample_rate;
    }
    else {
        player->audio_clock = NAN;
    }

    player->audio_clock_serial = frame->serial;
    return resampled_data_size;
}

static void on_process(void *ctx)
{
    MkPlayer *player = ctx;
    struct pw_buffer *buffer;
    struct spa_buffer *buf;
    uint8_t *data;
    int audio_size, len1;

    player->audio_callback_time = av_gettime_relative();

    if ((buffer = pw_stream_dequeue_buffer(player->pw.stream)) == NULL) {
        pw_log_warn("out of buffers: %m");
        return;
    }
    
    buf = buffer->buffer;
    if ((data = buf->datas[0].data) == NULL)
        return;

    uint32_t len = buf->datas[0].maxsize;
    player->audio_hw_buf_size = len;

    while (len > 0) {
        if (player->audio_buf_index >= player->audio_buf_size) {
            audio_size = prepare_frame_data(player);
           if (audio_size < 0) {
                /* if error, just output silence */
               player->audio_buf = NULL;

               player->audio_buf_size = 512 /
                player->tgt_a_params.frame_size *
                player->tgt_a_params.frame_size;
           } else {
               player->audio_buf_size = audio_size;
           }

           player->audio_buf_index = 0;
        }

        len1 = player->audio_buf_size - player->audio_buf_index;
        if (len1 > len)
            len1 = len;

        if (player->audio_buf)
            memcpy(data, (uint8_t *) player->audio_buf + player->audio_buf_index, len1);

        len -= len1;
        data += len1;
        player->audio_buf_index += len1;
    }

    buf->datas[0].chunk->offset = 0;
    buf->datas[0].chunk->stride = player->tgt_a_params.frame_size;
    buf->datas[0].chunk->size = buf->datas[0].maxsize;
    pw_stream_queue_buffer(player->pw.stream, buffer);

    player->audio_write_buf_size = player->audio_buf_size - player->audio_buf_index;

    if (!isnan(player->audio_clock)) {
        set_clock_at(&player->a_clock,
            player->audio_clock -
            (double) (2 * player->audio_hw_buf_size + player->audio_write_buf_size) /
            player->tgt_a_params.bytes_per_sec,
            player->audio_clock_serial,
            player->audio_callback_time / 1000000.0);

        sync_clock_to_slave(&player->ext_clock, &player->a_clock);
    }
}

static void do_quit(void *ctx, int signal_number)
{
        MkPlayer *player = ctx;
        pw_main_loop_quit(player->pw.main_loop);
}

static const struct pw_stream_events stream_events = {
        PW_VERSION_STREAM_EVENTS,
        .process = on_process,
};

void open_audio(MkPlayer *player, AVChannelLayout *wanted_channel_layout,
    int wanted_sample_rate, struct AudioParams *audio_hw_params)
{
    const struct spa_pod *params[1];
    uint32_t n_params = 0;
    uint8_t buffer[1024];
    struct pw_properties *props;
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

    pw_init(NULL, NULL);

    player->pw.main_loop = pw_main_loop_new(NULL);
    player->pw.loop = pw_main_loop_get_loop(player->pw.main_loop);

    pw_loop_add_signal(player->pw.loop, SIGINT, do_quit, player);
    pw_loop_add_signal(player->pw.loop, SIGTERM, do_quit, player);

    props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Playback",
        PW_KEY_MEDIA_ROLE, "Movie",
        NULL);

    player->pw.stream = pw_stream_new_simple(
        player->pw.loop,
        "maikyno-audio",
        props,
        &stream_events,
        player);

    player->tgt_a_params.fmt = AV_SAMPLE_FMT_S32;
    player->tgt_a_params.freq = DEFAULT_RATE;
    av_channel_layout_default(&player->tgt_a_params.ch_layout, DEFAULT_CHANNELS);

    player->tgt_a_params.frame_size = av_samples_get_buffer_size(NULL,
        player->tgt_a_params.ch_layout.nb_channels, 1, player->tgt_a_params.fmt, 1);

    player->tgt_a_params.bytes_per_sec = av_samples_get_buffer_size(NULL,
        player->tgt_a_params.ch_layout.nb_channels, player->tgt_a_params.freq,
        player->tgt_a_params.fmt, 1);

    if (player->tgt_a_params.bytes_per_sec <= 0 || player->tgt_a_params.frame_size <= 0) {
        av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
        return;
    }

    params[n_params++] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat,
        &SPA_AUDIO_INFO_RAW_INIT(
            .format = SPA_AUDIO_FORMAT_S32,
            .channels = DEFAULT_CHANNELS,
            .rate = DEFAULT_RATE));

    pw_stream_connect(player->pw.stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
        PW_STREAM_FLAG_AUTOCONNECT |
        PW_STREAM_FLAG_MAP_BUFFERS |
        PW_STREAM_FLAG_RT_PROCESS,
        params, n_params);
}

void *audio_thread(void *ctx)
{
    MkPlayer *player = ctx;
    AVFrame *av_frame = av_frame_alloc();
    Frame *frame;
    int got_frame = 0;
    int ret = 0;

    if (!av_frame)
        return NULL;

    if (thread_create(&player->pw.main_loop_tid, pw_main_loop_run, player->pw.main_loop) != 0) {
        fprintf(stderr, "Failed to start pw main loop thread.\n");
        return NULL;
    }

    do {
        if ((got_frame = decoder_decode_frame(&player->a_dec, av_frame, NULL)) < 0)
            goto the_end;

        if (got_frame) {
            FrameData *fd = av_frame->opaque_ref ? (FrameData *) av_frame->opaque_ref->data : NULL;

            if (!(frame = frame_queue_peek_writable(&player->a_frame_q)))
                goto the_end;

            frame->pts = (av_frame->pts == AV_NOPTS_VALUE) ? NAN : av_frame->pts * av_q2d(frame->av_frame->time_base);
            frame->pos = fd ? fd->pkt_pos : -1;
            frame->serial = player->a_dec.pkt_serial;
            frame->duration = av_q2d((AVRational) {av_frame->nb_samples, av_frame->sample_rate});

            av_frame_move_ref(frame->av_frame, av_frame);
            frame_queue_push(&player->a_frame_q);

            if (player->a_pkt_q.serial != player->a_dec.pkt_serial)
                break;

            if (ret == AVERROR_EOF)
                player->a_dec.finished = player->a_dec.pkt_serial;
        }
    } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);

 the_end:
    av_frame_free(&av_frame);
    return NULL;
}