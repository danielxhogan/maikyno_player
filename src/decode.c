#include "decode.h"
#include "frame_queue.h"

int initialize_decoder(Decoder *dec, AVCodecParameters *codecpar, Cond need_pkts)
{
    int ret = 0;
    const AVCodec *av_dec;

    memset(dec, 0, sizeof(Decoder));
    dec->need_pkts = need_pkts;
    dec->start_pts = AV_NOPTS_VALUE;
    dec->pkt_serial = -1;

    dec->av_dec = avcodec_alloc_context3(NULL);
    if (!dec->av_dec)
        return AVERROR(ENOMEM);

    ret = avcodec_parameters_to_context(dec->av_dec,
        codecpar);
    if (ret < 0)
        return ret;

    av_dec = avcodec_find_decoder(dec->av_dec->codec_id);
    if (!av_dec) {
        fprintf(stderr, "Failed to find decoder for codec %s.\n",
            avcodec_get_name(dec->av_dec->codec_id));
        ret = AVERROR(EINVAL);
        return ret;
    }

    ret = avcodec_open2(dec->av_dec, av_dec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Failed to open decoder context.\n"
            "Libav Error: %s\n", av_err2str(ret));
        return ret;
    }

    dec->av_pkt = av_packet_alloc();
    if (!dec->av_pkt)
        return AVERROR(ENOMEM);

    if ((ret = packet_queue_init(&dec->pkt_q)) != 0)
        return ret;

    return 0;
}

void decoder_destroy(Decoder *dec)
{
    av_packet_free(&dec->av_pkt);
    avcodec_free_context(&dec->av_dec);
}

int decode_frame(Decoder *dec, AVFrame *av_frame, AVSubtitle *sub)
{
    int ret = AVERROR(EAGAIN);

    for (;;) {
        if (dec->pkt_q.serial == dec->pkt_serial) {
            do {
                if (dec->pkt_q.abort_request)
                    return -1;

                switch (dec->av_dec->codec_type) {
                case AVMEDIA_TYPE_VIDEO: {
                    ret = avcodec_receive_frame(dec->av_dec, av_frame);
                    if (ret >= 0) {
                        av_frame->pts = av_frame->best_effort_timestamp;
                    }
                    break;
                }
                case AVMEDIA_TYPE_AUDIO: {
                    ret = avcodec_receive_frame(dec->av_dec, av_frame);
                    if (ret < 0)
                        break;
                        
                    AVRational tb = (AVRational) {1, av_frame->sample_rate};

                    if (av_frame->pts != AV_NOPTS_VALUE) {
                        av_frame->pts = av_rescale_q(av_frame->pts,
                            dec->av_dec->pkt_timebase, tb);
                    }
                    else if (dec->next_pts != AV_NOPTS_VALUE) {
                        av_frame->pts = av_rescale_q(dec->next_pts,
                            dec->next_pts_tb, tb);
                    }

                    if (av_frame->pts != AV_NOPTS_VALUE) {
                        dec->next_pts = av_frame->pts + av_frame->nb_samples;
                        dec->next_pts_tb = tb;
                    }
                    break;
                }
                default: break;
                }

                if (ret == AVERROR_EOF) {
                    dec->finished = dec->pkt_serial;
                    avcodec_flush_buffers(dec->av_dec);
                    return 0;
                }

                if (ret >= 0)
                    return 1;
            } while (ret != AVERROR(EAGAIN));
        }

        do {
            if (dec->pkt_q.nb_packets == 0)
                cond_signal(&dec->need_pkts);

            if (dec->packet_pending) {
                dec->packet_pending = 0;
            } else {
                int old_serial = dec->pkt_serial;
                if (packet_queue_get(&dec->pkt_q, dec->av_pkt, 1,
                    &dec->pkt_serial) < 0)
                    return -1;

                if (old_serial != dec->pkt_serial) {
                    avcodec_flush_buffers(dec->av_dec);
                    dec->finished = 0;
                    dec->next_pts = dec->start_pts;
                    dec->next_pts_tb = dec->start_pts_tb;
                }
            }

            if (dec->pkt_q.serial == dec->pkt_serial)
                break;

            av_packet_unref(dec->av_pkt);
        } while (1);

        if (dec->av_dec->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            int got_frame = 0;
            ret = avcodec_decode_subtitle2(dec->av_dec, sub, &got_frame, dec->av_pkt);
            if (ret < 0) {
                ret = AVERROR(EAGAIN);
            } else {
                if (got_frame && !dec->av_pkt->data) {
                    dec->packet_pending = 1;
                }
                ret = got_frame ? 0 : (dec->av_pkt->data ? AVERROR(EAGAIN) : AVERROR_EOF);
            }
            av_packet_unref(dec->av_pkt);
        } else {
            if (dec->av_pkt->buf && !dec->av_pkt->opaque_ref) {
                FrameData *fd;

                dec->av_pkt->opaque_ref = av_buffer_allocz(sizeof(*fd));
                if (!dec->av_pkt->opaque_ref)
                    return AVERROR(ENOMEM);
                fd = (FrameData*) dec->av_pkt->opaque_ref->data;
                fd->pkt_pos = dec->av_pkt->pos;
            }

            int ret = avcodec_send_packet(dec->av_dec, dec->av_pkt);

            if (ret == AVERROR(EAGAIN)) {
                av_log(dec->av_dec, AV_LOG_ERROR,
                    "Receive_frame and send_packet both returned EAGAIN, "
                    "which is an API violation.\n");
                dec->packet_pending = 1;
            } else {
                av_packet_unref(dec->av_pkt);
            }
        }
    }
}
