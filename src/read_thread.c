#include "mkp.h"
#include "packet_queue.h"

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

static int stream_has_enough_packets(AVStream *stream,
    int stream_id, PacketQueue *queue)
{
    return stream_id < 0 ||
        queue->abort_request ||
        (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
        (queue->nb_packets > MIN_FRAMES &&
        (!queue->duration || av_q2d(stream->time_base) * queue->duration > 1.0));
}

void *read_thread(void *ctx)
{
    int ret = 0;
    MkPlayer *player = ctx;
    AVPacket *pkt = NULL;
    int stream_idxs[AVMEDIA_TYPE_NB];
    Mutex wait_mutex;

    ret = mutex_init(&wait_mutex);
    if (ret != 0) {
        fprintf(stderr, "Failed to init wait_mutex. Error: %d\n", ret);
        goto end;
    }

    memset(stream_idxs, -1, sizeof(stream_idxs));

    pkt = av_packet_alloc();
    if (!pkt)
        goto end;

    for (;;) {
        if (player->abort_request)
            break;

        // if ((player->a_pkt_q->size + player->v_pkt_q->size + player->s_pkt_q->size > MAX_QUEUE_SIZE
        //     || (stream_has_enough_packets(player->a_stream, player->a_stream_idx, &player->a_pkt_q) &&
        //         stream_has_enough_packets(player->v_stream, player->v_stream_idx, &player->v_pkt_q) &&
        //         stream_has_enough_packets(player->s_stream, player->s_stream_idx, &player->s_pkt_q)))) {
        //     /* wait 10 ms */
        //     mutex_lock(&wait_mutex);
        //     cond_timedwait(&player->continue_read_thread, &wait_mutex, 10000000);
        //     mutex_unlock(&wait_mutex);
        //     continue;
        // }

        if (player->a_pkt_q.size > MAX_QUEUE_SIZE ||
            stream_has_enough_packets(player->av_a_stream, player->a_stream_idx, &player->a_pkt_q)
        ) {
            /* wait 10 ms */
            mutex_lock(&wait_mutex);
            cond_timedwait(&player->continue_read_thread, &wait_mutex, 10000000);
            mutex_unlock(&wait_mutex);
            continue;
        }

        ret = av_read_frame(player->fmt_ctx, pkt);
        if (ret < 0) {
            if ((ret == AVERROR_EOF || avio_feof(player->fmt_ctx->pb)) && !player->eof) {
                packet_queue_put_nullpacket(&player->a_pkt_q, pkt, player->a_stream_idx);
                player->eof = 1;
            }
            if (player->fmt_ctx->pb && player->fmt_ctx->pb->error)
                break;
            mutex_lock(&wait_mutex);
            cond_timedwait(&player->continue_read_thread, &wait_mutex, 10000000);
            mutex_unlock(&wait_mutex);
            continue;
        } else {
            player->eof = 0;
        }

        if (pkt->stream_index == player->a_stream_idx) {
            packet_queue_put(&player->a_pkt_q, pkt);
        } else {
            av_packet_unref(pkt);
        }
    }

end:
    av_packet_free(&pkt);
    mutex_destroy(&wait_mutex);
    return NULL;
}
