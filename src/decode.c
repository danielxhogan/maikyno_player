#include "mkp.h"
#include "thread/thread.h"

int decoder_init(Decoder *dec, AVCodecContext *dec_ctx, PacketQueue *pkt_q, Cond empty_queue_cond)
{
    memset(dec, 0, sizeof(Decoder));
    dec->pkt = av_packet_alloc();
    if (!dec->pkt)
        return AVERROR(ENOMEM);
    dec->dec_ctx = dec_ctx;
    dec->pkt_q = pkt_q;
    dec->empty_queue_cond = empty_queue_cond;
    dec->start_pts = AV_NOPTS_VALUE;
    dec->pkt_serial = -1;
    return 0;
}

void decoder_destroy(Decoder *dec) {
    av_packet_free(&dec->pkt);
    avcodec_free_context(&dec->dec_ctx);
}

int decoder_start(Decoder *dec, void *(*fn)(void *), const char *thread_name, void* ctx)
{
    packet_queue_start(dec->pkt_q);

    if (thread_create(&dec->decoder_tid, fn, ctx) != 0) {
        fprintf(stderr, "Failed to create thread.\n");
        return AVERROR(ENOMEM);
    }

    return 0;
}

int i = 0;
int decoder_decode_frame(Decoder *dec, AVFrame *frame, AVSubtitle *sub)
{
    int ret = AVERROR(EAGAIN);

    for (;;) {
        if (dec->pkt_q->serial == dec->pkt_serial) {
            do {
                if (dec->pkt_q->abort_request)
                    return -1;

                switch (dec->dec_ctx->codec_type) {
                case AVMEDIA_TYPE_VIDEO: {
                    ret = avcodec_receive_frame(dec->dec_ctx, frame);
                    if (ret >= 0) {
                        frame->pts = frame->best_effort_timestamp;
                    }
                    break;
                }
                case AVMEDIA_TYPE_AUDIO: {
                    ret = avcodec_receive_frame(dec->dec_ctx, frame);
                    if (ret >= 0) {
                        // printf("recieved frame\n");
                        AVRational tb = (AVRational){1, frame->sample_rate};
                        if (frame->pts != AV_NOPTS_VALUE)
                            frame->pts = av_rescale_q(frame->pts, dec->dec_ctx->pkt_timebase, tb);
                        else if (dec->next_pts != AV_NOPTS_VALUE)
                            frame->pts = av_rescale_q(dec->next_pts, dec->next_pts_tb, tb);
                        if (frame->pts != AV_NOPTS_VALUE) {
                            dec->next_pts = frame->pts + frame->nb_samples;
                            dec->next_pts_tb = tb;
                        }
                    }
                    break;
                }
                default: break;
                }

                if (ret == AVERROR_EOF) {
                    dec->finished = dec->pkt_serial;
                    avcodec_flush_buffers(dec->dec_ctx);
                    return 0;
                }

                if (ret >= 0)
                    return 1;
            } while (ret != AVERROR(EAGAIN));
        }

        do {
            if (dec->pkt_q->nb_packets == 0)
                cond_signal(&dec->empty_queue_cond);
            if (dec->packet_pending) {
                dec->packet_pending = 0;
            } else {
                int old_serial = dec->pkt_serial;
                if (packet_queue_get(dec->pkt_q, dec->pkt, 1, &dec->pkt_serial) < 0)
                    return -1;
                if (old_serial != dec->pkt_serial) {
                    avcodec_flush_buffers(dec->dec_ctx);
                    dec->finished = 0;
                    dec->next_pts = dec->start_pts;
                    dec->next_pts_tb = dec->start_pts_tb;
                }
            }
            if (dec->pkt_q->serial == dec->pkt_serial)
                break;
            av_packet_unref(dec->pkt);
        } while (1);

        if (dec->dec_ctx->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            int got_frame = 0;
            ret = avcodec_decode_subtitle2(dec->dec_ctx, sub, &got_frame, dec->pkt);
            if (ret < 0) {
                ret = AVERROR(EAGAIN);
            } else {
                if (got_frame && !dec->pkt->data) {
                    dec->packet_pending = 1;
                }
                ret = got_frame ? 0 : (dec->pkt->data ? AVERROR(EAGAIN) : AVERROR_EOF);
            }
            av_packet_unref(dec->pkt);
        } else {
            if (dec->pkt->buf && !dec->pkt->opaque_ref) {
                FrameData *fd;

                dec->pkt->opaque_ref = av_buffer_allocz(sizeof(*fd));
                if (!dec->pkt->opaque_ref)
                    return AVERROR(ENOMEM);
                fd = (FrameData*)dec->pkt->opaque_ref->data;
                fd->pkt_pos = dec->pkt->pos;
            }

            int ret = avcodec_send_packet(dec->dec_ctx, dec->pkt);

            if (ret == AVERROR(EAGAIN)) {
                av_log(dec->dec_ctx, AV_LOG_ERROR, "Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
                dec->packet_pending = 1;
            } else {
                av_packet_unref(dec->pkt);
            }
        }
    }
}
