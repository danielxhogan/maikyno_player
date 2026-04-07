#include "./includes/libmkp/mkplayer.h"
#include "mkp.h"
#include "demux.h"

#include <malloc.h>

static int initialize_player(MkPlayer **player, char *src)
{
    *player = calloc(1, sizeof(MkPlayer));
    if (!player)
        return -ENOMEM;

    (*player)->src = strdup(src);

    return 0;
}

static int get_initial_stream_idx(MkPlayer *player,
    enum AVMediaType media_type, int initial_stream_idx)
{
    if (initial_stream_idx < 0 ||
        initial_stream_idx >= player->av_fmt->nb_streams ||
        player->av_fmt->streams[initial_stream_idx]->codecpar->codec_type
            != media_type
    ) {
        initial_stream_idx = av_find_best_stream(player->av_fmt,
            media_type, -1, -1, NULL, 0);
    }

    return initial_stream_idx;
}

MkPlayer *mkp_create_player(char *src, int initial_a_stream_idx)
{
    MkPlayer *player = NULL;

    if (initialize_player(&player, src) != 0)
        goto end;

    if (initialize_demuxer(player) != 0)
        goto end;

    initial_a_stream_idx =
        get_initial_stream_idx(player, AVMEDIA_TYPE_AUDIO, initial_a_stream_idx);
    player->av_fmt->streams[initial_a_stream_idx]->discard = AVDISCARD_DEFAULT;

    player->a_player = create_audio_player(initial_a_stream_idx);
    if (!player->a_player)
        goto end;

    if (initialize_decoder(&player->a_player->dec,
        player->av_fmt->streams[player->a_player->stream_idx]->codecpar,
        player->need_pkts) != 0)
        goto end;

    if (thread_create(&player->demux_tid, start_demuxer, player) != 0)
        goto end;

    if (thread_create(&player->a_player->dec.tid,
        start_audio_player, player->a_player) != 0)
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

    (*player)->abort_request = 1;
    thread_join((*player)->demux_tid);

    if ((*player)->a_player) {
        stop_audio_player((*player)->a_player);
        destroy_audio_player(&(*player)->a_player);
    }

    avformat_close_input(&(*player)->av_fmt);

    free(*player);
    *player = NULL;
}
