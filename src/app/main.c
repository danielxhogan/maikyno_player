#include "../includes/libmkp/mkplayer.h"
#include <malloc.h>

typedef struct App {
    MkPlayer *player;
    char *src;
} App;

int main(int argc, char **argv)
{
    App *app = malloc(sizeof(App));
    app->src = "http://192.168.1.209:8080/media/tha_movies/mk_movies_2/collections/The Fast & The Furious Series/01 - The Fast And The Furious/The Fast And The Furious.mkv";
    app->player = mkp_create_player(app->src);
    if (!app->player)
        return -1;

    #include <unistd.h>
    sleep(500); // Suspends execution for 5 seconds

    mkp_stop_player(&app->player);
    #include <stdio.h>
    printf("hi\n");
    return 0;
}
