//
// Created by Codex on 26-3-28.
//

#include "Muxer.h"

#include <iostream>

namespace av {
    Muxer::Muxer() = default;

    Muxer::~Muxer() {
        Close();
    }

    void Muxer::SetListener(Listener* listener) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_listener = listener;
    }

    bool Muxer::Open(const std::string& filePath,
                     AVCodecContext* videoCodecContext,
                     AVCodecContext* audioCodecContext) {
        Listener* listener = nullptr;
        int errorCode = 0;
        std::string errorMessage;
        bool opened = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            Cleanup();
            m_filePath = filePath;
            listener = m_listener;
            if (!AllocateOutputContext(filePath)) {
                errorCode = -1;
                errorMessage = "allocate output context failed";
                Cleanup();
            } else if (videoCodecContext && !AddVideoStream(videoCodecContext)) {
                errorCode = -2;
                errorMessage = "add video stream failed";
                Cleanup();
            } else if (audioCodecContext && !AddAudioStream(audioCodecContext)) {
                errorCode = -3;
                errorMessage = "add audio stream failed";
                Cleanup();
            } else if (!OpenIo(filePath)) {
                errorCode = -4;
                errorMessage = "open io failed";
                Cleanup();
            } else if (!WriteHeader()) {
                errorCode = -5;
                errorMessage = "write header failed";
                Cleanup();
            } else {
                m_opened = true;
                opened = true;
            }
        }
        if (opened) {
            if (listener) {
                listener->OnMuxerOpen(filePath);
            }
            return true;
        }
        if (listener) {
            listener->OnMuxerError(errorCode, errorMessage);
        }
        return false;
    }

    void Muxer::NotifyAudioPacket(const std::shared_ptr<IAVPacket>& packet) {
        std::lock_guard<std::mutex> lock(m_mutex);
        WritePacket(packet, false);
    }

    void Muxer::NotifyVideoPacket(const std::shared_ptr<IAVPacket>& packet) {
        std::lock_guard<std::mutex> lock(m_mutex);
        WritePacket(packet, true);
    }

    void Muxer::Close() {
        std::string filePath;
        Listener* listener = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            filePath = m_filePath;
            listener = m_listener;
            if (m_formatContext && m_headerWritten) {
                av_write_trailer(m_formatContext);
            }
            Cleanup();
        }
        if (listener && !filePath.empty()) {
            listener->OnMuxerClose(filePath);
        }
    }

    bool Muxer::IsOpen() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_opened;
    }

    bool Muxer::AddVideoStream(AVCodecContext* videoCodecContext) {
        m_videoStream = avformat_new_stream(m_formatContext, nullptr);
        if (!m_videoStream) {
            std::cerr << "[Muxer] create video stream failed" << std::endl;
            return false;
        }
        const int result = avcodec_parameters_from_context(m_videoStream->codecpar, videoCodecContext);
        if (result < 0) {
            LogError("copy video codec params failed", result);
            return false;
        }
        m_videoStream->time_base = videoCodecContext->time_base;
        m_videoInputTimeBase = videoCodecContext->time_base;
        return true;
    }

    bool Muxer::AddAudioStream(AVCodecContext* audioCodecContext) {
        m_audioStream = avformat_new_stream(m_formatContext, nullptr);
        if (!m_audioStream) {
            std::cerr << "[Muxer] create audio stream failed" << std::endl;
            return false;
        }
        const int result = avcodec_parameters_from_context(m_audioStream->codecpar, audioCodecContext);
        if (result < 0) {
            LogError("copy audio codec params failed", result);
            return false;
        }
        m_audioStream->time_base = audioCodecContext->time_base;
        m_audioInputTimeBase = audioCodecContext->time_base;
        return true;
    }

    bool Muxer::OpenIo(const std::string& filePath) {
        if (!m_formatContext) {
            return false;
        }
        if ((m_formatContext->oformat->flags & AVFMT_NOFILE) != 0) {
            return true;
        }
        const int result = avio_open(&m_formatContext->pb, filePath.c_str(), AVIO_FLAG_WRITE);
        if (result < 0) {
            LogError("open output file failed", result);
            return false;
        }
        return true;
    }

    bool Muxer::WriteHeader() {
        const int result = avformat_write_header(m_formatContext, nullptr);
        if (result < 0) {
            LogError("write header failed", result);
            return false;
        }
        m_headerWritten = true;
        return true;
    }

    bool Muxer::WritePacket(const std::shared_ptr<IAVPacket>& packet, bool isVideo) {
        if (!m_opened || !m_formatContext || !packet || !packet->avPacket) {
            return false;
        }
        AVStream* stream = isVideo ? m_videoStream : m_audioStream;
        AVRational inputTimeBase = packet->timeBase.num > 0 && packet->timeBase.den > 0
                                   ? packet->timeBase
                                   : (isVideo ? m_videoInputTimeBase : m_audioInputTimeBase);
        if (!stream) {
            return false;
        }

        AVPacket* writablePacket = av_packet_clone(packet->avPacket);
        if (!writablePacket) {
            return false;
        }
        av_packet_rescale_ts(writablePacket, inputTimeBase, stream->time_base);
        writablePacket->stream_index = stream->index;
        const int result = av_interleaved_write_frame(m_formatContext, writablePacket);
        av_packet_free(&writablePacket);
        if (result < 0) {
            LogError("interleaved write packet failed", result);
            return false;
        }
        return true;
    }

    void Muxer::Cleanup() {
        m_opened = false;
        m_headerWritten = false;
        m_videoStream = nullptr;
        m_audioStream = nullptr;
        m_videoInputTimeBase = AVRational{0, 1};
        m_audioInputTimeBase = AVRational{0, 1};
        if (m_formatContext) {
            if ((m_formatContext->oformat->flags & AVFMT_NOFILE) == 0 && m_formatContext->pb) {
                avio_closep(&m_formatContext->pb);
            }
            avformat_free_context(m_formatContext);
            m_formatContext = nullptr;
        }
        m_filePath.clear();
    }

    void Muxer::LogError(const char* prefix, int errorCode) const {
        char errorBuffer[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(errorCode, errorBuffer, sizeof(errorBuffer));
        std::cerr << "[Muxer] " << prefix << ": " << errorBuffer << std::endl;
    }
}
