//
// Created by Joe on 26-3-25.
//

#ifndef IAUDIODECODER_H
#define IAUDIODECODER_H
#include <memory>

#include "../../Define/IAudioSamples.h"
#include "../../Define/IAVPacket.h"
struct AVStream;
namespace av {
    struct IAudioDecoder {
        struct Listener {
            virtual void OnNotifyAudioSamples(std::shared_ptr<IAudioSamples>) = 0;
            virtual ~Listener() = default;
        };

        virtual void SetListener(Listener* listener) = 0;
        virtual void SetStream(struct AVStream* stream) = 0;
        virtual void Decode(std::shared_ptr<IAVPacket> packet) = 0;

        virtual void Start() = 0;
        virtual void Pause() = 0;
        virtual void Stop() = 0;

        virtual ~IAudioDecoder() = default;

        static IAudioDecoder* Create(unsigned int channels, unsigned int sampleRate);
    };
}
#endif //IAUDIODECODER_H
