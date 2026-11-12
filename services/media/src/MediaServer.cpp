#include "MediaServer.h"

#include <filesystem>
#include <iostream>

namespace av {

namespace {
namespace fs = std::filesystem;
}

StreamingServiceImpl::StreamingServiceImpl(std::string storageRoot) : m_storageRoot(std::move(storageRoot)) {}

std::string StreamingServiceImpl::SessionKey(const std::string& sessionId, const std::string& mediaId) const {
    return sessionId + "/" + mediaId;
}

std::string StreamingServiceImpl::ResolveMediaPath(const std::string& sessionId, const std::string& mediaId) const {
    const fs::path directory = fs::path(m_storageRoot) / sessionId / mediaId;
    std::error_code errorCode;
    if (!fs::is_directory(directory, errorCode)) return {};
    for (const auto& entry : fs::directory_iterator(directory, errorCode)) {
        if (entry.path().stem() == "original") return entry.path().string();
    }
    return {};
}

grpc::Status StreamingServiceImpl::Signal(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<avplayer::agent::v1::SignalMessage, avplayer::agent::v1::SignalMessage>* stream) {
    using avplayer::agent::v1::SignalMessage;

    SignalMessage first;
    if (!stream->Read(&first) || first.kind() != "offer") {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "first Signal message must be an offer");
    }

    const std::string mediaPath = ResolveMediaPath(first.session_id(), first.media_id());
    if (mediaPath.empty()) {
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "media not found; upload it via MediaService first");
    }

    auto session = std::make_shared<MediaSession>(first.session_id(), first.media_id(), mediaPath);
    std::mutex writeMutex;
    session->SetSignalSender([&](const std::string& kind, const std::string& sdp,
                                  const std::string& candidate, const std::string& mid) {
        SignalMessage out;
        out.set_session_id(first.session_id());
        out.set_media_id(first.media_id());
        out.set_kind(kind);
        out.set_sdp(sdp);
        out.set_candidate(candidate);
        out.set_mid(mid);
        std::lock_guard<std::mutex> lock(writeMutex);
        stream->Write(out);
    });

    if (!session->HandleRemoteOffer(first.sdp())) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "failed to negotiate session");
    }

    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        m_sessions[SessionKey(first.session_id(), first.media_id())] = session;
    }

    SignalMessage message;
    while (stream->Read(&message)) {
        if (message.kind() == "ice") {
            session->AddRemoteCandidate(message.candidate(), message.mid());
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        m_sessions.erase(SessionKey(first.session_id(), first.media_id()));
    }
    return grpc::Status::OK;
}

grpc::Status StreamingServiceImpl::Control(grpc::ServerContext* context,
                                           const avplayer::agent::v1::ControlRequest* request,
                                           avplayer::agent::v1::ControlAck* response) {
    std::shared_ptr<MediaSession> session;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        const auto it = m_sessions.find(SessionKey(request->session_id(), request->media_id()));
        if (it != m_sessions.end()) session = it->second;
    }
    if (!session) {
        response->set_success(false);
        response->set_error("session not found; negotiate via Signal first");
        return grpc::Status::OK;
    }
    std::string error;
    MediaSession::PlaybackStatus status;
    const bool ok = session->Control(request->action(), request->position(), &status, &error);
    response->set_success(ok);
    response->set_error(error);
    response->set_duration_ms(status.durationMs);
    response->set_position_ms(status.positionMs);
    response->set_playing(status.playing);
    response->set_recording(status.recording);
    response->set_output_path(status.outputPath);
    return grpc::Status::OK;
}

std::unique_ptr<grpc::Server> StartMediaServer(const std::string& address, const std::string& storageRoot) {
    // Leaked intentionally: the service must outlive the grpc::Server, and
    // this process only ever starts one media_server for its lifetime.
    auto* service = new StreamingServiceImpl(storageRoot);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    builder.RegisterService(service);
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    if (server) std::cout << "[media_server] listening on " << address << std::endl;
    return server;
}

}
