#pragma once

typedef struct MkPlayer MkPlayer;

MkPlayer *mkp_create_player(char *src,
    int initial_v_stream_idx, int initial_a_stream_idx,
    void (* render_cb) (void *ctx), void *render_cb_ctx);
int mkp_render_from_fbo(MkPlayer *player, unsigned int fbo, int width, int height);
void mkp_destroy_player(MkPlayer **mkp);
