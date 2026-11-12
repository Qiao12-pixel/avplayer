//
// Created by Codex on 26-3-28.
//

#ifndef MUXER_H
#define MUXER_H

#include <mutex>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
}

#include "IMuxer.h"

namespace av {
    class Muxer : public IMuxer {
    public:
        Muxer();
        ~Muxer() override;

        void SetListener(Listener* listener) override;
        bool Open(const std::string& filePath,
                  AVCodecContext* videoCodecContext,
                  AVCodecContext* audioCodecContext) override;
        void NotifyAudioPacket(const std::shared_ptr<IAVPacket>& packet) override;
        void NotifyVideoPacket(const std::shared_ptr<IAVPacket>& packet) override;
        void Close() override;
        bool IsOpen() const override;

    protected:
        virtual bool AllocateOutputContext(const std::string& filePath) = 0;

        bool AddVideoStream(AVCodecContext* videoCodecContext);
        bool AddAudioStream(AVCodecContext* audioCodecContext);
        bool OpenIo(const std::string& filePath);
        bool WriteHeader();
        bool WritePacket(const std::shared_ptr<IAVPacket>& packet, bool isVideo);
        void Cleanup();
        void LogError(const char* prefix, int errorCode) const;

        mutable std::mutex m_mutex;
        std::string m_filePath;
        Listener* m_listener{nullptr};
        AVFormatContext* m_formatContext{nullptr};
        AVStream* m_videoStream{nullptr};
        AVStream* m_audioStream{nullptr};
        AVRational m_videoInputTimeBase{0, 1};
        AVRational m_audioInputTimeBase{0, 1};
        bool m_headerWritten{false};
        bool m_opened{false};
    };
}

#endif //MUXER_H
