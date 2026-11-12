//
// Created by Codex on 26-3-28.
//

#ifndef IAUDIOENCODER_H
#define IAUDIOENCODER_H

#include <memory>

#include "../../Define/IAudioSamples.h"
#include "../../Define/IAVPacket.h"

namespace av {
    struct RecorderConfig;

    class IAudioEncoder {
    public:
        struct Listener {
            virtual void OnNotifyAudioPacket(std::shared_ptr<IAVPacket> packet) = 0;
            virtual ~Listener() = default;
        };

        virtual ~IAudioEncoder() = default;

        static IAudioEncoder* Create();

        virtual void SetListener(Listener* listener) = 0;
        virtual bool Open(const RecorderConfig& config, bool globalHeader) = 0;
        virtual void Encode(const std::shared_ptr<IAudioSamples>& audioSamples, int64_t recordingStartUs) = 0;
        virtual void Flush() = 0;
        virtual void Close() = 0;
        virtual AVCodecContext* GetCodecContext() const = 0;
    };
}

#endif //IAUDIOENCODER_H
