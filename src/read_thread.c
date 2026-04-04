#include "mkp.h"
#include "packet_queue.h"

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

void open_audio(MkPlayer *player, AVChannelLayout *wanted_channel_layout,
    int wanted_sample_rate, struct AudioParams *audio_hw_params);

int decoder_init(Decoder *dec, AVCodecContext *dec_ctx, PacketQueue *pkt_q, Cond empty_queue_cond);
int decoder_start(Decoder *dec, void *(*fn)(void *), const char *thread_name, void* ctx);
void *audio_thread(void *ctx);

static int decode_interrupt_cb(void *ctx)
{
    MkPlayer *player = ctx;
    return player->abort_request;
}

static int stream_has_enough_packets(AVStream *stream,
    int stream_id, PacketQueue *queue)
{
    return stream_id < 0 ||
        queue->abort_request ||
        (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
        (queue->nb_packets > MIN_FRAMES &&
        (!queue->duration || av_q2d(stream->time_base) * queue->duration > 1.0));
}

static int open_stream(MkPlayer *player, int stream_idx)
{
    int ret = 0;
    AVFormatContext *fmt_ctx = player->fmt_ctx;
    AVCodecContext *dec_ctx = NULL;
    const AVCodec *dec;

    dec_ctx = avcodec_alloc_context3(NULL);
    if (!dec_ctx)
        return AVERROR(ENOMEM);

    ret = avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[stream_idx]->codecpar);
    if (ret < 0)
        goto fail;

    dec = avcodec_find_decoder(dec_ctx->codec_id);
    switch(dec_ctx->codec_type) {
        // case AVMEDIA_TYPE_AUDIO   : player->last_v_stream_idx    = stream_idx; break;
        case AVMEDIA_TYPE_AUDIO: player->last_a_stream_idx = stream_idx; break;
        // case AVMEDIA_TYPE_AUDIO   : player->last_s_stream_idx    = stream_idx; break;
        default: break;
    }

    if (!dec) {
        fprintf(stderr, "Failed to find decoder for codec %s.\n", avcodec_get_name(dec_ctx->codec_id));
        ret = AVERROR(EINVAL);
        goto fail;
    }

    ret = avcodec_open2(dec_ctx, dec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Failed to open decoder context.\n"
            "Libav Error: %s\n", av_err2str(ret));
        goto fail;
    }

    player->eof = 0;
    fmt_ctx->streams[stream_idx]->discard = AVDISCARD_DEFAULT;

    switch (dec_ctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO: {
        open_audio(player, &dec_ctx->ch_layout,
            dec_ctx->sample_rate, &player->tgt_a_params);
    
        player->src_a_params = player->tgt_a_params;
        player->audio_buf_size  = 0;
        player->audio_buf_index = 0;
        player->a_stream_idx = stream_idx;
        player->av_a_stream = fmt_ctx->streams[stream_idx];

        // if ((ret = decoder_init(&player->a_dec, dec_ctx, &player->a_pkt_q, player->threads.continue_read_thread)) < 0)
        if ((ret = decoder_init(&player->a_dec, dec_ctx, &player->a_pkt_q, player->continue_read_thread)) < 0)
            goto fail;
        if ((ret = decoder_start(&player->a_dec, audio_thread, "audio_decoder", player)) < 0)
            goto out;

        break;
    }
    default: break;
    }

    goto out;

fail:
    avcodec_free_context(&dec_ctx);
out:
    return ret;
}

void *read_thread(void *ctx)
{
    int ret = 0;
    MkPlayer *player = ctx;
    AVFormatContext *fmt_ctx = NULL;
    AVPacket *pkt = NULL;
    AVDictionary *fmt_opts;
    // int scan_all_pmts_set = 0;
    int stream_idxs[AVMEDIA_TYPE_NB];
    Mutex wait_mutex;

    ret = mutex_init(&wait_mutex);
    if (ret != 0) {
        fprintf(stderr, "Failed to init wait_mutex. Error: %d\n", ret);
        goto end;
    }

    // cond_init(&player->threads.continue_read_thread);
    ret = cond_init(&player->continue_read_thread);
    if (ret != 0) {
        fprintf(stderr, "Failed to init continue_read_thread. Error: %d\n", ret);
        goto end;
    }

    memset(stream_idxs, -1, sizeof(stream_idxs));

    pkt = av_packet_alloc();
    if (!pkt)
        goto end;

    fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate context.\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    fmt_ctx->interrupt_callback.callback = decode_interrupt_cb;
    fmt_ctx->interrupt_callback.opaque = player;
    ret = avformat_open_input(&fmt_ctx, player->src, NULL, &fmt_opts);
    if (ret < 0) {
        fprintf(stderr, "Failed to open input.\nLibav Error: %s.\n", av_err2str(ret));
        goto end;
    }
    player->fmt_ctx = fmt_ctx;

    ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Failed to find stream info.\n"
            "Libav Error: %s.\n", av_err2str(ret));
        goto end;
    }

    if (fmt_ctx->pb)
        fmt_ctx->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end
    player->max_frame_duration = (fmt_ctx->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;

    stream_idxs[AVMEDIA_TYPE_AUDIO] = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (stream_idxs[AVMEDIA_TYPE_AUDIO] < 0) {
        fprintf(stderr, "Failed to find a valid audio stream.\n"
            "Libav Error: %s.\n", av_err2str(ret));
        goto end;
    }

    open_stream(player, stream_idxs[AVMEDIA_TYPE_AUDIO]);
    if (player->a_stream_idx < 0) {
        fprintf(stderr, "Failed to open audio stream.\n");
        goto end;
    }

    for (;;) {
        if (player->abort_request)
            break;

        // if ((player->a_pkt_q->size + player->v_pkt_q->size + player->s_pkt_q->size > MAX_QUEUE_SIZE
        //     || (stream_has_enough_packets(player->a_stream, player->a_stream_idx, &player->a_pkt_q) &&
        //         stream_has_enough_packets(player->v_stream, player->v_stream_idx, &player->v_pkt_q) &&
        //         stream_has_enough_packets(player->s_stream, player->s_stream_idx, &player->s_pkt_q)))) {
        //     /* wait 10 ms */
        //     mutex_lock(&wait_mutex);
        //     cond_timedwait(&player->continue_read_thread, &wait_mutex, 10000000);
        //     mutex_unlock(&wait_mutex);
        //     continue;
        // }

        if (player->a_pkt_q.size > MAX_QUEUE_SIZE ||
            stream_has_enough_packets(player->av_a_stream, player->a_stream_idx, &player->a_pkt_q)
        ) {
            /* wait 10 ms */
            mutex_lock(&wait_mutex);
            cond_timedwait(&player->continue_read_thread, &wait_mutex, 10000000);
            mutex_unlock(&wait_mutex);
            continue;
        }

        ret = av_read_frame(fmt_ctx, pkt);
        if (ret < 0) {
            if ((ret == AVERROR_EOF || avio_feof(fmt_ctx->pb)) && !player->eof) {
                packet_queue_put_nullpacket(&player->a_pkt_q, pkt, player->a_stream_idx);
                player->eof = 1;
            }
            if (fmt_ctx->pb && fmt_ctx->pb->error)
                break;
            mutex_lock(&wait_mutex);
            cond_timedwait(&player->continue_read_thread, &wait_mutex, 10000000);
            mutex_unlock(&wait_mutex);
            continue;
        } else {
            player->eof = 0;
        }

        if (pkt->stream_index == player->a_stream_idx) {
            packet_queue_put(&player->a_pkt_q, pkt);
        } else {
            av_packet_unref(pkt);
        }
    }

end:
    if (fmt_ctx && !player->fmt_ctx)
        avformat_close_input(&fmt_ctx);
    av_packet_free(&pkt);
    mutex_destroy(&wait_mutex);
    return NULL;
}
