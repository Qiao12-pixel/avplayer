#ifndef AV_STREAMING_WEBRTCAUDIOSINK_H
#define AV_STREAMING_WEBRTCAUDIOSINK_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <rtc/rtc.hpp>

#include "Engine/IAudioSamplesSink.h"

struct OpusEncoder;
struct SwrContext;

namespace av {

// Server-side audio sink: resamples decoded PCM (normally 44.1 kHz stereo)
// to 48 kHz, encodes 20 ms Opus frames and feeds libdatachannel's Opus RTP
// packetizer. Paired with WebRtcFrameSink so the browser/Qt client receives
// a synchronized audio + video track.
class WebRtcAudioSink final : public IAudioSamplesSink {
public:
    explicit WebRtcAudioSink(std::shared_ptr<rtc::Track> audioTrack);
    ~WebRtcAudioSink() override;

    void PushSamples(const std::shared_ptr<IAudioSamples>& samples) override;
    void Flush() override;

private:
    void EncodeBufferedFramesLocked();
    void ResetLocked();

    std::shared_ptr<rtc::Track> m_audioTrack;
    OpusEncoder* m_encoder{nullptr};
    SwrContext* m_resampler{nullptr};
    unsigned int m_inputChannels{0};
    unsigned int m_inputSampleRate{0};

    std::mutex m_mutex;
    std::vector<int16_t> m_pcm48k;
    double m_nextPtsSeconds{0.0};
    bool m_ptsInitialized{false};
};

}

#endif // AV_STREAMING_WEBRTCAUDIOSINK_H
