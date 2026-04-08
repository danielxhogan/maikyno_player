#include "./includes/libmkp/mkplayer_pl.h"
#include "mkp.h"
#include "demux.h"

#include <libplacebo/vulkan.h>
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
        if (media_type != AVMEDIA_TYPE_SUBTITLE) {
            return av_find_best_stream(player->av_fmt,
                media_type, -1, -1, NULL, 0);
        } else {
            return -1;
        }
    }

    return initial_stream_idx;
}

MkPlayer *mkp_create_player(char *src,
    int initial_v_stream_idx, int initial_a_stream_idx)
{
    return NULL;
}

MkPlayer *mkp_create_player_from_pl_vulkan(char *src,
    int initial_v_stream_idx, int initial_a_stream_idx, pl_vulkan vk)
{
    MkPlayer *player = NULL;

    if (initialize_player(&player, src) != 0)
        goto end;

    if (initialize_demuxer(player) != 0)
        goto end;

    initial_v_stream_idx =
        get_initial_stream_idx(player, AVMEDIA_TYPE_VIDEO, initial_v_stream_idx);
    if (initial_v_stream_idx < 0) {
        fprintf(stderr, "No valid video stream found in input file.\n");
        goto end;
    }
    player->v_renderer.stream = player->av_fmt->streams[initial_v_stream_idx];
    player->v_renderer.stream->discard = AVDISCARD_DEFAULT;

    initial_a_stream_idx =
        get_initial_stream_idx(player, AVMEDIA_TYPE_AUDIO, initial_a_stream_idx);
    if (initial_a_stream_idx < 0) {
        fprintf(stderr, "No valid audio stream found in input file.\n");
        goto end;
    }
    player->av_fmt->streams[initial_a_stream_idx]->discard = AVDISCARD_DEFAULT;

    if (create_video_renderer(&player->v_renderer,
        vk, initial_v_stream_idx) < 0)
    {
        fprintf(stderr, "Failed to create video renderer.\n");
        goto end;
    }

    player->a_player = create_audio_player(initial_a_stream_idx);
    if (!player->a_player)
        goto end;

    if (initialize_decoder(&player->v_renderer.dec,
        player->av_fmt->streams[player->v_renderer.stream_idx]->codecpar,
        player->need_pkts) != 0)
        goto end;

    if (initialize_decoder(&player->a_player->dec,
        player->av_fmt->streams[player->a_player->stream_idx]->codecpar,
        player->need_pkts) != 0)
        goto end;

    if (thread_create(&player->demux_tid, start_demuxer, player) != 0)
        goto end;

    if (thread_create(&player->v_renderer.dec.tid,
        start_video_decoder, &player->v_renderer) != 0)
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
