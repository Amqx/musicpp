//
// Created by Jonathan on 23-Dec-25.
//

#ifndef M3U8_M3U8_INTERNALS_H
#define M3U8_M3U8_INTERNALS_H

#include "constants.h"
#include <functional>
#include <variant>
#include <string>
#include <vector>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
}

static int out_write(void *opaque, uint8_t *buf, int buf_size);

static int64_t out_seek(void *, int64_t, int);

static double LoopMetric(const uint8_t *a, const uint8_t *b);

struct InFmtDeleter {
    void operator()(AVFormatContext *p) const {
        if (p) avformat_close_input(&p);
    }
};

using InFmtPtr = std::unique_ptr<AVFormatContext, InFmtDeleter>;

struct OutFmtDeleter {
    void operator()(AVFormatContext *p) const {
        if (!p) return;
        if (p->pb) {
            avio_context_free(&p->pb);
        }
        avformat_free_context(p);
    }
};

using OutFmtPtr = std::unique_ptr<AVFormatContext, OutFmtDeleter>;

struct CodecCtxDeleter {
    void operator()(AVCodecContext *p) const {
        avcodec_free_context(&p);
    }
};

using CodecCtxPtr = std::unique_ptr<AVCodecContext, CodecCtxDeleter>;

struct FrameDeleter {
    void operator()(AVFrame *p) const { av_frame_free(&p); }
};

using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

struct PacketDeleter {
    void operator()(AVPacket *p) const { av_packet_free(&p); }
};

using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;

struct SwsDeleter {
    void operator()(SwsContext *p) const { sws_freeContext(p); }
};

using SwsPtr = std::unique_ptr<SwsContext, SwsDeleter>;

struct OutBuffer {
    std::vector<uint8_t> data;
};

struct InputVideo {
    InFmtPtr fmt;
    int vstream = -1;
};

struct GifOutput {
    OutFmtPtr out;
    CodecCtxPtr enc_ctx;
    AVStream *ost = nullptr;
    std::unique_ptr<OutBuffer> ob = std::make_unique<OutBuffer>();
    bool header_written = false;
    AVIOContext *avio = nullptr;
};

struct SwsBundle {
    SwsPtr to_rgb8;
    SwsPtr to_gray32;
};

struct WorkBuffers {
    FramePtr dec_frame;
    FramePtr gif_frame;
    PacketPtr pkt;
    PacketPtr outpkt;
};

struct Err {
    std::string msg;
};

template<class T>
using Result = std::variant<T, Err>;
using EmptyResult = std::variant<std::monostate, Err>;

static inline std::string FfErrStr(int err);

#define TRY(expr) \
    do { \
        auto _r = (expr); \
        if (std::holds_alternative<Err>(_r)) return std::get<Err>(_r); \
        (void)0; \
    } while (0)

template<class T>
static T &&UNWRAP(Result<T> &&r) {
    return std::get<T>(std::move(r));
}

Result<InputVideo> OpenInputAndFindVideostream(const std::string &url);

Result<CodecCtxPtr> BuildDecoder(const AVStream *ist);

Result<GifOutput> CreateGifOutput(int resolution);

EmptyResult FinalizeGifOutput(const GifOutput &go);

Result<SwsBundle> CreateSws(const AVCodecContext *dec_ctx, const AVCodecContext *enc_ctx, int resolution);

Result<WorkBuffers> CreateWorkBuffers(const AVCodecContext *enc_ctx, int resolution);

EmptyResult FlushEncoder(const GifOutput &go, const WorkBuffers &wb);

EmptyResult TranscodeLoop(const InputVideo &iv, AVCodecContext *dec_ctx, const GifOutput &go, const SwsBundle &sws,
                          const WorkBuffers &wb, const std::function<bool(uint64_t loop_gen)> &is_cancelled,
                          uint64_t loop_gen);

#endif //M3U8_M3U8_INTERNALS_H
