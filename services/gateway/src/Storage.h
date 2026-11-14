#pragma once
#include "agent.grpc.pb.h"
#include <hiredis/hiredis.h>
#include <mutex>
#include <mysql/mysql.h>
#include <nlohmann/json.hpp>
class Storage final : public avplayer::agent::v1::StorageService::Service {
  std::mutex mutex;
  MYSQL *mysql = nullptr;
  redisContext *redis = nullptr;
  std::string quote(const std::string &value);
  bool sql(const std::string &statement);
  nlohmann::json execute(const nlohmann::json &request);

public:
  Storage();
  ~Storage();
  grpc::Status Execute(grpc::ServerContext *,
                       const avplayer::agent::v1::Struct *,
                       avplayer::agent::v1::Struct *) override;
};
