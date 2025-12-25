//
// Created by Jonathan on 23-Dec-25.
//

#include "m3u8_internals.h"
#include <iostream>

using namespace std;

static int out_write(void *opaque, const uint8_t *buf, int buf_size) {
    auto *ob = static_cast<OutBuffer *>(opaque);
    ob->data.insert(ob->data.end(), buf, buf + buf_size);
    return buf_size;
}

static int64_t out_seek(void *, int64_t, int) { return -1; }

static double LoopMetric(const uint8_t *a, const uint8_t *b) {
    uint64_t sum = 0;
    for (int i = 0; i < 32 * 32; i++)
        sum += static_cast<uint64_t>(
            abs(static_cast<int>(a[i]) - static_cast<int>(b[i])));
    return static_cast<double>(sum) / static_cast<double>(32 * 32);
}

std::string FfErrStr(const int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return {buf};
}

Result<InputVideo> OpenInputAndFindVideostream(const std::string &url) {
    avformat_network_init();

    AVFormatContext *raw = nullptr;
    int r = avformat_open_input(&raw, url.c_str(), nullptr, nullptr);
    if (r < 0) return Err{"avformat_open_input failed: " + FfErrStr(r)};
    InFmtPtr in(raw);

    r = avformat_find_stream_info(in.get(), nullptr);
    if (r < 0) return Err{"avformat_find_stream_info failed: " + FfErrStr(r)};

    int vstream = -1;
    for (unsigned i = 0; i < in->nb_streams; i++) {
        if (in->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            vstream = static_cast<int>(i);
            break;
        }
    }
    if (vstream < 0) return Err{"No video stream found in m3u8"};

    return InputVideo{std::move(in), vstream};
}

Result<CodecCtxPtr> BuildDecoder(const AVStream *ist) {
    AVCodecParameters *ipar = ist->codecpar;
    const AVCodec *dec = avcodec_find_decoder(ipar->codec_id);
    if (!dec) {
        // remove this
        cout << "Error: no decoder found for ID " << ipar->codec_id << " (Name: " << avcodec_get_name(ipar->codec_id) <<
                ")" << endl;
        return Err{"No decoder for input codec"};
    }

    CodecCtxPtr dec_ctx(avcodec_alloc_context3(dec));
    if (!dec_ctx) return Err{"Failed to allocate decoder context"};

    int r = avcodec_parameters_to_context(dec_ctx.get(), ipar);
    if (r < 0) return Err{"avcodec_parameters_to_context failed: " + FfErrStr(r)};

    r = avcodec_open2(dec_ctx.get(), dec, nullptr);
    if (r < 0) return Err{"avcodec_open2 (decoder) failed: " + FfErrStr(r)};

    return dec_ctx;
}

Result<GifOutput> CreateGifOutput(const int resolution) {
    GifOutput go;

    // format
    AVFormatContext *raw_out = nullptr;
    int r = avformat_alloc_output_context2(&raw_out, nullptr, "gif", nullptr);
    if (r < 0 || !raw_out) return Err{"avformat_alloc_output_context2 failed: " + FfErrStr(r)};
    go.out.reset(raw_out);

    // encoder
    const AVCodec *enc = avcodec_find_encoder(AV_CODEC_ID_GIF);
    if (!enc) return Err{"Missing GIF encoder (AV_CODEC_ID_GIF)"};

    go.enc_ctx.reset(avcodec_alloc_context3(enc));
    if (!go.enc_ctx) return Err{"Failed to allocate encoder context"};

    go.enc_ctx->width = resolution;
    go.enc_ctx->height = resolution;
    go.enc_ctx->pix_fmt = AV_PIX_FMT_RGB8;
    go.enc_ctx->time_base = AVRational{1, kGifFPS};
    go.enc_ctx->framerate = AVRational{kGifFPS, 1};

    if (go.out->oformat->flags & AVFMT_GLOBALHEADER)
        go.enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    r = avcodec_open2(go.enc_ctx.get(), enc, nullptr);
    if (r < 0) return Err{"avcodec_open2 (encoder) failed: " + FfErrStr(r)};

    // stream
    go.ost = avformat_new_stream(go.out.get(), nullptr);
    if (!go.ost) return Err{"avformat_new_stream failed"};
    go.ost->time_base = go.enc_ctx->time_base;

    r = avcodec_parameters_from_context(go.ost->codecpar, go.enc_ctx.get());
    if (r < 0) return Err{"avcodec_parameters_from_context failed: " + FfErrStr(r)};

    // custom AVIO -> OutBuffer
    constexpr int avio_buf_size = 1 << 20;
    const auto avio_buf = static_cast<uint8_t *>(av_malloc(avio_buf_size));
    if (!avio_buf) return Err{"av_malloc for AVIO buffer failed"};

    go.avio = avio_alloc_context(
        avio_buf, avio_buf_size,
        1, // writeable
        go.ob.get(),
        nullptr,
        &out_write,
        &out_seek
    );
    if (!go.avio) {
        av_free(avio_buf);
        return Err{"avio_alloc_context failed"};
    }
    go.avio->seekable = 0;
    go.out->pb = go.avio;

    // header (loop=0)
    AVDictionary *mux_opts = nullptr;
    av_dict_set_int(&mux_opts, "loop", 0, 0);

    r = avformat_write_header(go.out.get(), &mux_opts);
    av_dict_free(&mux_opts);

    if (r < 0) return Err{"avformat_write_header failed: " + FfErrStr(r)};
    go.header_written = true;

    return go;
}

EmptyResult FinalizeGifOutput(const GifOutput &go) {
    if (!go.header_written) return Err{"Finalize called before header"};

    if (const int r = av_write_trailer(go.out.get()); r < 0) return Err{"av_write_trailer failed: " + FfErrStr(r)};

    return monostate{};
}

Result<SwsBundle> CreateSws(const AVCodecContext *dec_ctx, const AVCodecContext *enc_ctx, int resolution) {
    SwsBundle sb;

    sb.to_rgb8.reset(sws_getContext(
        dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
        resolution, resolution, enc_ctx->pix_fmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    ));
    if (!sb.to_rgb8) return Err{"sws_getContext (to_rgb8) failed"};

    sb.to_gray32.reset(sws_getContext(
        dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
        32, 32, AV_PIX_FMT_GRAY8,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    ));
    if (!sb.to_gray32) return Err{"sws_getContext (to_gray32) failed"};

    return sb;
}

Result<WorkBuffers> CreateWorkBuffers(const AVCodecContext *enc_ctx, int resolution) {
    WorkBuffers wb;
    wb.dec_frame.reset(av_frame_alloc());
    wb.gif_frame.reset(av_frame_alloc());
    if (!wb.dec_frame || !wb.gif_frame) return Err{"av_frame_alloc failed"};

    wb.gif_frame->format = enc_ctx->pix_fmt;
    wb.gif_frame->width = resolution;
    wb.gif_frame->height = resolution;

    if (const int r = av_frame_get_buffer(wb.gif_frame.get(), 32); r < 0)
        return Err{
            "av_frame_get_buffer (gif_frame) failed: " + FfErrStr(r)
        };

    wb.pkt.reset(av_packet_alloc());
    wb.outpkt.reset(av_packet_alloc());
    if (!wb.pkt || !wb.outpkt) return Err{"av_packet_alloc failed"};

    return wb;
}

EmptyResult FlushEncoder(const GifOutput &go, const WorkBuffers &wb) {
    int r = avcodec_send_frame(go.enc_ctx.get(), nullptr);
    if (r < 0) return Err{"avcodec_send_frame (flush) failed: " + FfErrStr(r)};

    while (true) {
        r = avcodec_receive_packet(go.enc_ctx.get(), wb.outpkt.get());
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
        if (r < 0) return Err{"avcodec_receive_packet (flush) failed: " + FfErrStr(r)};

        av_packet_rescale_ts(wb.outpkt.get(), go.enc_ctx->time_base, go.ost->time_base);
        wb.outpkt->stream_index = go.ost->index;

        r = av_interleaved_write_frame(go.out.get(), wb.outpkt.get());
        if (r < 0) return Err{"av_interleaved_write_frame (flush) failed: " + FfErrStr(r)};

        av_packet_unref(wb.outpkt.get());
    }
    return monostate{};
}

EmptyResult TranscodeLoop(const InputVideo &iv, AVCodecContext *dec_ctx, const GifOutput &go, const SwsBundle &sws,
                          const WorkBuffers &wb, const std::function<bool(uint64_t loop_gen)> &is_cancelled,
                          uint64_t loop_gen) {
    vector<uint8_t> first_gray(32 * 32);
    bool have_first = false;
    bool stop = false;
    int64_t frame_index = 0;
    int64_t analyzed_frames = 0;

    while (!stop && frame_index < kGifMaxTotalFrames && av_read_frame(iv.fmt.get(), wb.pkt.get()) >= 0) {
        if (is_cancelled(loop_gen)) return Err{"Cancelled"};
        if (wb.pkt->stream_index != iv.vstream) {
            av_packet_unref(wb.pkt.get());
            continue;
        }

        int r = avcodec_send_packet(dec_ctx, wb.pkt.get());
        av_packet_unref(wb.pkt.get());
        if (r < 0) continue; // you were skipping bad packets; keep that behavior

        while (!stop) {
            r = avcodec_receive_frame(dec_ctx, wb.dec_frame.get());
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
            if (r < 0) return Err{"avcodec_receive_frame failed: " + FfErrStr(r)};

            //temp
            if (!wb.dec_frame->data[0]) {
                cerr << "Warning: received frame with NULL data, skipping" << endl;
                av_frame_unref(wb.dec_frame.get());
                continue;
            }
            if (wb.dec_frame->width == 0 || wb.dec_frame->height == 0) {
                cerr << "Warning: received frame with invalid dimensions, skipping" << endl;
                av_frame_unref(wb.dec_frame.get());
                continue;
            }

            // gray32 downscale for loop detection
            uint8_t gray32[32 * 32];
            uint8_t *gray_dst[4] = {gray32, nullptr, nullptr, nullptr};
            int gray_linesize[4] = {32, 0, 0, 0};

            sws_scale(
                sws.to_gray32.get(),
                wb.dec_frame->data,
                wb.dec_frame->linesize,
                0,
                wb.dec_frame->height,
                gray_dst,
                gray_linesize
            );

            if (!have_first) {
                std::memcpy(first_gray.data(), gray32, 32 * 32);
                have_first = true;
            } else if (analyzed_frames >= kGifMinFramesBeforeMatch) {
                if (LoopMetric(first_gray.data(), gray32) <= kGifMatchThreshold) {
                    stop = true;
                    break;
                }
            }
            analyzed_frames++;

            // scale to RGB8 for encoder
            r = av_frame_make_writable(wb.gif_frame.get());
            if (r < 0) return Err{"av_frame_make_writable failed: " + FfErrStr(r)};

            sws_scale(
                sws.to_rgb8.get(),
                wb.dec_frame->data,
                wb.dec_frame->linesize,
                0,
                wb.dec_frame->height,
                wb.gif_frame->data,
                wb.gif_frame->linesize
            );

            wb.gif_frame->pts = frame_index++;

            r = avcodec_send_frame(go.enc_ctx.get(), wb.gif_frame.get());
            if (r < 0) return Err{"avcodec_send_frame (encode) failed: " + FfErrStr(r)};

            while (true) {
                r = avcodec_receive_packet(go.enc_ctx.get(), wb.outpkt.get());
                if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
                if (r < 0) return Err{"avcodec_receive_packet (encode) failed: " + FfErrStr(r)};

                av_packet_rescale_ts(wb.outpkt.get(), go.enc_ctx->time_base, go.ost->time_base);
                wb.outpkt->stream_index = go.ost->index;

                r = av_interleaved_write_frame(go.out.get(), wb.outpkt.get());
                if (r < 0) return Err{"av_interleaved_write_frame failed: " + FfErrStr(r)};

                av_packet_unref(wb.outpkt.get());
            }
        }
    }

    // flush encoder packets
    TRY(FlushEncoder(go, wb));

    return monostate{};
}
