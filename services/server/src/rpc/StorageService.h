#pragma once
#include "agent.grpc.pb.h"
#include "storage/mysql/MySqlRepository.h"
#include "storage/redis/RedisCache.h"
class StorageService final
    : public avplayer::agent::v1::StorageService::Service {
  std::mutex resultMutex;
  RedisCache cache;
  MySqlRepository repository;

public:
  grpc::Status Execute(grpc::ServerContext *,
                       const avplayer::agent::v1::Struct *,
                       avplayer::agent::v1::Struct *) override;
};
