#include "WebRtcPlaybackSource.h"

#include <cstring>
#include <iostream>

#include <opus/opus.h>

extern "C" {
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace av {

namespace {
using avplayer::agent::v1::ControlAck;
using avplayer::agent::v1::ControlRequest;
using avplayer::agent::v1::SignalMessage;
using avplayer::agent::v1::StreamingService;
constexpr int kH264PayloadType = 102;
constexpr int kOpusPayloadType = 111;
}

WebRtcPlaybackSource::WebRtcPlaybackSource(IFrameSink* sink) : m_sink(sink) {}

WebRtcPlaybackSource::~WebRtcPlaybackSource() { Disconnect(); }

bool WebRtcPlaybackSource::Connect(const std::string& serverAddress, const std::string& sessionId,
                                   const std::string& mediaId, int timeoutMs, std::string* error) {
    m_sessionId = sessionId;
    m_mediaId = mediaId;

    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        if (error) *error = "本地没有可用的 H264 解码器";
        return false;
    }
    m_codecContext = avcodec_alloc_context3(codec);
    if (!m_codecContext || avcodec_open2(m_codecContext, codec, nullptr) < 0) {
        if (error) *error = "无法打开 H264 解码器";
        return false;
    }

    int opusError = 0;
    m_audioDecoder = opus_decoder_create(48000, 2, &opusError);
    if (opusError != OPUS_OK || !m_audioDecoder) {
        m_audioDecoder = nullptr;
        std::cerr << "[WebRtcPlaybackSource] Opus decoder unavailable; audio disabled" << std::endl;
    } else {
        m_audioRender.Init(2, 48000);
    }

    auto channel = grpc::CreateChannel(serverAddress, grpc::InsecureChannelCredentials());
    m_stub = StreamingService::NewStub(channel);
    m_signalContext = std::make_unique<grpc::ClientContext>();
    m_signalStream = m_stub->Signal(m_signalContext.get());

    rtc::Configuration config;
    m_peerConnection = std::make_shared<rtc::PeerConnection>(config);

    std::atomic<bool> negotiated{false};
    m_peerConnection->onLocalDescription([this, &negotiated](const rtc::Description& description) {
        SignalMessage message;
        message.set_session_id(m_sessionId);
        message.set_media_id(m_mediaId);
        message.set_kind("offer");
        message.set_sdp(std::string(description));
        m_signalStream->Write(message);
        negotiated = true;
    });
    m_peerConnection->onLocalCandidate([this](const rtc::Candidate& candidate) {
        SignalMessage message;
        message.set_session_id(m_sessionId);
        message.set_media_id(m_mediaId);
        message.set_kind("ice");
        message.set_candidate(std::string(candidate));
        message.set_mid(candidate.mid());
        m_signalStream->Write(message);
    });
    m_peerConnection->onStateChange([](rtc::PeerConnection::State state) {
        std::cout << "[WebRtcPlaybackSource] state=" << static_cast<int>(state) << std::endl;
    });

    rtc::Description::Video media("video", rtc::Description::Direction::RecvOnly);
    media.addH264Codec(kH264PayloadType);
    m_videoTrack = m_peerConnection->addTrack(media);
    m_videoTrack->setMediaHandler(
        std::make_shared<rtc::H264RtpDepacketizer>(rtc::NalUnit::Separator::StartSequence));
    m_videoTrack->onFrame([this](rtc::binary data, rtc::FrameInfo info) {
        DecodeAnnexBFrame(data, info.timestamp);
    });

    rtc::Description::Audio audio("audio", rtc::Description::Direction::RecvOnly);
    audio.addOpusCodec(kOpusPayloadType);
    m_audioTrack = m_peerConnection->addTrack(audio);
    m_audioTrack->setMediaHandler(std::make_shared<rtc::OpusRtpDepacketizer>());
    m_audioTrack->onFrame([this](rtc::binary data, rtc::FrameInfo info) {
        DecodeOpusFrame(data, info.timestamp);
    });

    m_peerConnection->setLocalDescription();

    m_running = true;
    m_signalReaderThread = std::thread([this] { SignalReaderLoop(); });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!negotiated && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!negotiated) {
        if (error) *error = "与媒体服务端协商超时";
        return false;
    }
    return true;
}

void WebRtcPlaybackSource::SignalReaderLoop() {
    SignalMessage incoming;
    while (m_running && m_signalStream->Read(&incoming)) {
        if (incoming.kind() == "answer") {
            try {
                m_peerConnection->setRemoteDescription(
                    rtc::Description(incoming.sdp(), rtc::Description::Type::Answer));
            } catch (const std::exception& error) {
                std::cerr << "[WebRtcPlaybackSource] setRemoteDescription failed: " << error.what() << std::endl;
            }
        } else if (incoming.kind() == "ice") {
            try {
                m_peerConnection->addRemoteCandidate(rtc::Candidate(incoming.candidate(), incoming.mid()));
            } catch (const std::exception&) {
            }
        }
    }
}

void WebRtcPlaybackSource::Disconnect() {
    if (!m_running.exchange(false)) return;
    if (m_signalContext) m_signalContext->TryCancel();
    if (m_signalStream) m_signalStream->Finish();
    if (m_signalReaderThread.joinable()) m_signalReaderThread.join();
    m_videoTrack.reset();
    m_audioTrack.reset();
    m_peerConnection.reset();
    if (m_audioDecoder) {
        opus_decoder_destroy(m_audioDecoder);
        m_audioDecoder = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(m_audioMutex);
        m_audioRender.Stop();
    }
    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
        m_codecContext = nullptr;
    }
    m_videoPtsBaseSet = false;
    m_audioPtsBaseSet = false;
}

bool WebRtcPlaybackSource::Control(const std::string& action, float position,
                                   PlaybackStatus* status, std::string* error) {
    if (status) *status = PlaybackStatus{};
    if (!m_stub) {
        if (error) *error = "尚未连接媒体服务端";
        return false;
    }
    if (action == "pause" || action == "seek") {
        // Discard audio queued before the pause/seek so it does not play over
        // the new position.
        std::lock_guard<std::mutex> lock(m_audioMutex);
        m_audioRender.Reset();
    }
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(300));
    ControlRequest request;
    request.set_session_id(m_sessionId);
    request.set_media_id(m_mediaId);
    request.set_action(action);
    request.set_position(position);
    ControlAck ack;
    const auto rpcStatus = m_stub->Control(&context, request, &ack);
    if (!rpcStatus.ok()) {
        if (error) *error = rpcStatus.error_message();
        return false;
    }
    if (status) {
        status->success = ack.success();
        status->error = ack.error();
        status->durationMs = ack.duration_ms();
        status->positionMs = ack.position_ms();
        status->playing = ack.playing();
        status->recording = ack.recording();
        status->outputPath = ack.output_path();
    }
    if (!ack.success() && error) *error = ack.error();
    return ack.success();
}

void WebRtcPlaybackSource::DecodeAnnexBFrame(const std::vector<std::byte>& frame, uint32_t rtpTimestamp) {
    if (!m_codecContext || frame.empty()) return;
    if (!m_videoPtsBaseSet) {
        m_videoPtsBase = rtpTimestamp;
        m_videoPtsBaseSet = true;
    }

    AVPacket* packet = av_packet_alloc();
    if (!packet) return;
    if (av_new_packet(packet, static_cast<int>(frame.size())) < 0) {
        av_packet_free(&packet);
        return;
    }
    std::memcpy(packet->data, frame.data(), frame.size());
    packet->pts = static_cast<int64_t>(rtpTimestamp - m_videoPtsBase);

    if (avcodec_send_packet(m_codecContext, packet) < 0) {
        av_packet_free(&packet);
        return;
    }
    av_packet_free(&packet);

    AVFrame* decoded = av_frame_alloc();
    if (!decoded) return;
    while (avcodec_receive_frame(m_codecContext, decoded) == 0) {
        const int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, decoded->width, decoded->height, 1);
        if (numBytes <= 0) continue;
        std::shared_ptr<uint8_t> buffer(new uint8_t[numBytes], std::default_delete<uint8_t[]>());
        bool ok;
        if (decoded->format == AV_PIX_FMT_YUV420P) {
            ok = av_image_copy_to_buffer(buffer.get(), numBytes, decoded->data, decoded->linesize,
                                         AV_PIX_FMT_YUV420P, decoded->width, decoded->height, 1) >= 0;
        } else {
            SwsContext* sws = sws_getContext(decoded->width, decoded->height,
                                              static_cast<AVPixelFormat>(decoded->format), decoded->width,
                                              decoded->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr,
                                              nullptr, nullptr);
            uint8_t* planes[4]{};
            int linesizes[4]{};
            ok = sws && av_image_fill_arrays(planes, linesizes, buffer.get(), AV_PIX_FMT_YUV420P,
                                              decoded->width, decoded->height, 1) >= 0 &&
                 sws_scale(sws, decoded->data, decoded->linesize, 0, decoded->height, planes, linesizes) > 0;
            if (sws) sws_freeContext(sws);
        }
        if (ok && m_sink) {
            auto videoFrame = std::make_shared<IVideoFrame>();
            videoFrame->width = decoded->width;
            videoFrame->height = decoded->height;
            videoFrame->data = std::move(buffer);
            videoFrame->pts = decoded->pts;
            videoFrame->timebaseNum = 1;
            videoFrame->timebaseDen = 90000;
            m_sink->PushFrame(videoFrame);
        }
    }
    av_frame_free(&decoded);
}

void WebRtcPlaybackSource::DecodeOpusFrame(const std::vector<std::byte>& frame, uint32_t rtpTimestamp) {
    if (!m_audioDecoder || frame.empty()) return;
    if (!m_audioPtsBaseSet) {
        m_audioPtsBase = rtpTimestamp;
        m_audioPtsBaseSet = true;
    }
    int16_t pcm[5760 * 2];
    const int decodedSamples = opus_decode(m_audioDecoder,
                                           reinterpret_cast<const unsigned char*>(frame.data()),
                                           static_cast<opus_int32>(frame.size()), pcm, 5760, 0);
    if (decodedSamples <= 0) return;

    auto samples = std::make_shared<IAudioSamples>();
    samples->channels = 2;
    samples->sampleRate = 48000;
    samples->pts = static_cast<int64_t>(rtpTimestamp - m_audioPtsBase);
    samples->duration = decodedSamples;
    samples->timebaseNum = 1;
    samples->timebaseDen = 48000;
    samples->pcmData.assign(pcm, pcm + static_cast<size_t>(decodedSamples) * 2);
    std::lock_guard<std::mutex> lock(m_audioMutex);
    m_audioRender.PushSamples(samples);
}

}
