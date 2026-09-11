#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <functional>
#include "task_models.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

// RAII Deleters
struct AVFormatContextDeleter {
    void operator()(AVFormatContext* ctx) const {
        if (ctx) {
            if (ctx->pb && !(ctx->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&ctx->pb);
            }
            avformat_free_context(ctx);
        }
    }
};

struct AVFormatInputContextDeleter {
    void operator()(AVFormatContext* ctx) const {
        if (ctx) {
            avformat_close_input(&ctx);
        }
    }
};

struct AVCodecContextDeleter {
    void operator()(AVCodecContext* ctx) const {
        if (ctx) {
            avcodec_free_context(&ctx);
        }
    }
};

struct AVFrameDeleter {
    void operator()(AVFrame* frame) const {
        if (frame) {
            av_frame_free(&frame);
        }
    }
};

struct AVPacketDeleter {
    void operator()(AVPacket* pkt) const {
        if (pkt) {
            av_packet_free(&pkt);
        }
    }
};

struct SwrContextDeleter {
    void operator()(SwrContext* swr) const {
        if (swr) {
            swr_free(&swr);
        }
    }
};

struct AVAudioFifoDeleter {
    void operator()(AVAudioFifo* fifo) const {
        if (fifo) {
            av_audio_fifo_free(fifo);
        }
    }
};

using AVFormatInputContextPtr = std::unique_ptr<AVFormatContext, AVFormatInputContextDeleter>;
using AVFormatOutputContextPtr = std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;
using AVCodecContextPtr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;
using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;
using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;
using SwrContextPtr = std::unique_ptr<SwrContext, SwrContextDeleter>;
using AVAudioFifoPtr = std::unique_ptr<AVAudioFifo, AVAudioFifoDeleter>;

class TranscoderAgent {
public:
    TranscoderAgent() = default;
    ~TranscoderAgent() = default;

    TranscoderAgent(const TranscoderAgent&) = delete;
    TranscoderAgent& operator=(const TranscoderAgent&) = delete;

    static bool transcode(TranscodeTask& task,
                          BitrateProfile profile,
                          TaskProgressCallback progress_cb,
                          std::string& out_error);
};
