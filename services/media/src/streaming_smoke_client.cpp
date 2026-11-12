// Standalone verification client for Phase 2, Checkpoint B (see the project
// plan): connects to media_server's StreamingService over gRPC, negotiates
// a recvonly WebRTC video track (as a browser would), and confirms actual
// encoded bytes arrive for a real uploaded video — no Qt, no browser needed.
#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include <grpcpp/grpcpp.h>
#include <rtc/rtc.hpp>

#include "agent.grpc.pb.h"

using avplayer::agent::v1::ControlAck;
using avplayer::agent::v1::ControlRequest;
using avplayer::agent::v1::SignalMessage;
using avplayer::agent::v1::StreamingService;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: streaming_smoke_client <server_address> <session_id> <media_id>" << std::endl;
        return 2;
    }
    const std::string serverAddress = argv[1];
    const std::string sessionId = argv[2];
    const std::string mediaId = argv[3];

    std::atomic<size_t> bytesReceived{0};
    std::atomic<size_t> audioBytesReceived{0};
    std::atomic<bool> trackOpen{false};

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
    pc->onStateChange([](rtc::PeerConnection::State state) {
        std::cout << "[client] state=" << static_cast<int>(state) << std::endl;
    });

    // recvonly video transceiver, matching how a browser would request the
    // stream (it only accepts media, it does not send any of its own).
    // We are the offerer, so the track that will actually carry the
    // negotiated remote media is the one addTrack() returns here directly
    // (matches libdatachannel's own examples/media-receiver) — NOT a new
    // object delivered later via onTrack(), which is for tracks the remote
    // side adds that we did not already create ourselves.
    rtc::Description::Video media("video", rtc::Description::Direction::RecvOnly);
    media.addH264Codec(102);
    auto localVideoTrack = pc->addTrack(media);
    localVideoTrack->setMediaHandler(std::make_shared<rtc::RtcpReceivingSession>());
    localVideoTrack->onOpen([&] { trackOpen = true; });
    localVideoTrack->onMessage([&](auto data) {
        if (std::holds_alternative<rtc::binary>(data)) {
            bytesReceived += std::get<rtc::binary>(data).size();
        }
    });

    rtc::Description::Audio audio("audio", rtc::Description::Direction::RecvOnly);
    audio.addOpusCodec(111);
    auto localAudioTrack = pc->addTrack(audio);
    localAudioTrack->setMediaHandler(std::make_shared<rtc::RtcpReceivingSession>());
    localAudioTrack->onMessage([&](auto data) {
        if (std::holds_alternative<rtc::binary>(data)) {
            audioBytesReceived += std::get<rtc::binary>(data).size();
        }
    });
    pc->setLocalDescription();

    std::thread reader([&] {
        SignalMessage incoming;
        while (stream->Read(&incoming)) {
            std::cout << "[client] incoming kind=" << incoming.kind() << std::endl;
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

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (bytesReceived == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "trackOpen=" << trackOpen << " bytesReceived=" << bytesReceived
              << " audioBytesReceived=" << audioBytesReceived << std::endl;

    // Exercise Control()/status and server-side recording while we're here.
    auto control = [&](const std::string& action) {
        grpc::ClientContext controlContext;
        ControlRequest request;
        request.set_session_id(sessionId);
        request.set_media_id(mediaId);
        request.set_action(action);
        ControlAck ack;
        const auto status = stub->Control(&controlContext, request, &ack);
        std::cout << "control action=" << action << " rpc=" << status.ok()
                  << " success=" << ack.success()
                  << " duration_ms=" << ack.duration_ms()
                  << " position_ms=" << ack.position_ms()
                  << " playing=" << ack.playing()
                  << " recording=" << ack.recording()
                  << " output=" << ack.output_path() << std::endl;
        return ack;
    };
    control("status");
    control("pause");
    control("play");
    control("record_start");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    const auto stopped = control("record_stop");
    if (!stopped.output_path().empty()) {
        std::cout << "recording_path=" << stopped.output_path() << std::endl;
    }

    context.TryCancel();
    stream->Finish();
    if (reader.joinable()) reader.join();

    return bytesReceived > 0 ? 0 : 1;
}
