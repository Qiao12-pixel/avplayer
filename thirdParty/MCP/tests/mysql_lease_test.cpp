#include "messaging/JobStore.h"
#include "persistence/EventStore.h"
#include "persistence/Schema.h"
#include <iostream>
#include <stdexcept>

using namespace persistence;

namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

Json requestFor(const std::string &id) {
  return {{"job_id", id},
          {"session_id", "lease-test"},
          {"media_id", UniqueId()},
          {"task", "export_video"},
          {"arguments", {{"start_ms", 0}, {"end_ms", 1000}}},
          {"timeout_seconds", 30}};
}
} // namespace

int main() {
  try {
    mcp::messaging::JobStore first, second;
    MySql db;
    EnsureSchema(db);

    auto id = UniqueId();
    auto request = requestFor(id);
    first.Submit(request);
    auto a = first.Claim(id);
    const auto version = a.at("execution_version").get<uint64_t>();
    check(second.Claim(id).value("busy", false),
          "two workers claimed one live lease");
    check(first.Renew(id, version), "owner cannot renew");

    db.Query("UPDATE mcp_jobs SET lease_until=TIMESTAMPADD(SECOND,-1,NOW(6)) "
             "WHERE job_id=" + db.Quote(id));
    check(!first.Renew(id, version), "expired owner renewed");
    Json success = {{"job_id", id},
                    {"success", true},
                    {"state", "succeeded"},
                    {"result", {{"path", "new"}}}};
    check(first.Complete(id, version, success) ==
              mcp::messaging::JobStore::Completion::stale,
          "expired owner wrote before reclaim");
    auto b = second.Claim(id);
    const auto next = b.at("execution_version").get<uint64_t>();
    check(next > version, "fence token did not advance");
    check(first.Complete(id, version, success) ==
              mcp::messaging::JobStore::Completion::stale,
          "stale owner overwrote successor");
    check(second.Complete(id, next, success) ==
              mcp::messaging::JobStore::Completion::completed,
          "current owner cannot complete");
    check(first.Get(id, "lease-test").at("state") == "succeeded",
          "shared completion absent");

    auto cancelId = UniqueId();
    first.Submit(requestFor(cancelId));
    auto cancelled = first.Claim(cancelId);
    second.Get(cancelId, "lease-test", true);
    check(!first.Renew(cancelId,
                       cancelled.at("execution_version").get<uint64_t>()),
          "cancel did not revoke lease");
    check(first.Complete(cancelId,
                         cancelled.at("execution_version").get<uint64_t>(),
                         success) ==
              mcp::messaging::JobStore::Completion::stale,
          "cancelled task completed");

    auto retryId = UniqueId();
    auto retryRequest = requestFor(retryId);
    first.Submit(retryRequest);
    for (unsigned attempt = 1; attempt <= 3; ++attempt) {
      auto claimed = first.Claim(retryId);
      auto failed = Json{{"job_id", retryId},
                         {"success", false},
                         {"state", "failed"},
                         {"error", "temporary media failure"}};
      auto completion = first.Complete(
          retryId, claimed.at("execution_version").get<uint64_t>(), failed);
      if (attempt < 3) {
        check(completion ==
                  mcp::messaging::JobStore::Completion::retry_queued,
              "failed attempt was not requeued");
        check(first.Get(retryId, "lease-test").at("state") == "queued",
              "retry task is not queued");
      } else {
        check(completion ==
                  mcp::messaging::JobStore::Completion::exhausted,
              "final failed attempt was not terminal");
        check(first.Get(retryId, "lease-test").at("state") == "failed",
              "exhausted task did not fail");
      }
    }
    auto attempts = db.Query("SELECT attempts FROM mcp_jobs WHERE job_id=" +
                             db.Quote(retryId));
    check(attempts[0][0] == "3", "retry count is not three");
    auto retryMessages = db.Query(
        "SELECT COUNT(*) FROM event_outbox WHERE source='mcp.rabbit' AND "
        "aggregate_id=" + db.Quote(retryId));
    check(retryMessages[0][0] == "3",
          "initial dispatch and retry outbox entries are not atomic");

    auto crashId = UniqueId();
    first.Submit(requestFor(crashId));
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
      auto claimed = first.Claim(crashId);
      check(claimed.contains("execution_version"),
            "crash retry was not claimable");
      db.Query("UPDATE mcp_jobs SET lease_until=TIMESTAMPADD(SECOND,-1,NOW(6)) "
               "WHERE job_id=" + db.Quote(crashId));
    }
    check(first.Claim(crashId).value("exhausted", false),
          "crashed task exceeded the retry budget");
    auto crashState = db.Query(
        "SELECT state,attempts,lease_owner,lease_until FROM mcp_jobs WHERE "
        "job_id=" + db.Quote(crashId));
    check(crashState[0][0] == "failed" && crashState[0][1] == "3" &&
              crashState[0][2].empty() && crashState[0][3].empty(),
          "crash exhaustion did not leave a clean terminal task");

    EventStore audit;
    const auto projection = "audit-test-" + UniqueId().substr(0, 12);
    const auto eventId = UniqueId();
    const Json auditEvent = {{"event_id", eventId},
                             {"job_id", id},
                             {"state", "succeeded"}};
    check(audit.Audit(projection, auditEvent), "first audit insert was skipped");
    check(!audit.Audit(projection, auditEvent),
          "duplicate audit event was not deduplicated");

    auto source = "test-" + UniqueId().substr(0, 12);
    auto ownerA = UniqueId(), ownerB = UniqueId();
    {
      Transaction txn(db);
      AddEvent(db, source, id, "test.events", {{"test", true}});
      txn.Commit();
    }
    auto event = ClaimEvents(db, source, ownerA);
    check(event.size() == 1, "outbox not leased");
    check(ClaimEvents(db, source, ownerB).empty(), "outbox leased twice");
    db.Query("UPDATE event_outbox SET lease_until=TIMESTAMPADD(SECOND,-1,NOW(6)) "
             "WHERE event_id=" + db.Quote(event[0].at("id")));
    auto recovered = ClaimEvents(db, source, ownerB);
    check(!EventDelivered(db, event[0], ownerA),
          "stale publisher marked sent");
    check(EventDelivered(db, recovered[0], ownerB),
          "current publisher could not mark sent");

    std::cout << "PASS: lease fencing, cancellation, three failed attempts, "
                 "audit deduplication, publisher fencing\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
