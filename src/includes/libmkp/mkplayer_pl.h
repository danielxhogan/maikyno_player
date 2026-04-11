#pragma once

#include "mkplayer.h"

#include <libplacebo/renderer.h>
#include <libplacebo/vulkan.h>

MkPlayer *mkp_create_player_from_pl_vulkan(char *src,
    int initial_v_stream_idx, int initial_a_stream_idx,
    void (* render_cb) (void *ctx), void *render_cb_ctx, pl_vulkan vk);
int mkp_render_from_pl_frame(MkPlayer *player, struct pl_frame *frame);
