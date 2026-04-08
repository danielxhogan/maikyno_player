#include "demux.h"
#include "mkp.h"
#include "packet_queue.h"

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

static int decode_interrupt_cb(void *ctx)
{
    MkPlayer *player = ctx;
    return player->abort_request;
}

int initialize_demuxer(MkPlayer *player)
{
    int ret = 0;

    player->av_fmt = avformat_alloc_context();
    if (!player->av_fmt) {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate context.\n");
        return AVERROR(ENOMEM);
    }

    player->av_fmt->interrupt_callback.callback = decode_interrupt_cb;
    player->av_fmt->interrupt_callback.opaque = player;

    ret = avformat_open_input(&player->av_fmt, player->src, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "Failed to open input.\n"
            "Libav Error: %s.\n", av_err2str(ret));
        return ret;
    }

    ret = avformat_find_stream_info(player->av_fmt, NULL);
    if (ret < 0) {
        fprintf(stderr, "Failed to find stream info.\n"
            "Libav Error: %s.\n", av_err2str(ret));
        return ret;
    }

    if (player->av_fmt->pb)
        player->av_fmt->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end

    player->max_frame_duration =
        (player->av_fmt->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;

    return 0;
}

static int stream_has_enough_packets(AVStream *stream,
    int stream_id, PacketQueue *queue)
{
    return stream_id < 0 ||
        queue->abort_request ||
        (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
        (queue->nb_packets > MIN_FRAMES &&
        (!queue->duration || av_q2d(stream->time_base) * queue->duration > 1.0));
}

void *start_demuxer(void *ctx)
{
    int ret = 0;
    MkPlayer *player = ctx;
    AVPacket *pkt = NULL;
    Mutex wait_mutex;

    ret = mutex_init(&wait_mutex);
    if (ret != 0) {
        fprintf(stderr, "Failed to init wait_mutex. Error: %d\n", ret);
        goto end;
    }

    if (cond_init(&player->need_pkts) != 0) {
        fprintf(stderr, "Failed to init continue_read_thread. Error: %d\n", ret);
    }

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

        if (player->a_player->dec.pkt_q.size + player->v_renderer.dec.pkt_q.size > MAX_QUEUE_SIZE ||
            (stream_has_enough_packets(
                player->av_fmt->streams[player->a_player->stream_idx],
                player->a_player->stream_idx,
                &player->a_player->dec.pkt_q) &&
            stream_has_enough_packets(
                player->av_fmt->streams[player->v_renderer.stream_idx],
                player->v_renderer.stream_idx,
                &player->v_renderer.dec.pkt_q))
        ) {
            /* wait 10 ms */
            mutex_lock(&wait_mutex);
            cond_timedwait(&player->need_pkts, &wait_mutex, 10000000);
            mutex_unlock(&wait_mutex);
            continue;
        }

        ret = av_read_frame(player->av_fmt, pkt);
        if (ret < 0) {
            if ((ret == AVERROR_EOF || avio_feof(player->av_fmt->pb)) && !player->eof) {
                packet_queue_put_nullpacket(&player->a_player->dec.pkt_q, pkt, player->a_player->stream_idx);
                player->eof = 1;
            }
            if (player->av_fmt->pb && player->av_fmt->pb->error)
                break;
            mutex_lock(&wait_mutex);
            cond_timedwait(&player->need_pkts, &wait_mutex, 10000000);
            mutex_unlock(&wait_mutex);
            continue;
        } else {
            player->eof = 0;
        }

        if (pkt->stream_index == player->a_player->stream_idx) {
            packet_queue_put(&player->a_player->dec.pkt_q, pkt);
        } else if (pkt->stream_index == player->v_renderer.stream_idx) {
            packet_queue_put(&player->v_renderer.dec.pkt_q, pkt);
        } else {
            av_packet_unref(pkt);
        }
    }

end:
    av_packet_free(&pkt);
    mutex_destroy(&wait_mutex);
    return NULL;
}
