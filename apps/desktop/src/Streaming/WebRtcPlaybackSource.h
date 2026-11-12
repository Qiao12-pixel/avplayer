#ifndef AV_STREAMING_WEBRTCPLAYBACKSOURCE_H
#define AV_STREAMING_WEBRTCPLAYBACKSOURCE_H

#include <atomic>
#include <memory>
#include <string>
#include <mutex>
#include <thread>

#include <grpcpp/grpcpp.h>
#include <rtc/rtc.hpp>

#include "Engine/AudioRender.h"
#include "Engine/IFrameSink.h"
#include "agent.grpc.pb.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

struct OpusDecoder;

namespace av {

// Client-side counterpart to MediaSession/WebRtcFrameSink (Phase 2,
// Checkpoint C): negotiates a recvonly WebRTC video track with media_server
// over StreamingService, reassembles the incoming RTP into Annex-B access
// units via libdatachannel's H264 depacketizer, decodes them with a plain
// libavcodec H264 decoder, and pushes the result into an IFrameSink — the
// same interface PlayerWidget already implements for local playback, so
// MainWindow can point the existing widget at either source unchanged.
class WebRtcPlaybackSource final {
public:
    struct PlaybackStatus {
        bool success{false};
        std::string error;
        double durationMs{0.0};
        double positionMs{0.0};
        bool playing{false};
        bool recording{false};
        std::string outputPath;
    };

    explicit WebRtcPlaybackSource(IFrameSink* sink);
    ~WebRtcPlaybackSource();

    // Blocks until the answer/first candidates are exchanged or `timeoutMs`
    // elapses. Decoded frames start arriving asynchronously afterward.
    bool Connect(const std::string& serverAddress, const std::string& sessionId,
                const std::string& mediaId, int timeoutMs, std::string* error);
    void Disconnect();

    // action: "play" | "pause" | "seek" | "status" | "record_start" |
    // "record_stop". position is 0..1 for "seek".
    bool Control(const std::string& action, float position,
                 PlaybackStatus* status, std::string* error);

private:
    void SignalReaderLoop();
    void DecodeAnnexBFrame(const std::vector<std::byte>& frame, uint32_t rtpTimestamp);
    void DecodeOpusFrame(const std::vector<std::byte>& frame, uint32_t rtpTimestamp);

    IFrameSink* m_sink;
    std::string m_sessionId;
    std::string m_mediaId;

    std::shared_ptr<rtc::PeerConnection> m_peerConnection;
    std::shared_ptr<rtc::Track> m_videoTrack;
    std::shared_ptr<rtc::Track> m_audioTrack;

    std::unique_ptr<avplayer::agent::v1::StreamingService::Stub> m_stub;
    std::unique_ptr<grpc::ClientContext> m_signalContext;
    std::unique_ptr<grpc::ClientReaderWriter<avplayer::agent::v1::SignalMessage,
                                              avplayer::agent::v1::SignalMessage>> m_signalStream;
    std::thread m_signalReaderThread;
    std::atomic<bool> m_running{false};

    AVCodecContext* m_codecContext{nullptr};
    OpusDecoder* m_audioDecoder{nullptr};
    AudioRender m_audioRender;
    std::mutex m_audioMutex;
    uint32_t m_videoPtsBase{0};
    uint32_t m_audioPtsBase{0};
    bool m_videoPtsBaseSet{false};
    bool m_audioPtsBaseSet{false};
};

}

#endif // AV_STREAMING_WEBRTCPLAYBACKSOURCE_H
