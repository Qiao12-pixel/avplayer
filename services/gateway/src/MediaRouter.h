#pragma once
#include "Registry.h"
#include "agent.grpc.pb.h"
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
class MediaRouter final
    : public avplayer::agent::v1::StreamingService::Service {
  av::Registry registry;
  struct Route {
    std::string endpoint;
    uint64_t token;
  };
  std::mutex mutex;
  std::map<std::string, Route> routes;
  uint64_t sequence = 0, tie = 0;
  std::string key(const std::string &session, const std::string &media) const;

public:
  grpc::Status Signal(
      grpc::ServerContext *,
      grpc::ServerReaderWriter<avplayer::agent::v1::SignalMessage,
                               avplayer::agent::v1::SignalMessage> *) override;
  grpc::Status Control(grpc::ServerContext *,
                       const avplayer::agent::v1::ControlRequest *,
                       avplayer::agent::v1::ControlAck *) override;
  nlohmann::json snapshot();
};
