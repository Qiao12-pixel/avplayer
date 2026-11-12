//
// Created by Codex on 26-3-28.
//

#ifndef IMUXER_H
#define IMUXER_H

#include <memory>
#include <string>

#include "../Define/IAVPacket.h"

namespace av {
    class IMuxer {
    public:
        struct Listener {
            virtual void OnMuxerOpen(const std::string& filePath) = 0;
            virtual void OnMuxerClose(const std::string& filePath) = 0;
            virtual void OnMuxerError(int code, const std::string& message) = 0;
            virtual ~Listener() = default;
        };

        virtual ~IMuxer() = default;

        virtual void SetListener(Listener* listener) = 0;
        virtual bool Open(const std::string& filePath,
                          AVCodecContext* videoCodecContext,
                          AVCodecContext* audioCodecContext) = 0;
        virtual void NotifyAudioPacket(const std::shared_ptr<IAVPacket>& packet) = 0;
        virtual void NotifyVideoPacket(const std::shared_ptr<IAVPacket>& packet) = 0;
        virtual void Close() = 0;
        virtual bool IsOpen() const = 0;
    };
}

#endif //IMUXER_H
