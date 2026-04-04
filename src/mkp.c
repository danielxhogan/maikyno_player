#include "./includes/libmkp/mkplayer.h"
#include "mkp.h"
#include "frame_queue.h"
#include "thread/thread.h"
#include "clock.h"

#include <malloc.h>

void *read_thread(void *ctx);

MkPlayer *mkp_create_player(char *src)
{
    MkPlayer *player = malloc(sizeof(MkPlayer));
    if (!player)
        return NULL;

    player->src = strdup(src);
    if (frame_queue_init(&player->a_frame_q, &player->a_pkt_q, 1) != 0)
        goto end;
    if (packet_queue_init(&player->a_pkt_q) != 0)
        goto end;
    init_clock(&player->a_clock, &player->a_pkt_q.serial);
    init_clock(&player->ext_clock, &player->ext_clock.serial);

    if (thread_create(&player->read_tid, read_thread, player) != 0)
        goto end;

    return player;

end:
    mkp_stop_player(&player);
    return NULL;
}

void mkp_stop_player(MkPlayer **player)
{
    if (!player || !*player)
        return;
    free(*player);
    *player = NULL;
}
