#include "./includes/libmkp/mkplayer.h"
#include "mkp.h"
#include "demux.h"

#include <malloc.h>

static int initialize_player(MkPlayer **player, char *src)
{
    *player = malloc(sizeof(MkPlayer));
    if (!player)
        return -ENOMEM;

    (*player)->av_fmt = NULL;
    (*player)->eof = 0;
    (*player)->src = strdup(src);

    return 0;
}

static void set_initial_stream(MkPlayer *player,
    enum AVMediaType media_type, int initial_stream_idx)
{
    int *stream_idx;
    int *last_stream_idx;
    // AVStream **av_stream;

    switch (media_type) {
    case AVMEDIA_TYPE_AUDIO:
        stream_idx = &player->a_player->stream_idx;
        last_stream_idx = &player->a_player->last_stream_idx;
        // av_stream = &player->a_player.av_stream;
        break;
    default:
        printf("set_initial_stream: SHOULD NOT REACH!!\n");
        break;
    }

    if (initial_stream_idx < 0 ||
        initial_stream_idx >= player->av_fmt->nb_streams ||
        player->av_fmt->streams[initial_stream_idx]->codecpar->codec_type
            != media_type
    ) {
        initial_stream_idx = av_find_best_stream(player->av_fmt,
            media_type, -1, -1, NULL, 0);
    }

    *stream_idx = initial_stream_idx;
    *last_stream_idx = initial_stream_idx;
    player->av_fmt->streams[*stream_idx]->discard = AVDISCARD_DEFAULT;
    // *av_stream = player->av_fmt->streams[*stream_idx];
}

MkPlayer *mkp_create_player(char *src, int initial_a_stream_idx)
{
    MkPlayer *player = NULL;

    if (initialize_player(&player, src) != 0)
        goto end;

    if (initialize_demuxer(player) != 0)
        goto end;

    player->a_player = create_audio_player();
    if (!player->a_player)
        goto end;

    set_initial_stream(player, AVMEDIA_TYPE_AUDIO, initial_a_stream_idx);

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
    avformat_close_input(&(*player)->av_fmt);
    avcodec_free_context(&(*player)->a_player->dec.av_dec);
    free(*player);
    *player = NULL;
}
