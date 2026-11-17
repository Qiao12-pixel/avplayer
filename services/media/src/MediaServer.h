#ifndef AV_STREAMING_MEDIASERVER_H
#define AV_STREAMING_MEDIASERVER_H

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>

#include <grpcpp/grpcpp.h>

#include "MediaSession.h"
#include "agent.grpc.pb.h"

namespace av {

// gRPC StreamingService server: Signal() negotiates one WebRTC session per
// call (see proto/agent.proto), Control() drives play/pause/seek on an
// already-negotiated session. Reuses the media files Phase 1's MediaService
// (services/agent) already stored under the shared storage root as
// <session_id>/<media_id>/original.*.
class StreamingServiceImpl final
    : public avplayer::agent::v1::StreamingService::Service {
public:
  using EventSink = std::function<void(const std::string &, const std::string &,
                                       const std::string &, const std::string &,
                                       nlohmann::json)>;
  explicit StreamingServiceImpl(std::string storageRoot, EventSink events = {});

  grpc::Status
  Signal(grpc::ServerContext *context,
         grpc::ServerReaderWriter<avplayer::agent::v1::SignalMessage,
                                  avplayer::agent::v1::SignalMessage> *stream)
      override;
  grpc::Status Control(grpc::ServerContext *context,
                       const avplayer::agent::v1::ControlRequest *request,
                       avplayer::agent::v1::ControlAck *response) override;

private:
  std::string ResolveMediaPath(const std::string &sessionId,
                               const std::string &mediaId) const;
  std::string SessionKey(const std::string &sessionId,
                         const std::string &mediaId) const;

  EventSink m_events;
  std::map<std::string, std::string> m_playbackIds;
  std::string m_storageRoot;
  std::mutex m_sessionsMutex;
  std::map<std::string, std::shared_ptr<MediaSession>> m_sessions;
};

} // namespace av

#endif // AV_STREAMING_MEDIASERVER_H
