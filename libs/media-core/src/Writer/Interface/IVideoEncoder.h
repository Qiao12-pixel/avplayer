//
// Created by Codex on 26-3-28.
//

#ifndef IVIDEOENCODER_H
#define IVIDEOENCODER_H

#include <memory>

#include "../../Define/IAVPacket.h"
#include "../../Define/IVideoFrame.h"

namespace av {
    struct RecorderConfig;

    class IVideoEncoder {
    public:
        struct Listener {
            virtual void OnNotifyVideoPacket(std::shared_ptr<IAVPacket> packet) = 0;
            virtual ~Listener() = default;
        };

        virtual ~IVideoEncoder() = default;

        static IVideoEncoder* Create();

        virtual void SetListener(Listener* listener) = 0;
        virtual bool Open(const RecorderConfig& config, bool globalHeader) = 0;
        virtual void Encode(const std::shared_ptr<IVideoFrame>& videoFrame, int64_t recordingStartUs) = 0;
        virtual void Flush() = 0;
        virtual void Close() = 0;
        virtual AVCodecContext* GetCodecContext() const = 0;
    };
}

#endif //IVIDEOENCODER_H
