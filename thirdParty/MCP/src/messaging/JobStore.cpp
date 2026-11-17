#include "JobStore.h"
#include "persistence/Outbox.h"
#include "persistence/Schema.h"
#include <chrono>
namespace mcp::messaging {
using namespace persistence;
namespace {
constexpr unsigned MaxAttempts = 3;
bool terminal(const std::string &state) {
  return state == "succeeded" || state == "failed" || state == "cancelled";
}
} // namespace
JobStore::JobStore()
    : leaseSeconds(std::stoi(Env("MCP_JOB_LEASE_SECONDS", "15"))) {
  if (leaseSeconds < 3 || leaseSeconds > 300)
    throw std::invalid_argument("lease duration must be 3..300 seconds");
  EnsureSchema(db);
}
void JobStore::event(const Json &request, const Json &status) {
  Json value = {
      {"job_id", request.at("job_id")},
      {"session_id", request.at("session_id")},
      {"media_id", request.at("media_id")},
      {"task", request.at("task")},
      {"state", status.at("state")},
      {"worker", status.value("worker", "")},
      {"error", status.value("error", "")},
      {"execution_version", status.value("execution_version", uint64_t(0))},
      {"occurred_at_ms",
       std::chrono::duration_cast<std::chrono::milliseconds>(
           std::chrono::system_clock::now().time_since_epoch())
           .count()}};
  AddEvent(db, "mcp.kafka", request.at("job_id"),
           Env("MCP_KAFKA_TOPIC", "avplayer.media.events"), value);
}
Json JobStore::Submit(const Json &request) {
  std::lock_guard<std::mutex> lock(mutex);
  Transaction transaction(db);
  auto id = request.at("job_id").get<std::string>();
  db.Query(
      "SELECT singleton_id FROM mcp_admission WHERE singleton_id=1 FOR UPDATE");
  auto existing =
      db.Query("SELECT request_json,status_json FROM mcp_jobs WHERE job_id=" +
               db.Quote(id));
  if (!existing.empty()) {
    if (Json::parse(existing[0][0]) != request)
      throw std::invalid_argument("job_id input conflict");
    transaction.Commit();
    return Json::parse(existing[0][1]);
  }
  auto count = db.Query(
      "SELECT COUNT(*) FROM mcp_jobs WHERE state IN ('queued','running')");
  if (std::stoull(count[0][0]) >= std::stoull(Env("MCP_PENDING_LIMIT", "1024")))
    throw std::runtime_error("pending task limit reached");
  Json status = {{"success", true},
                 {"job_id", id},
                 {"state", "queued"},
                 {"worker", "rabbitmq"},
                 {"error", ""}};
  db.Query("INSERT IGNORE INTO "
           "mcp_jobs(job_id,session_id,media_id,queue_name,request_json,status_"
           "json) VALUES(" +
           db.Quote(id) + "," + db.Quote(request.at("session_id")) + "," +
           db.Quote(request.at("media_id")) + "," +
           db.Quote(Env("MCP_RABBIT_QUEUE", "avplayer.media.tasks")) + "," +
           db.Quote(request.dump()) + "," + db.Quote(status.dump()) + ")");
  bool inserted = db.Changes() == 1;
  auto rows =
      db.Query("SELECT request_json,status_json FROM mcp_jobs WHERE job_id=" +
               db.Quote(id) + " FOR UPDATE");
  if (rows.empty() || Json::parse(rows[0][0]) != request)
    throw std::invalid_argument("job_id input conflict");
  if (inserted) {
    event(request, status);
    AddEvent(db, "mcp.rabbit", id,
             Env("MCP_RABBIT_QUEUE", "avplayer.media.tasks"), {{"job_id", id}});
  }
  transaction.Commit();
  return Json::parse(rows[0][1]);
}
Json JobStore::Get(const std::string &id, const std::string &session,
                   bool cancel) {
  std::lock_guard<std::mutex> lock(mutex);
  Transaction transaction(db);
  auto rows = db.Query(
      "SELECT request_json,status_json,state FROM mcp_jobs WHERE job_id=" +
      db.Quote(id) + " AND session_id=" + db.Quote(session) + " FOR UPDATE");
  if (rows.empty()) {
    transaction.Commit();
    return {{"success", false}, {"error", "job not found"}};
  }
  auto status = Json::parse(rows[0][1]);
  if (cancel && !terminal(rows[0][2])) {
    status["state"] = "cancelled";
    status["success"] = false;
    status["error"] = "cancelled by caller";
    db.Query("UPDATE mcp_jobs SET "
             "cancel_requested=1,state='cancelled',status_json=" +
             db.Quote(status.dump()) +
             ",lease_owner=NULL,lease_until=NULL,execution_version=execution_"
             "version+1 WHERE job_id=" +
             db.Quote(id));
    event(Json::parse(rows[0][0]), status);
  }
  transaction.Commit();
  return status;
}
Json JobStore::Claim(const std::string &id) {
  std::lock_guard<std::mutex> lock(mutex);
  Transaction transaction(db);
  auto rows = db.Query(
      "SELECT request_json,state,execution_version,attempts,(lease_until IS "
      "NOT NULL AND lease_until>NOW(6)) FROM mcp_jobs WHERE job_id=" +
      db.Quote(id) + " FOR UPDATE");
  if (rows.empty() || terminal(rows[0][1])) {
    transaction.Commit();
    return nullptr;
  }
  if (rows[0][1] == "running" && rows[0][4] == "1") {
    transaction.Commit();
    return {{"busy", true}};
  }
  auto request = Json::parse(rows[0][0]);
  auto version = std::stoull(rows[0][2]) + 1;
  auto attempts = std::stoul(rows[0][3]);
  const bool exhausted = attempts >= MaxAttempts;
  if (!exhausted)
    ++attempts;
  Json status = {{"success", !exhausted},
                 {"job_id", id},
                 {"state", exhausted ? "failed" : "running"},
                 {"execution_version", version},
                 {"worker", owner},
                 {"error", exhausted ? "delivery attempts exhausted" : ""}};
  db.Query("UPDATE mcp_jobs SET state=" + db.Quote(status.at("state")) +
           ",status_json=" + db.Quote(status.dump()) + ",execution_version=" +
           std::to_string(version) + ",attempts=" + std::to_string(attempts) +
           (exhausted
                ? ",result_json=NULL,error_text=" +
                      db.Quote(status.at("error")) +
                      ",lease_owner=NULL,lease_until=NULL WHERE job_id="
                : ",lease_owner=" + db.Quote(owner) +
                      ",lease_until=TIMESTAMPADD(SECOND," +
                      std::to_string(leaseSeconds) +
                      ",NOW(6)) WHERE job_id=") +
           db.Quote(id));
  event(request, status);
  transaction.Commit();
  if (exhausted)
    return {{"exhausted", true}};
  request["execution_version"] = version;
  request["lease_owner"] = owner;
  return request;
}
bool JobStore::Renew(const std::string &id, uint64_t version) {
  std::lock_guard<std::mutex> lock(mutex);
  db.Query(
      "UPDATE mcp_jobs SET lease_until=TIMESTAMPADD(SECOND," +
      std::to_string(leaseSeconds) + ",NOW(6)) WHERE job_id=" + db.Quote(id) +
      " AND state='running' AND cancel_requested=0 AND lease_owner=" +
      db.Quote(owner) + " AND execution_version=" + std::to_string(version) +
      " AND lease_until>NOW(6)");
  return db.Changes() == 1;
}
bool JobStore::Cancelled(const std::string &id, uint64_t version) {
  std::lock_guard<std::mutex> lock(mutex);
  auto rows = db.Query(
      "SELECT (state='running' AND cancel_requested=0 AND lease_owner=" +
      db.Quote(owner) + " AND execution_version=" + std::to_string(version) +
      " AND lease_until>NOW(6)) FROM mcp_jobs WHERE job_id=" + db.Quote(id));
  return rows.empty() || rows[0][0] != "1";
}
JobStore::Completion JobStore::Complete(const std::string &id,
                                        uint64_t version, Json status) {
  std::lock_guard<std::mutex> lock(mutex);
  Transaction transaction(db);
  auto rows = db.Query(
      "SELECT request_json,attempts,queue_name FROM mcp_jobs WHERE job_id=" +
      db.Quote(id) +
      " AND state='running' AND cancel_requested=0 AND lease_owner=" +
      db.Quote(owner) + " AND execution_version=" + std::to_string(version) +
      " AND lease_until>NOW(6) FOR UPDATE");
  if (rows.empty()) {
    transaction.Commit();
    return Completion::stale;
  }
  status["execution_version"] = version;
  const auto request = Json::parse(rows[0][0]);
  const auto attempts = std::stoul(rows[0][1]);
  if (status.value("state", "") == "failed" && attempts < MaxAttempts) {
    const auto error = status.value("error", "media execution failed");
    status["state"] = "queued";
    status["success"] = true;
    status["worker"] = "rabbitmq";
    status["error"] = "attempt " + std::to_string(attempts) +
                      " failed; retry queued: " + error;
    db.Query("UPDATE mcp_jobs SET state='queued',status_json=" +
             db.Quote(status.dump()) + ",result_json=NULL,error_text=" +
             db.Quote(status["error"]) +
             ",execution_version=execution_version+1,lease_owner=NULL,"
             "lease_until=NULL WHERE job_id=" + db.Quote(id));
    event(request, status);
    AddEvent(db, "mcp.rabbit", id, rows[0][2], {{"job_id", id}});
    transaction.Commit();
    return Completion::retry_queued;
  }
  db.Query("UPDATE mcp_jobs SET state=" + db.Quote(status.at("state")) +
           ",status_json=" + db.Quote(status.dump()) + ",result_json=" +
           db.Quote(status.value("result", Json::object()).dump()) +
           ",error_text=" + db.Quote(status.value("error", "")) +
           ",lease_owner=NULL,lease_until=NULL WHERE job_id=" + db.Quote(id));
  event(request, status);
  transaction.Commit();
  return status.value("state", "") == "failed" ? Completion::exhausted
                                                  : Completion::completed;
}
std::vector<Json> JobStore::Pending() {
  std::lock_guard<std::mutex> lock(mutex);
  {
    Transaction transaction(db);
    auto rows =
        db.Query("SELECT job_id,request_json,status_json FROM mcp_jobs WHERE "
                 "queue_name=" +
                 db.Quote(Env("MCP_RABBIT_QUEUE", "avplayer.media.tasks")) +
                 " AND state='running' AND lease_until<=NOW(6) ORDER BY "
                 "lease_until LIMIT 16 FOR UPDATE SKIP LOCKED");
    for (auto &row : rows) {
      auto status = Json::parse(row[2]);
      status["state"] = "queued";
      status["error"] = "lease expired; awaiting another execution";
      db.Query("UPDATE mcp_jobs SET "
               "state='queued',lease_owner=NULL,lease_until=NULL,execution_"
               "version=execution_version+1,status_json=" +
               db.Quote(status.dump()) + " WHERE job_id=" + db.Quote(row[0]));
      event(Json::parse(row[1]), status);
      AddEvent(db, "mcp.rabbit", row[0],
               Env("MCP_RABBIT_QUEUE", "avplayer.media.tasks"),
               {{"job_id", row[0]}});
    }
    transaction.Commit();
  }
  return ClaimEvents(db, "mcp.rabbit", owner);
}

void JobStore::Published(const Json &event) {
  std::lock_guard<std::mutex> lock(mutex);
  EventDelivered(db, event, owner);
}
std::vector<Json> JobStore::Events() {
  std::lock_guard<std::mutex> lock(mutex);
  return ClaimEvents(db, "mcp.kafka", owner);
}
void JobStore::EventSent(const Json &event) {
  std::lock_guard<std::mutex> lock(mutex);
  EventDelivered(db, event, owner);
}
} // namespace mcp::messaging
