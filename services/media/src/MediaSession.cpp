#include "MediaSession.h"

#include "Writer/IRecorder.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <utility>
#include <vector>

namespace av {

namespace {
constexpr uint32_t kVideoSsrc = 42;
constexpr uint8_t kH264PayloadType = 102;
constexpr uint32_t kAudioSsrc = 43;
constexpr uint8_t kOpusPayloadType = 111;

// Comma-separated STUN/TURN URLs, e.g.
//   AVPLAYER_ICE_SERVERS=stun:stun.l.google.com:19302,turn:turn.example.com:3478
std::string FindMid(const std::string& sdp, const std::string& kind) {
    const std::string marker = "m=" + kind + " ";
    const std::size_t media = sdp.find(marker);
    if (media == std::string::npos) return kind;
    const std::size_t nextMedia = sdp.find("\nm=", media + marker.size());
    const std::size_t mid = sdp.find("a=mid:", media);
    if (mid == std::string::npos || (nextMedia != std::string::npos && mid > nextMedia)) return kind;
    std::size_t end = sdp.find_first_of("\r\n", mid);
    if (end == std::string::npos) end = sdp.size();
    std::string value = sdp.substr(mid + 6, end - mid - 6);
    return value.empty() ? kind : value;
}

std::vector<rtc::IceServer> IceServersFromEnv() {
    std::vector<rtc::IceServer> servers;
    const char* raw = std::getenv("AVPLAYER_ICE_SERVERS");
    if (raw == nullptr) return servers;
    std::string value(raw);
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find(',', start);
        const std::string url = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!url.empty()) servers.emplace_back(url);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return servers;
}
}

MediaSession::MediaSession(std::string sessionId, std::string mediaId, std::string mediaPath)
    : m_sessionId(std::move(sessionId)), m_mediaId(std::move(mediaId)), m_mediaPath(std::move(mediaPath)),
      m_fileReader(IFileReader::Create()) {
    m_fileReader->SetListener(this);
}

MediaSession::~MediaSession() {
    m_running = false;
    if (m_tickThread.joinable()) m_tickThread.join();
    m_fileReader->Stop();
    {
        std::string path;
        std::string error;
        StopRecording(&path, &error);
    }
    m_synchronizer.Shutdown();
}

void MediaSession::SetSignalSender(SignalSender sender) { m_signalSender = std::move(sender); }

bool MediaSession::HandleRemoteOffer(const std::string& sdp) {
    m_answerSent = false;
    rtc::Configuration config;
    for (auto& server : IceServersFromEnv()) config.iceServers.push_back(std::move(server));
    m_peerConnection = std::make_shared<rtc::PeerConnection>(config);

    m_peerConnection->onLocalDescription([this](const rtc::Description& description) {
        // libdatachannel can report the local description more than once while
        // media sections are finalized; the client only accepts one answer.
        if (m_answerSent) return;
        m_answerSent = true;
        if (m_signalSender) m_signalSender("answer", std::string(description), "", "");
    });
    m_peerConnection->onLocalCandidate([this](const rtc::Candidate& candidate) {
        if (m_signalSender) m_signalSender("ice", "", std::string(candidate), candidate.mid());
    });
    m_peerConnection->onStateChange([](rtc::PeerConnection::State state) {
        std::cout << "[MediaSession] peer connection state=" << static_cast<int>(state) << std::endl;
    });

    const std::string videoMid = FindMid(sdp, "video");
    const std::string audioMid = FindMid(sdp, "audio");
    rtc::Description::Video media(videoMid, rtc::Description::Direction::SendOnly);
    media.addH264Codec(kH264PayloadType);
    media.addSSRC(kVideoSsrc, "video-send");
    m_videoTrack = m_peerConnection->addTrack(media);

    auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
        kVideoSsrc, "video-send", kH264PayloadType, rtc::H264RtpPacketizer::ClockRate);
    auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::Length, rtpConfig);
    auto srReporter = std::make_shared<rtc::RtcpSrReporter>(rtpConfig);
    packetizer->addToChain(srReporter);
    packetizer->addToChain(std::make_shared<rtc::RtcpNackResponder>());
    m_videoTrack->setMediaHandler(packetizer);

    m_frameSink = std::make_unique<WebRtcFrameSink>(m_videoTrack);
    m_synchronizer.SetVideoOutput(m_frameSink.get());

    rtc::Description::Audio audio(audioMid, rtc::Description::Direction::SendOnly);
    audio.addOpusCodec(kOpusPayloadType);
    audio.addSSRC(kAudioSsrc, "audio-send");
    m_audioTrack = m_peerConnection->addTrack(audio);
    auto audioRtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
        kAudioSsrc, "audio-send", kOpusPayloadType, rtc::OpusRtpPacketizer::DefaultClockRate);
    auto audioPacketizer = std::make_shared<rtc::OpusRtpPacketizer>(audioRtpConfig);
    audioPacketizer->addToChain(std::make_shared<rtc::RtcpSrReporter>(audioRtpConfig));
    m_audioTrack->setMediaHandler(audioPacketizer);
    m_audioSink = std::make_unique<WebRtcAudioSink>(m_audioTrack);
    m_synchronizer.SetAudioOutput(m_audioSink.get());
    m_synchronizer.Init();

    try {
        m_peerConnection->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Offer));
    } catch (const std::exception& error) {
        std::cerr << "[MediaSession] failed to set remote offer: " << error.what() << std::endl;
        return false;
    }
    m_peerConnection->setLocalDescription();

    std::string path = m_mediaPath;
    if (!m_fileReader->Open(path)) {
        std::cerr << "[MediaSession] failed to open media: " << m_mediaPath << std::endl;
        return false;
    }
    m_fileReader->Start();
    m_playing = true;
    m_running = true;
    m_tickThread = std::thread([this] { TickLoop(); });
    return true;
}

void MediaSession::AddRemoteCandidate(const std::string& candidate, const std::string& mid) {
    if (!m_peerConnection) return;
    try {
        m_peerConnection->addRemoteCandidate(rtc::Candidate(candidate, mid));
    } catch (const std::exception& error) {
        std::cerr << "[MediaSession] failed to add remote candidate: " << error.what() << std::endl;
    }
}

bool MediaSession::Control(const std::string& action, float position,
                           PlaybackStatus* status, std::string* error) {
    if (!m_fileReader) {
        if (error) *error = "媒体尚未就绪";
        return false;
    }
    if (action == "play") {
        m_fileReader->Start();
        m_playing = true;
    } else if (action == "pause") {
        m_fileReader->Paused();
        m_playing = false;
    } else if (action == "seek") {
        const float clamped = std::clamp(position, 0.0f, 1.0f);
        m_fileReader->SeekTo(clamped);
        m_lastPositionSeconds = clamped * m_fileReader->GetDuration();
    } else if (action == "record_start") {
        if (!StartRecording(error)) return false;
    } else if (action == "record_stop") {
        std::string outputPath;
        if (!StopRecording(&outputPath, error)) return false;
        if (status) status->outputPath = outputPath;
    } else if (action != "status") {
        if (error) *error = "未知播放操作：" + action;
        return false;
    }

    if (status) {
        status->durationMs = m_fileReader->GetDuration() * 1000.0;
        status->positionMs = m_lastPositionSeconds.load() * 1000.0;
        status->playing = m_playing.load();
        {
            std::lock_guard<std::mutex> lock(m_recordMutex);
            status->recording = m_recordSession && m_recordSession->IsRecording();
            status->outputPath = m_recordingOutputPath;
        }
    }
    return true;
}

bool MediaSession::StartRecording(std::string* error) {
    if (!m_fileReader) {
        if (error) *error = "媒体尚未就绪";
        return false;
    }
    // Read the dimensions BEFORE taking m_recordMutex. The decoder invokes our
    // frame callbacks (which take m_recordMutex) while holding its codec mutex,
    // so acquiring record -> codec here deadlocks against codec -> record.
    const int width = m_fileReader->GetVideoWidth();
    const int height = m_fileReader->GetVideoHeight();

    std::lock_guard<std::mutex> lock(m_recordMutex);
    if (m_recordSession && m_recordSession->IsRecording()) {
        if (error) *error = "已经在录制";
        return false;
    }
    if (width <= 0 || height <= 0) {
        if (error) *error = "视频尺寸无效，无法录制";
        return false;
    }

    RecorderConfig config;
    config.width = width;
    config.height = height;
    config.audioChannels = 2;
    config.audioSampleRate = 44100;
    m_recordSession.reset(IRecordSession::Create());
    if (!m_recordSession || !m_recordSession->Start(config)) {
        m_recordSession.reset();
        if (error) *error = "无法启动服务端录制";
        return false;
    }

    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    const std::filesystem::path directory = std::filesystem::path(m_mediaPath).parent_path();
    m_recordingOutputPath = (directory / ("recording-" + std::to_string(millis) + ".mp4")).string();
    std::cout << "[MediaSession] recording started: " << m_recordingOutputPath << std::endl;
    return true;
}

bool MediaSession::StopRecording(std::string* outputPath, std::string* error) {
    std::lock_guard<std::mutex> lock(m_recordMutex);
    if (!m_recordSession || !m_recordSession->IsRecording()) {
        if (outputPath) *outputPath = m_recordingOutputPath;
        return true;
    }
    const std::string path = m_recordingOutputPath;
    const bool saved = m_recordSession->StopAndSave(path);
    m_recordSession.reset();
    if (!saved) {
        if (error) *error = "服务端录制保存失败";
        return false;
    }
    if (outputPath) *outputPath = path;
    std::cout << "[MediaSession] recording saved: " << path << std::endl;
    return true;
}

void MediaSession::OnFileReaderNotifyAudioSamples(std::shared_ptr<IAudioSamples> audioSamples) {
    if (!audioSamples) return;
    if (audioSamples->flags & AVFrameFlag::KFlush) {
        m_lastPositionSeconds = 0.0;
    } else {
        m_lastPositionSeconds = audioSamples->GetTimeStamp();
    }
    {
        std::lock_guard<std::mutex> lock(m_recordMutex);
        if (m_recordSession && m_recordSession->IsRecording()) {
            m_recordSession->WriteAudioSamples(audioSamples);
        }
    }
    m_synchronizer.PushAudioSamples(audioSamples);
}

void MediaSession::OnFileReaderNotifyVideoFrame(std::shared_ptr<IVideoFrame> videoFrame) {
    if (!videoFrame) return;
    if (videoFrame->flags & AVFrameFlag::KFlush) {
        m_lastPositionSeconds = 0.0;
    } else if (videoFrame->GetTimeStamp() > 0.0f) {
        m_lastPositionSeconds = videoFrame->GetTimeStamp();
    }
    {
        std::lock_guard<std::mutex> lock(m_recordMutex);
        if (m_recordSession && m_recordSession->IsRecording()) {
            m_recordSession->WriteVideoFrame(videoFrame);
        }
    }
    m_synchronizer.PushVideoFrame(videoFrame);
}

void MediaSession::OnFileReaderNotifyAudioFinished() {}
void MediaSession::OnFileReaderNotifyVideoFinished() { m_playing = false; }

void MediaSession::TickLoop() {
    while (m_running) {
        m_synchronizer.Tick();
        // Tick() dispatches at most one frame, so the sleep bounds the stream
        // rate: 16 ms capped 1080p60 input at ~47 fps in benchmarks. 8 ms gives
        // enough headroom for 60 fps without busy-spinning.
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
}

}
