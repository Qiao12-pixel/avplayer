#ifndef AV_STREAMING_MEDIASESSION_H
#define AV_STREAMING_MEDIASESSION_H

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <mutex>
#include <thread>

#include <rtc/rtc.hpp>

#include "Engine/AVSynchronizer.h"
#include "Reader/Interface/IFileReader.h"
#include "WebRtcAudioSink.h"
#include "WebRtcFrameSink.h"
#include "Writer/IRecordSession.h"

namespace av {

// One (session_id, media_id) playback: owns the existing local decode
// pipeline (FileReader -> AVSynchronizer, unchanged from the Qt desktop
// path) and a PeerConnection whose video track is fed by WebRtcFrameSink
// instead of a local Qt widget. See the project plan, Phase 2 Checkpoint B.
class MediaSession final : public IFileReader::Listener {
public:
    using SignalSender = std::function<void(const std::string& kind, const std::string& sdp,
                                             const std::string& candidate, const std::string& mid)>;

    MediaSession(std::string sessionId, std::string mediaId, std::string mediaPath);
    ~MediaSession() override;

    // Wires signaling output (answer SDP / local ICE candidates) to the
    // caller, who forwards them over the gRPC Signal stream.
    void SetSignalSender(SignalSender sender);

    // Processes a client offer, opens the local file, starts playback, and
    // (asynchronously, via SignalSender) emits the answer + ICE candidates.
    bool HandleRemoteOffer(const std::string& sdp);
    void AddRemoteCandidate(const std::string& candidate, const std::string& mid);

    struct PlaybackStatus {
        double durationMs{0.0};
        double positionMs{0.0};
        bool playing{false};
        bool recording{false};
        std::string outputPath;
    };

    // action: "play" | "pause" | "seek" | "status" | "record_start" |
    // "record_stop". position is 0..1 for "seek".
    bool Control(const std::string& action, float position,
                 PlaybackStatus* status, std::string* error);

    void OnFileReaderNotifyAudioSamples(std::shared_ptr<IAudioSamples>) override;
    void OnFileReaderNotifyVideoFrame(std::shared_ptr<IVideoFrame>) override;
    void OnFileReaderNotifyAudioFinished() override;
    void OnFileReaderNotifyVideoFinished() override;

private:
    void TickLoop();

    std::string m_sessionId;
    std::string m_mediaId;
    std::string m_mediaPath;
    SignalSender m_signalSender;

    std::shared_ptr<rtc::PeerConnection> m_peerConnection;
    bool m_answerSent{false};
    std::shared_ptr<rtc::Track> m_videoTrack;
    std::shared_ptr<rtc::Track> m_audioTrack;
    std::unique_ptr<WebRtcFrameSink> m_frameSink;
    std::unique_ptr<WebRtcAudioSink> m_audioSink;

    std::unique_ptr<IFileReader> m_fileReader;
    AVSynchronizer m_synchronizer;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_playing{false};
    std::atomic<double> m_lastPositionSeconds{0.0};
    std::thread m_tickThread;

    bool StartRecording(std::string* error);
    bool StopRecording(std::string* outputPath, std::string* error);

    mutable std::mutex m_recordMutex;
    std::unique_ptr<IRecordSession> m_recordSession;
    std::string m_recordingOutputPath;
};

}

#endif // AV_STREAMING_MEDIASESSION_H
