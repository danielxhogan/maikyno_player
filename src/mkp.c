#include "./includes/libmkp/mkplayer.h"
#include "mkp.h"
#include "read_thread.h"

#include <malloc.h>

static int initialize_player(MkPlayer **player, char *src)
{
    *player = malloc(sizeof(MkPlayer));
    if (!player)
        return -ENOMEM;

    (*player)->fmt_ctx = NULL;
    (*player)->eof = 0;
    (*player)->src = strdup(src);

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
