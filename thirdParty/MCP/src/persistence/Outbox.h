#pragma once
#include "MySql.h"
#include <nlohmann/json.hpp>
namespace persistence
{
    using Json = nlohmann::json;
    // Must be called inside the same transaction as the business state change.
    inline void AddEvent(MySql &db, const std::string &source, const std::string &key, const std::string &topic, Json payload)
    {
        auto event = payload.value("event_id", UniqueId());
        payload["event_id"] = event;
        db.Query("INSERT INTO event_outbox(event_id,source,aggregate_id,topic,payload) VALUES(" + db.Quote(event) + "," + db.Quote(source) + "," + db.Quote(key) + "," + db.Quote(topic) + "," + db.Quote(payload.dump()) + ")");
    }
    inline std::vector<Json> ClaimEvents(MySql &db, const std::string &source, const std::string &owner, int limit = 1)
    {
        Transaction txn(db);
        // Never overtake an earlier undelivered event for the same aggregate.
        auto rows = db.Query("SELECT e.event_id,e.aggregate_id,e.topic,e.payload,e.execution_version FROM event_outbox e WHERE e.source=" + db.Quote(source) + " AND e.delivered=0 AND (e.lease_until IS NULL OR e.lease_until<=NOW(6)) AND NOT EXISTS(SELECT 1 FROM event_outbox previous WHERE previous.source=e.source AND previous.aggregate_id=e.aggregate_id AND previous.delivered=0 AND previous.seq<e.seq) ORDER BY e.seq LIMIT " + std::to_string(limit) + " FOR UPDATE SKIP LOCKED");
        std::vector<Json> result;
        for (auto &row : rows)
        {
            auto version = std::stoull(row[4]) + 1;
            db.Query("UPDATE event_outbox SET lease_owner=" + db.Quote(owner) + ",lease_until=TIMESTAMPADD(SECOND,30,NOW(6)),execution_version=" + std::to_string(version) + " WHERE event_id=" + db.Quote(row[0]));
            result.push_back({{"id", row[0]}, {"key", row[1]}, {"topic", row[2]}, {"payload", row[3]}, {"version", version}});
        }
        txn.Commit();
        return result;
    }
    inline bool EventDelivered(MySql &db, const Json &event, const std::string &owner)
    {
        db.Query("UPDATE event_outbox SET delivered=1,lease_owner=NULL,lease_until=NULL WHERE event_id=" + db.Quote(event.at("id")) + " AND delivered=0 AND lease_owner=" + db.Quote(owner) + " AND execution_version=" + std::to_string(event.at("version").get<uint64_t>()) + " AND lease_until>NOW(6)");
        return db.Changes() == 1;
    }
}
