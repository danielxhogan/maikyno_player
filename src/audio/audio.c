#include "audio.h"

extern const struct AudioPlayerBackend pw_backend;

const struct AudioPlayerBackend *a_backend = &pw_backend;

AudioPlayer *create_audio_player()
{
    return a_backend->create();
}

void *start_audio_player(void *ctx)
{
    AudioPlayer *a_player = ctx;
    a_player->backend->start(a_player);
    return NULL;
}
