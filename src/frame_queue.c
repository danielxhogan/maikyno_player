#include "frame_queue.h"

void frame_queue_unref_item(Frame *frame)
{
    if (!frame)
        return;
    av_frame_unref(frame->av_frame);
    avsubtitle_free(&frame->sub);
}

int frame_queue_init(FrameQueue *frame_q,
    PacketQueue *pkt_q, int keep_last)
{
    int i, ret = 0;
    memset(frame_q, 0, sizeof(FrameQueue));

    if ((ret = mutex_init(&frame_q->mutex)) != 0) {
        fprintf(stderr, "Failed to create mutex. Error: %d\n", ret);
        return ret;
    }
    if ((ret = cond_init(&frame_q->cond)) != 0) {
        fprintf(stderr, "Failed to create cond. Error: %d\n", ret);
        return ret;
    }

    frame_q->pkt_q = pkt_q;
    frame_q->max_size = FFMIN(SAMPLE_QUEUE_SIZE, FRAME_QUEUE_SIZE);
    frame_q->keep_last = !!keep_last;

    for (i = 0; i < frame_q->max_size; i++)
        if (!(frame_q->queue[i].av_frame = av_frame_alloc()))
            return AVERROR(ENOMEM);

    return 0;
}

void frame_queue_destroy(FrameQueue *frame_q)
{
    int i;
    for (i = 0; i < frame_q->max_size; i++) {
        Frame *frame = &frame_q->queue[i];
        frame_queue_unref_item(frame);
        av_frame_free(&frame->av_frame);
    }
    mutex_destroy(&frame_q->mutex);
    cond_destroy(&frame_q->cond);
}

void frame_queue_signal(FrameQueue *frame_q)
{
    mutex_lock(&frame_q->mutex);
    cond_signal(&frame_q->cond);
    mutex_unlock(&frame_q->mutex);
}

Frame *frame_queue_peek(FrameQueue *frame_q)
{
    return &frame_q->queue[
        (frame_q->read_idx + frame_q->read_idx_shown) % frame_q->max_size
    ];
}

Frame *frame_queue_peek_next(FrameQueue *frame_q)
{
    return &frame_q->queue[
        (frame_q->read_idx + frame_q->read_idx_shown + 1) % frame_q->max_size
    ];
}

Frame *frame_queue_peek_last(FrameQueue *frame_q)
{
    return &frame_q->queue[frame_q->read_idx];
}

Frame *frame_queue_peek_writable(FrameQueue *frame_q)
{
    mutex_lock(&frame_q->mutex);
    while (frame_q->size >= frame_q->max_size &&
        !frame_q->pkt_q->abort_request)
    {
        cond_wait(&frame_q->cond, &frame_q->mutex);
    }
    mutex_unlock(&frame_q->mutex);

    if (frame_q->pkt_q->abort_request)
        return NULL;

    return &frame_q->queue[frame_q->write_idx];
}

Frame *frame_queue_peek_readable(FrameQueue *frame_q)
{
    mutex_lock(&frame_q->mutex);
    while (frame_q->size - frame_q->read_idx_shown <= 0 &&
           !frame_q->pkt_q->abort_request)
    {
        cond_wait(&frame_q->cond, &frame_q->mutex);
    }
    mutex_unlock(&frame_q->mutex);

    if (frame_q->pkt_q->abort_request)
        return NULL;

    return &frame_q->queue[
        (frame_q->read_idx + frame_q->read_idx_shown) % frame_q->max_size
    ];
}

void frame_queue_push(FrameQueue *frame_q)
{
    if (++frame_q->write_idx == frame_q->max_size)
        frame_q->write_idx = 0;
    mutex_lock(&frame_q->mutex);
    frame_q->size++;
    cond_signal(&frame_q->cond);
    mutex_unlock(&frame_q->mutex);
}

void frame_queue_next(FrameQueue *frame_q)
{
    if (frame_q->keep_last && !frame_q->read_idx_shown) {
        frame_q->read_idx_shown = 1;
        return;
    }

    frame_queue_unref_item(&frame_q->queue[frame_q->read_idx]);
    if (++frame_q->read_idx == frame_q->max_size)
        frame_q->read_idx = 0;

    mutex_lock(&frame_q->mutex);
    frame_q->size--;
    cond_signal(&frame_q->cond);
    mutex_unlock(&frame_q->mutex);
}

int frame_queue_nb_remaining(FrameQueue *frame_q)
{
    return frame_q->size - frame_q->read_idx_shown;
}

int64_t frame_queue_last_pos(FrameQueue *frame_q)
{
    Frame *frame = &frame_q->queue[frame_q->read_idx];
    if (frame_q->read_idx_shown && frame->serial == frame_q->pkt_q->serial)
        return frame->pos;
    else
        return -1;
}
