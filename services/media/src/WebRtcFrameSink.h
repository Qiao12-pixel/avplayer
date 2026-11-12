#ifndef AV_STREAMING_WEBRTCFRAMESINK_H
#define AV_STREAMING_WEBRTCFRAMESINK_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <rtc/rtc.hpp>

#include "Engine/IFrameSink.h"
#include "Writer/Interface/IVideoEncoder.h"

namespace av {

// Server-side IFrameSink implementation: encodes the decoded YUV420P frames
// AVSynchronizer hands it (same contract PlayerWidget satisfies locally) to
// H264 via the existing VideoEncoder, repackages the Annex-B bitstream it
// produces into length-prefixed NAL units, and feeds libdatachannel's H264
// RTP packetizer so a connected WebRTC peer receives the stream.
class WebRtcFrameSink final : public IFrameSink, private IVideoEncoder::Listener {
public:
    explicit WebRtcFrameSink(std::shared_ptr<rtc::Track> videoTrack);
    ~WebRtcFrameSink() override;

    void PushFrame(const std::shared_ptr<IVideoFrame>& frame) override;
    void ClearFrame() override;

private:
    void OnNotifyVideoPacket(std::shared_ptr<IAVPacket> packet) override;
    static std::vector<std::byte> AnnexBToLengthPrefixed(const uint8_t* data, int size);

    std::shared_ptr<rtc::Track> m_videoTrack;
    std::mutex m_mutex;
    std::unique_ptr<IVideoEncoder> m_encoder;
    bool m_opened{false};
};

}

#endif // AV_STREAMING_WEBRTCFRAMESINK_H
