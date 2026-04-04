#include "./includes/libmkp/mkplayer.h"
#include "mkp.h"
#include "frame_queue.h"
#include "thread/thread.h"
#include "clock.h"

#include <malloc.h>

int initialize_decoder(Decoder *dec, AVCodecParameters *codecpar,
    PacketQueue *pkt_q, Cond empty_queue_cond);
void initialize_audio_output(MkPlayer *player);
void *read_thread(void *ctx);
void *audio_thread(void *ctx);

static int initialize_player(MkPlayer **player, char *src)
{
    int ret = 0;

    *player = malloc(sizeof(MkPlayer));
    if (!player)
        return -1;

    (*player)->fmt_ctx = NULL;
    (*player)->eof = 0;
    (*player)->src = strdup(src);

    if (frame_queue_init(&(*player)->a_frame_q, &(*player)->a_pkt_q, 1) != 0)
        return -1;
    if (packet_queue_init(&(*player)->a_pkt_q) != 0)
        return -1;
    init_clock(&(*player)->a_clock, &(*player)->a_pkt_q.serial);

    ret = cond_init(&(*player)->continue_read_thread);
    if (ret != 0) {
        fprintf(stderr, "Failed to init continue_read_thread. Error: %d\n", ret);
        return ret;
    }

    return 0;
}

static int decode_interrupt_cb(void *ctx)
{
    MkPlayer *player = ctx;
    return player->abort_request;
}

static int initialize_demuxer(MkPlayer *player)
{
    int ret = 0;

    player->fmt_ctx = avformat_alloc_context();
    if (!player->fmt_ctx) {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate context.\n");
        return AVERROR(ENOMEM);
    }

    player->fmt_ctx->interrupt_callback.callback = decode_interrupt_cb;
    player->fmt_ctx->interrupt_callback.opaque = player;

    ret = avformat_open_input(&player->fmt_ctx, player->src, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "Failed to open input.\n"
            "Libav Error: %s.\n", av_err2str(ret));
        return ret;
    }

    ret = avformat_find_stream_info(player->fmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Failed to find stream info.\n"
            "Libav Error: %s.\n", av_err2str(ret));
        return ret;
    }

    if (player->fmt_ctx->pb)
        player->fmt_ctx->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end
    player->max_frame_duration =
        (player->fmt_ctx->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;

    return 0;
}

static void set_initial_stream(MkPlayer *player,
    enum AVMediaType media_type, int initial_stream_idx)
{
    int *stream_idx;
    int *last_stream_idx;
    AVStream **av_stream;

    switch (media_type) {
    case AVMEDIA_TYPE_AUDIO:
        stream_idx = &player->a_stream_idx;
        last_stream_idx = &player->last_a_stream_idx;
        av_stream = &player->av_a_stream;
        break;
    default:
        printf("SHOULD NOT REACH!!\n");
        break;
    }

    if (initial_stream_idx < 0 ||
        initial_stream_idx >= player->fmt_ctx->nb_streams ||
        player->fmt_ctx->streams[initial_stream_idx]->codecpar->codec_type
            != media_type
    ) {
        initial_stream_idx = av_find_best_stream(player->fmt_ctx,
            media_type, -1, -1, NULL, 0);
    }


    *stream_idx = initial_stream_idx;
    *last_stream_idx = initial_stream_idx;
    player->fmt_ctx->streams[*stream_idx]->discard = AVDISCARD_DEFAULT;
    *av_stream = player->fmt_ctx->streams[*stream_idx];
}

MkPlayer *mkp_create_player(char *src, int initial_a_stream_idx)
{
    MkPlayer *player = NULL;

    if (initialize_player(&player, src) != 0)
        goto end;

    if (initialize_demuxer(player) != 0)
        goto end;

    set_initial_stream(player, AVMEDIA_TYPE_AUDIO, initial_a_stream_idx);

    if (initialize_decoder(&player->a_dec,
        player->fmt_ctx->streams[player->a_stream_idx]->codecpar,
        &player->a_pkt_q, player->continue_read_thread) != 0)
        goto end;

    initialize_audio_output(player);

    if (thread_create(&player->read_tid, read_thread, player) != 0)
        goto end;

    if (thread_create(&player->a_dec.decoder_tid, audio_thread, player) != 0)
        goto end;

    return player;

end:
    mkp_destroy_player(&player);
    return NULL;
}

void mkp_destroy_player(MkPlayer **player)
{
    if (!player || !*player)
        return;
    avformat_close_input(&(*player)->fmt_ctx);
    avcodec_free_context(&(*player)->a_dec.dec_ctx);
    free(*player);
    *player = NULL;
}
