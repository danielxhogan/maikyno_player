#pragma once

#include "./includes/libmkp/mkplayer.h"

#define MAX_QUEUE_SIZE (15 * 1024 * 1024)
#define MIN_FRAMES 25

int initialize_demuxer(MkPlayer *player);
void *start_demuxer(void *ctx);
