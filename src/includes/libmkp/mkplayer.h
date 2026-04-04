#pragma once

typedef struct MkPlayer MkPlayer;

MkPlayer *mkp_create_player(char *src, int initial_a_stream_idx);
void mkp_destroy_player(MkPlayer **mkp);
