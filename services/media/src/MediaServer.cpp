#include "MediaServer.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <unistd.h>

namespace av {

namespace {
namespace fs = std::filesystem;
}

StreamingServiceImpl::StreamingServiceImpl(std::string storageRoot,
                                           EventSink events)
    : m_events(std::move(events)), m_storageRoot(std::move(storageRoot)) {}

std::string StreamingServiceImpl::SessionKey(const std::string &sessionId,
                                             const std::string &mediaId) const {
  return sessionId + "/" + mediaId;
}

std::string
StreamingServiceImpl::ResolveMediaPath(const std::string &sessionId,
                                       const std::string &mediaId) const {
  const fs::path directory = fs::path(m_storageRoot) / sessionId / mediaId;
  std::error_code errorCode;
  if (!fs::is_directory(directory, errorCode))
    return {};
  for (const auto &entry : fs::directory_iterator(directory, errorCode)) {
    if (entry.path().stem() == "original")
      return entry.path().string();
  }
  return {};
}

grpc::Status StreamingServiceImpl::Signal(
    grpc::ServerContext *context,
    grpc::ServerReaderWriter<avplayer::agent::v1::SignalMessage,
                             avplayer::agent::v1::SignalMessage> *stream) {
  using avplayer::agent::v1::SignalMessage;

  SignalMessage first;
  if (!stream->Read(&first) || first.kind() != "offer") {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "first Signal message must be an offer");
  }

  static std::atomic<uint64_t> sequence{0};
  auto playback =
      std::to_string(getpid()) + "-" +
      std::to_string(
          std::chrono::system_clock::now().time_since_epoch().count()) +
      "-" + std::to_string(++sequence);
  auto publishEvent = [this, playback, sessionId = first.session_id(),
               mediaId = first.media_id()](const std::string &type,
                                           nlohmann::json details =
                                               nlohmann::json::object()) {
    if (m_events)
      m_events(type, sessionId, mediaId, playback, std::move(details));
  };
  publishEvent("playback.requested");
  const std::string mediaPath =
      ResolveMediaPath(first.session_id(), first.media_id());
  if (mediaPath.empty()) {
    publishEvent("playback.error", {{"reason", "media_not_found"}});
    return grpc::Status(grpc::StatusCode::NOT_FOUND,
                        "media not found; upload it via MediaService first");
  }

  auto session = std::make_shared<MediaSession>(first.session_id(),
                                                first.media_id(), mediaPath);
  session->SetConnectionObserver(
      [publishEvent](const std::string &state) { publishEvent("playback." + state); });
  std::mutex writeMutex;
  session->SetSignalSender([&](const std::string &kind, const std::string &sdp,
                               const std::string &candidate,
                               const std::string &mid) {
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
    publishEvent("playback.error", {{"reason", "negotiation_failed"}});
    publishEvent("playback.closed", {{"reason", "negotiation_failed"}});
    return grpc::Status(grpc::StatusCode::INTERNAL,
                        "failed to negotiate session");
  }

  {
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    m_sessions[SessionKey(first.session_id(), first.media_id())] = session;
    m_playbackIds[SessionKey(first.session_id(), first.media_id())] = playback;
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
    m_playbackIds.erase(SessionKey(first.session_id(), first.media_id()));
  }
  publishEvent("playback.closed",
       {{"reason", context->IsCancelled() ? "cancelled_or_disconnected"
                                          : "signal_closed"}});
  return grpc::Status::OK;
}

grpc::Status StreamingServiceImpl::Control(
    grpc::ServerContext *context,
    const avplayer::agent::v1::ControlRequest *request,
    avplayer::agent::v1::ControlAck *response) {
  std::shared_ptr<MediaSession> session;
  std::string playback;
  {
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    const auto it =
        m_sessions.find(SessionKey(request->session_id(), request->media_id()));
    if (it != m_sessions.end()) {
      session = it->second;
      playback = m_playbackIds[it->first];
    }
  }
  if (!session) {
    response->set_success(false);
    response->set_error("session not found; negotiate via Signal first");
    return grpc::Status::OK;
  }
  std::string error;
  MediaSession::PlaybackStatus status;
  const bool ok =
      session->Control(request->action(), request->position(), &status, &error);
  response->set_success(ok);
  response->set_error(error);
  response->set_duration_ms(status.durationMs);
  response->set_position_ms(status.positionMs);
  response->set_playing(status.playing);
  response->set_recording(status.recording);
  response->set_output_path(status.outputPath);
  if (m_events && request->action() != "status") {
    auto action = request->action();
    auto type = !ok                        ? "playback.error"
                : action == "play"         ? "playback.resumed"
                : action == "pause"        ? "playback.paused"
                : action == "seek"         ? "playback.seeked"
                : action == "record_start" ? "recording.started"
                : action == "record_stop"  ? "recording.completed"
                                           : "playback.controlled";
    m_events(type, request->session_id(), request->media_id(), playback,
             {{"action", action},
              {"position_ms", status.positionMs},
              {"duration_ms", status.durationMs},
              {"success", ok},
              {"error", error}});
  }
  return grpc::Status::OK;
}

} // namespace av
