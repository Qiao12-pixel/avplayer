#pragma once
#include "persistence/MySql.h"
#include <mutex>
#include <nlohmann/json.hpp>
namespace mcp::messaging {
using Json = nlohmann::json;
class JobStore {
  persistence::MySql db;
  std::mutex mutex;
  std::string owner = persistence::UniqueId();
  int leaseSeconds;
  void event(const Json &request, const Json &status);

public:
  enum class Completion { stale, completed, retry_queued, exhausted };

  JobStore();
  Json Submit(const Json &request);
  Json Get(const std::string &id, const std::string &session,
           bool cancel = false);
  Json Claim(const std::string &id);
  bool Renew(const std::string &id, uint64_t version);
  Completion Complete(const std::string &id, uint64_t version, Json status);
  bool Cancelled(const std::string &id, uint64_t version);
  std::vector<Json> Pending();
  void Published(const Json &event);
  std::vector<Json> Events();
  void EventSent(const Json &event);
};
} // namespace mcp::messaging
