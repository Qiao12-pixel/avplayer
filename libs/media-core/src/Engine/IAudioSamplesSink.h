#ifndef AV_ENGINE_IAUDIOSAMPLESSINK_H
#define AV_ENGINE_IAUDIOSAMPLESSINK_H

#include <memory>

#include "../Define/IAudioSamples.h"

namespace av {

// Decouples AVSynchronizer from the local Qt audio device so the same decode
// pipeline can feed a WebRTC Opus encoder on the server.
class IAudioSamplesSink {
public:
    virtual ~IAudioSamplesSink() = default;
    virtual void PushSamples(const std::shared_ptr<IAudioSamples>& samples) = 0;
    virtual void Flush() = 0;
};

}

#endif // AV_ENGINE_IAUDIOSAMPLESSINK_H
