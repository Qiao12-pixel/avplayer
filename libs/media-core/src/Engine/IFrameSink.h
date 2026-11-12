#ifndef AV_ENGINE_IFRAMESINK_H
#define AV_ENGINE_IFRAMESINK_H

#include <memory>

#include "../Define/IVideoFrame.h"

namespace av {

// Decouples AVSynchronizer from the concrete Qt PlayerWidget so the same
// decode pipeline (FileReader -> AVSynchronizer) can be reused server-side
// with a non-Qt sink (e.g. a WebRTC encoder) instead of local GL rendering.
class IFrameSink {
public:
    virtual ~IFrameSink() = default;
    virtual void PushFrame(const std::shared_ptr<IVideoFrame>& frame) = 0;
    virtual void ClearFrame() = 0;
};

}

#endif // AV_ENGINE_IFRAMESINK_H
