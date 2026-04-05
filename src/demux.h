#pragma once

#include "./includes/libmkp/mkplayer.h"

int initialize_demuxer(MkPlayer *player);
void *demux_thread(void *ctx);