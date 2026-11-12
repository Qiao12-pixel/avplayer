// Measurement client for the WebRTC streaming path (see docs/benchmarks.md).
//
// It behaves like a browser: connects to StreamingService over gRPC, offers a
// recvonly H.264 + Opus session, and records what actually arrives. Output is
// line-oriented (`[bench] key=value`) so scripts/bench_metrics.py can parse it.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <grpcpp/grpcpp.h>
#include <rtc/rtc.hpp>

#include "agent.grpc.pb.h"

using avplayer::agent::v1::ControlAck;
using avplayer::agent::v1::ControlRequest;
using avplayer::agent::v1::SignalMessage;
using avplayer::agent::v1::StreamingService;
using Clock = std::chrono::steady_clock;

namespace {

double MsSince(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}

double Percentile(std::vector<double> values, double p) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const size_t index = static_cast<size_t>(p * static_cast<double>(values.size() - 1));
    return values[index];
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: streaming_smoke_client <server_address> <session_id> <media_id> "
                     "[measure_seconds]" << std::endl;
        return 2;
    }
    const std::string serverAddress = argv[1];
    const std::string sessionId = argv[2];
    const std::string mediaId = argv[3];
    const double measureSeconds = argc > 4 ? std::stod(argv[4]) : 6.0;

    const auto started = Clock::now();
    std::atomic<Clock::time_point::rep> iceRep{0};
    std::atomic<Clock::time_point::rep> firstVideoRep{0};
    std::atomic<uint64_t> videoBytes{0};
    std::atomic<uint64_t> videoPackets{0};
    std::atomic<uint64_t> audioBytes{0};
    std::atomic<uint64_t> audioPackets{0};
    std::atomic<bool> trackOpen{false};

    std::mutex intervalMutex;
    std::vector<double> videoIntervals;
    Clock::time_point lastVideo{};

    rtc::Configuration config;
    auto pc = std::make_shared<rtc::PeerConnection>(config);

    auto channel = grpc::CreateChannel(serverAddress, grpc::InsecureChannelCredentials());
    auto stub = StreamingService::NewStub(channel);
    grpc::ClientContext context;
    auto stream = stub->Signal(&context);

    pc->onLocalDescription([&](const rtc::Description& description) {
        SignalMessage message;
        message.set_session_id(sessionId);
        message.set_media_id(mediaId);
        message.set_kind("offer");
        message.set_sdp(std::string(description));
        stream->Write(message);
    });
    pc->onLocalCandidate([&](const rtc::Candidate& candidate) {
        SignalMessage message;
        message.set_session_id(sessionId);
        message.set_media_id(mediaId);
        message.set_kind("ice");
        message.set_candidate(std::string(candidate));
        message.set_mid(candidate.mid());
        stream->Write(message);
    });
    pc->onStateChange([&](rtc::PeerConnection::State state) {
        if (state == rtc::PeerConnection::State::Connected) {
            iceRep.store(Clock::now().time_since_epoch().count());
        }
    });

    rtc::Description::Video media("video", rtc::Description::Direction::RecvOnly);
    media.addH264Codec(102);
    auto localVideoTrack = pc->addTrack(media);
    localVideoTrack->setMediaHandler(std::make_shared<rtc::RtcpReceivingSession>());
    localVideoTrack->onOpen([&] { trackOpen = true; });
    localVideoTrack->onMessage([&](auto data) {
        if (!std::holds_alternative<rtc::binary>(data)) return;
        const auto& payload = std::get<rtc::binary>(data);
        const auto now = Clock::now();
        if (firstVideoRep.load() == 0) firstVideoRep.store(now.time_since_epoch().count());
        videoBytes += payload.size();
        videoPackets += 1;
        std::lock_guard<std::mutex> lock(intervalMutex);
        if (lastVideo.time_since_epoch().count() != 0) {
            videoIntervals.push_back(MsSince(lastVideo, now));
        }
        lastVideo = now;
    });

    rtc::Description::Audio audio("audio", rtc::Description::Direction::RecvOnly);
    audio.addOpusCodec(111);
    auto localAudioTrack = pc->addTrack(audio);
    localAudioTrack->setMediaHandler(std::make_shared<rtc::RtcpReceivingSession>());
    localAudioTrack->onMessage([&](auto data) {
        if (!std::holds_alternative<rtc::binary>(data)) return;
        audioBytes += std::get<rtc::binary>(data).size();
        audioPackets += 1;
    });
    pc->setLocalDescription();

    std::thread reader([&] {
        SignalMessage incoming;
        while (stream->Read(&incoming)) {
            if (incoming.kind() == "answer") {
                try {
                    pc->setRemoteDescription(rtc::Description(incoming.sdp(), rtc::Description::Type::Answer));
                } catch (const std::exception& error) {
                    std::cerr << "[client] setRemoteDescription failed: " << error.what() << std::endl;
                }
            } else if (incoming.kind() == "ice") {
                try {
                    pc->addRemoteCandidate(rtc::Candidate(incoming.candidate(), incoming.mid()));
                } catch (const std::exception&) {
                }
            }
        }
    });

    const auto firstFrameDeadline = started + std::chrono::seconds(15);
    while (firstVideoRep.load() == 0 && Clock::now() < firstFrameDeadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (firstVideoRep.load() == 0) {
        std::cout << "[bench] error=no_video_frame" << std::endl;
        context.TryCancel();
        stream->Finish();
        if (reader.joinable()) reader.join();
        return 1;
    }

    const Clock::time_point firstVideo{Clock::duration(firstVideoRep.load())};
    const Clock::time_point ice{Clock::duration(iceRep.load())};

    std::this_thread::sleep_for(std::chrono::duration<double>(measureSeconds));
    const double window = MsSince(firstVideo, Clock::now()) / 1000.0;

    std::vector<double> intervals;
    {
        std::lock_guard<std::mutex> lock(intervalMutex);
        intervals = videoIntervals;
    }
    const uint64_t frames = videoPackets.load();
    const uint64_t vbytes = videoBytes.load();
    const uint64_t abytes = audioBytes.load();

    std::cout << "[bench] track_open=" << (trackOpen ? 1 : 0) << std::endl;
    if (iceRep.load() != 0) {
        std::cout << "[bench] connect_ms=" << MsSince(started, ice) << std::endl;
        std::cout << "[bench] first_video_ms=" << MsSince(ice, firstVideo) << std::endl;
    }
    std::cout << "[bench] window_s=" << window << std::endl;
    std::cout << "[bench] video_packets=" << frames << std::endl;
    std::cout << "[bench] video_mbps=" << (vbytes * 8.0 / window / 1e6) << std::endl;
    std::cout << "[bench] video_pkt_per_s=" << (frames / window) << std::endl;
    std::cout << "[bench] audio_kbps=" << (abytes * 8.0 / window / 1e3) << std::endl;
    std::cout << "[bench] audio_pkt_per_s=" << (audioPackets.load() / window) << std::endl;
    if (!intervals.empty()) {
        std::cout << "[bench] pkt_interval_p50_ms=" << Percentile(intervals, 0.50) << std::endl;
        std::cout << "[bench] pkt_interval_p95_ms=" << Percentile(intervals, 0.95) << std::endl;
    }

    // Control-plane round trip, measured from a real gRPC client.
    std::vector<double> controlMs;
    for (int i = 0; i < 30; ++i) {
        grpc::ClientContext controlContext;
        ControlRequest request;
        request.set_session_id(sessionId);
        request.set_media_id(mediaId);
        request.set_action("status");
        ControlAck ack;
        const auto t0 = Clock::now();
        stub->Control(&controlContext, request, &ack);
        controlMs.push_back(MsSince(t0, Clock::now()));
    }
    std::cout << "[bench] control_p50_ms=" << Percentile(controlMs, 0.50) << std::endl;
    std::cout << "[bench] control_p95_ms=" << Percentile(controlMs, 0.95) << std::endl;

    // Server-side recording, so the report can state output codecs/dimensions.
    auto control = [&](const std::string& action) {
        grpc::ClientContext controlContext;
        ControlRequest request;
        request.set_session_id(sessionId);
        request.set_media_id(mediaId);
        request.set_action(action);
        ControlAck ack;
        stub->Control(&controlContext, request, &ack);
        return ack;
    };
    control("pause");
    control("play");
    control("record_start");
    std::this_thread::sleep_for(std::chrono::seconds(2));
    const auto stopped = control("record_stop");
    if (!stopped.output_path().empty()) {
        std::cout << "[bench] recording_path=" << stopped.output_path() << std::endl;
    }

    context.TryCancel();
    stream->Finish();
    if (reader.joinable()) reader.join();
    return 0;
}
