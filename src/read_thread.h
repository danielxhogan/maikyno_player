#pragma once

#include "./includes/libmkp/mkplayer.h"

int initialize_demuxer(MkPlayer *player);
void *read_thread(void *ctx);