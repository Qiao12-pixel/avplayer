//
// Created by Codex on 26-3-28.
//

#include "VideoEncoder.h"

#include <iostream>

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

namespace av {
    namespace {
        constexpr int kVideoBitrate = 2'000'000;
        constexpr AVRational kVideoTimeBase{1, 1000};
    }

    IVideoEncoder* IVideoEncoder::Create() {
        return new VideoEncoder();
    }

    VideoEncoder::VideoEncoder() = default;

    VideoEncoder::~VideoEncoder() {
        Close();
    }

    void VideoEncoder::SetListener(Listener* listener) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_listener = listener;
    }

    bool VideoEncoder::Open(const RecorderConfig& config, bool globalHeader) {
        std::lock_guard<std::mutex> lock(m_mutex);
        CleanupLocked();

        const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
        if (!codec) {
            codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        }
        if (!codec) {
            codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
        }
        if (!codec) {
            std::cerr << "[VideoEncoder] no available video encoder" << std::endl;
            return false;
        }

        m_codecContext = avcodec_alloc_context3(codec);
        if (!m_codecContext) {
            return false;
        }
        m_codecContext->codec_type = AVMEDIA_TYPE_VIDEO;
        m_codecContext->codec_id = codec->id;
        m_codecContext->width = config.width;
        m_codecContext->height = config.height;
        m_codecContext->time_base = kVideoTimeBase;
        m_codecContext->framerate = AVRational{30, 1};
        m_codecContext->bit_rate = kVideoBitrate;
        m_codecContext->gop_size = 48;
        m_codecContext->max_b_frames = 0;
        m_codecContext->pix_fmt = AV_PIX_FMT_YUV420P;

        if (codec->id == AV_CODEC_ID_H264) {
            av_opt_set(m_codecContext->priv_data, "preset", "veryfast", 0);
            av_opt_set(m_codecContext->priv_data, "tune", "zerolatency", 0);
        }
        if (globalHeader) {
            m_codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        const int openResult = avcodec_open2(m_codecContext, codec, nullptr);
        if (openResult < 0) {
            LogError("open video encoder failed", openResult);
            Close();
            return false;
        }
        return true;
    }

    void VideoEncoder::Encode(const std::shared_ptr<IVideoFrame>& videoFrame, int64_t recordingStartUs) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_codecContext || !videoFrame || !videoFrame->data || recordingStartUs == AV_NOPTS_VALUE) {
            return;
        }

        const int64_t videoTimestampUs = GetTimestampUs(videoFrame->pts, videoFrame->timebaseNum, videoFrame->timebaseDen);
        if (videoTimestampUs == AV_NOPTS_VALUE) {
            return;
        }

        AVFrame* frame = av_frame_alloc();
        if (!frame) {
            return;
        }
        frame->format = m_codecContext->pix_fmt;
        frame->width = m_codecContext->width;
        frame->height = m_codecContext->height;
        if (av_frame_get_buffer(frame, 32) < 0) {
            av_frame_free(&frame);
            return;
        }

        m_swsContext = sws_getCachedContext(m_swsContext,
                                            static_cast<int>(videoFrame->width),
                                            static_cast<int>(videoFrame->height),
                                            AV_PIX_FMT_RGBA,
                                            m_codecContext->width,
                                            m_codecContext->height,
                                            m_codecContext->pix_fmt,
                                            SWS_BILINEAR,
                                            nullptr,
                                            nullptr,
                                            nullptr);
        if (!m_swsContext) {
            av_frame_free(&frame);
            return;
        }

        uint8_t* srcData[4] = {videoFrame->data.get(), nullptr, nullptr, nullptr};
        int srcLineSize[4] = {static_cast<int>(videoFrame->width) * 4, 0, 0, 0};
        sws_scale(m_swsContext, srcData, srcLineSize, 0, static_cast<int>(videoFrame->height), frame->data, frame->linesize);

        const int64_t relativeVideoUs = std::max<int64_t>(0, videoTimestampUs - recordingStartUs);
        int64_t pts = av_rescale_q(relativeVideoUs, AV_TIME_BASE_Q, m_codecContext->time_base);
        if (m_lastVideoPts != AV_NOPTS_VALUE && pts <= m_lastVideoPts) {
            pts = m_lastVideoPts + 1;
        }
        m_lastVideoPts = pts;
        frame->pts = pts;

        EmitPackets(frame);
        av_frame_free(&frame);
    }

    void VideoEncoder::Flush() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_codecContext) {
            return;
        }
        EmitPackets(nullptr);
    }

    void VideoEncoder::Close() {
        std::lock_guard<std::mutex> lock(m_mutex);
        CleanupLocked();
    }

    void VideoEncoder::CleanupLocked() {
        if (m_swsContext) {
            sws_freeContext(m_swsContext);
            m_swsContext = nullptr;
        }
        if (m_codecContext) {
            avcodec_free_context(&m_codecContext);
        }
        m_lastVideoPts = AV_NOPTS_VALUE;
    }

    AVCodecContext* VideoEncoder::GetCodecContext() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_codecContext;
    }

    bool VideoEncoder::EmitPackets(AVFrame* frame) {
        const int sendResult = avcodec_send_frame(m_codecContext, frame);
        if (sendResult < 0) {
            LogError("send video frame failed", sendResult);
            return false;
        }

        AVPacket* packet = av_packet_alloc();
        if (!packet) {
            return false;
        }

        while (true) {
            const int receiveResult = avcodec_receive_packet(m_codecContext, packet);
            if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
                break;
            }
            if (receiveResult < 0) {
                LogError("receive video packet failed", receiveResult);
                av_packet_free(&packet);
                return false;
            }
            auto packetHolder = std::make_shared<IAVPacket>(packet);
            packetHolder->timeBase = m_codecContext->time_base;
            if (m_listener) {
                m_listener->OnNotifyVideoPacket(packetHolder);
            }
            packet = av_packet_alloc();
            if (!packet) {
                return false;
            }
        }
        av_packet_free(&packet);
        return true;
    }

    int64_t VideoEncoder::GetTimestampUs(int64_t pts, int32_t timebaseNum, int32_t timebaseDen) const {
        if (pts == AV_NOPTS_VALUE || timebaseNum <= 0 || timebaseDen <= 0) {
            return AV_NOPTS_VALUE;
        }
        return av_rescale_q(pts, AVRational{timebaseNum, timebaseDen}, AV_TIME_BASE_Q);
    }

    void VideoEncoder::LogError(const char* prefix, int errorCode) const {
        char errorBuffer[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(errorCode, errorBuffer, sizeof(errorBuffer));
        std::cerr << "[VideoEncoder] " << prefix << ": " << errorBuffer << std::endl;
    }
}
