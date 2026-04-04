#include "packet_queue.h"
#include <libavutil/avutil.h>

int packet_queue_put_private(PacketQueue *pkt_q, AVPacket *av_pkt)
{
    Packet pkt;
    int ret;

    if (pkt_q->abort_request)
       return -1;

    pkt.av_pkt = av_pkt;
    pkt.serial = pkt_q->serial;

    ret = av_fifo_write(pkt_q->pkt_list, &pkt, 1);
    if (ret < 0)
        return ret;
    pkt_q->nb_packets++;
    pkt_q->size += pkt.av_pkt->size + sizeof(pkt);
    pkt_q->duration += pkt.av_pkt->duration;
    cond_signal(&pkt_q->cond);
    return 0;
}

int packet_queue_put(PacketQueue *pkt_q, AVPacket *av_pkt)
{
    AVPacket *pkt;
    int ret = 0;

    pkt = av_packet_alloc();
    if (!pkt) {
        av_packet_unref(av_pkt);
        return -1;
    }
    av_packet_move_ref(pkt, av_pkt);

    mutex_lock(&pkt_q->mutex);
    ret = packet_queue_put_private(pkt_q, pkt);
    mutex_unlock(&pkt_q->mutex);

    if (ret < 0)
        av_packet_free(&pkt);

    return ret;
}

int packet_queue_put_nullpacket(PacketQueue *pkt_q,
    AVPacket *av_pkt, int stream_index)
{
    av_pkt->stream_index = stream_index;
    return packet_queue_put(pkt_q, av_pkt);
}

int packet_queue_init(PacketQueue *pkt_q)
{
    int ret = 0;

    memset(pkt_q, 0, sizeof(PacketQueue));
    pkt_q->pkt_list = av_fifo_alloc2(1, sizeof(Packet),
    AV_FIFO_FLAG_AUTO_GROW);
    if (!pkt_q->pkt_list)
        return -ENOMEM;

    if ((ret = mutex_init(&pkt_q->mutex)) != 0) {
        fprintf(stderr, "Failed to create mutex. Error: %d\n", ret);
        return ret;
    }
    if ((ret = cond_init(&pkt_q->cond)) != 0) {
        fprintf(stderr, "Failed to create cond. Error: %d\n", ret);
        return ret;
    }

    pkt_q->abort_request = 1;
    return 0;
}

void packet_queue_flush(PacketQueue *pkt_q)
{
    Packet pkt;

    mutex_lock(&pkt_q->mutex);
    while (av_fifo_read(pkt_q->pkt_list, &pkt, 1) >= 0)
        av_packet_free(&pkt.av_pkt);
    pkt_q->nb_packets = 0;
    pkt_q->size = 0;
    pkt_q->duration = 0;
    pkt_q->serial++;
    mutex_unlock(&pkt_q->mutex);
}

void packet_queue_destroy(PacketQueue *pkt_q)
{
    packet_queue_flush(pkt_q);
    av_fifo_freep2(&pkt_q->pkt_list);
    mutex_destroy(&pkt_q->mutex);
    cond_destroy(&pkt_q->cond);
}

void packet_queue_abort(PacketQueue *pkt_q)
{
    mutex_lock(&pkt_q->mutex);
    pkt_q->abort_request = 1;
    cond_signal(&pkt_q->cond);
    mutex_unlock(&pkt_q->mutex);
}

void packet_queue_start(PacketQueue *pkt_q)
{
    mutex_lock(&pkt_q->mutex);
    pkt_q->abort_request = 0;
    pkt_q->serial++;
    mutex_unlock(&pkt_q->mutex);
}

/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
int packet_queue_get(PacketQueue *pkt_q,
    AVPacket *av_pkt, int block, int *serial)
{
    Packet pkt;
    int ret = 0;

    mutex_lock(&pkt_q->mutex);

    for (;;) {
        if (pkt_q->abort_request) {
            ret = -1;
            break;
        }

        if (av_fifo_read(pkt_q->pkt_list, &pkt, 1) >= 0) {
            pkt_q->nb_packets--;
            pkt_q->size -= pkt.av_pkt->size + sizeof(pkt);
            pkt_q->duration -= pkt.av_pkt->duration;
            av_packet_move_ref(av_pkt, pkt.av_pkt);
            if (serial)
                *serial = pkt.serial;
            av_packet_free(&pkt.av_pkt);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            cond_wait(&pkt_q->cond, &pkt_q->mutex);
        }
    }

    mutex_unlock(&pkt_q->mutex);
    return ret;
}
