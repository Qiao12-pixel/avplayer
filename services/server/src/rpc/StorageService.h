#pragma once
#include "agent.grpc.pb.h"
#include "events/PlaybackEvents.h"
#include "storage/mysql/MySqlRepository.h"
#include "storage/redis/RedisCache.h"
class StorageService final
    : public avplayer::agent::v1::StorageService::Service {
  avserver::PlaybackEvents *events;
  std::mutex resultMutex;
  RedisCache cache;
  MySqlRepository repository;

public:
  explicit StorageService(avserver::PlaybackEvents *sink = nullptr)
      : events(sink) {}
  grpc::Status Execute(grpc::ServerContext *,
                       const avplayer::agent::v1::Struct *,
                       avplayer::agent::v1::Struct *) override;
};
