#pragma once

typedef struct MkPlayer MkPlayer;

MkPlayer *mkp_create_player(char *src);
void mkp_stop_player(MkPlayer **mkp);
