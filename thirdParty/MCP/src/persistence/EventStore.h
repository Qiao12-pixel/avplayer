#pragma once
#include "Schema.h"
#include "Outbox.h"
#include <mutex>
namespace persistence
{
    class EventStore
    {
        MySql db;
        std::mutex mutex;
        std::string owner = UniqueId();

    public:
        EventStore() { EnsureSchema(db); }
        void Append(const std::string &source, const std::string &key, const std::string &topic, Json payload)
        {
            std::lock_guard<std::mutex> lock(mutex);
            Transaction txn(db);
            AddEvent(db, source, key, topic, std::move(payload));
            txn.Commit();
        }
        std::vector<Json> Pending(const std::string &source)
        {
            std::lock_guard<std::mutex> lock(mutex);
            return ClaimEvents(db, source, owner);
        }
        bool Sent(const Json &event)
        {
            std::lock_guard<std::mutex> lock(mutex);
            return EventDelivered(db, event, owner);
        }
        bool Audit(const std::string &projection, const Json &value)
        {
            std::lock_guard<std::mutex> lock(mutex);
            Transaction txn(db);
            auto event = value.at("event_id").get<std::string>();
            db.Query("INSERT IGNORE INTO consumed_events(projection_name,event_id,payload) VALUES(" + db.Quote(projection) + "," + db.Quote(event) + "," + db.Quote(value.dump()) + ")");
            bool inserted = db.Changes() == 1;
            txn.Commit();
            return inserted;
        }
        void Playback(const std::string &projection, const Json &value)
        {
            std::lock_guard<std::mutex> lock(mutex);
            Transaction txn(db);
            auto q = [&](const std::string &v)
            { return db.Quote(v); };
            auto event = value.at("event_id").get<std::string>(), type = value.at("event_type").get<std::string>(), playback = value.at("playback_id").get<std::string>(), instance = value.at("instance_id").get<std::string>();
            if (value.value("schema_version", 0) != 1 || event.empty() || type.empty())
                throw std::invalid_argument("invalid event schema");
            auto time = value.at("occurred_at_ms").get<int64_t>();
            if (time < 0)
                throw std::invalid_argument("invalid event time");
            // Serializes a projection even if two consumer groups accidentally share it;
            // otherwise cross-partition inserts can deadlock counters in different order.
            db.Query("INSERT IGNORE INTO playback_instances(projection_name,instance_id,last_seen_ms) VALUES(" + q(projection) + ",'__projection_lock__',0)");
            db.Query("SELECT last_seen_ms FROM playback_instances WHERE projection_name=" + q(projection) + " AND instance_id='__projection_lock__' FOR UPDATE");
            db.Query("INSERT IGNORE INTO consumed_events(projection_name,event_id,payload) VALUES(" + q(projection) + "," + q(event) + "," + q(value.dump()) + ")");
            if (db.Changes())
            {
                db.Query("INSERT INTO playback_instances(projection_name,instance_id,last_seen_ms) VALUES(" + q(projection) + "," + q(instance) + "," + std::to_string(time) + ") ON DUPLICATE KEY UPDATE last_seen_ms=GREATEST(last_seen_ms,VALUES(last_seen_ms))");
                db.Query("INSERT INTO playback_event_counts(projection_name,event_type,event_count) VALUES(" + q(projection) + "," + q(type) + ",1) ON DUPLICATE KEY UPDATE event_count=event_count+1");
                db.Query("INSERT INTO playback_minute_counts(projection_name,minute_bucket,event_type,event_count) VALUES(" + q(projection) + "," + std::to_string(time / 60000) + "," + q(type) + ",1) ON DUPLICATE KEY UPDATE event_count=event_count+1");
                if (!playback.empty())
                {
                    db.Query("INSERT IGNORE INTO playback_sessions(projection_name,playback_id,session_id,media_id,instance_id) VALUES(" + q(projection) + "," + q(playback) + "," + q(value.at("session_id")) + "," + q(value.at("media_id")) + "," + q(instance) + ")");
                    if (type == "playback.connected")
                        db.Query("UPDATE playback_sessions SET connected_ms=COALESCE(connected_ms," + std::to_string(time) + ") WHERE projection_name=" + q(projection) + " AND playback_id=" + q(playback));
                    if (type == "playback.closed")
                        db.Query("UPDATE playback_sessions SET ended_ms=COALESCE(ended_ms," + std::to_string(time) + ") WHERE projection_name=" + q(projection) + " AND playback_id=" + q(playback));
                }
            }
            txn.Commit();
        }
        Json Statistics(const std::string &projection)
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto name = db.Quote(projection);
            Json counts = Json::object(), recent = Json::array();
            for (auto &row : db.Query("SELECT event_type,event_count FROM playback_event_counts WHERE projection_name=" + name))
                counts[row[0]] = std::stoll(row[1]);
            auto aggregate = db.Query("SELECT COUNT(*),COALESCE(SUM(CASE WHEN s.connected_ms IS NOT NULL AND s.ended_ms IS NULL AND i.last_seen_ms>=UNIX_TIMESTAMP(NOW(3))*1000-30000 THEN 1 ELSE 0 END),0),COALESCE(SUM(CASE WHEN s.connected_ms IS NOT NULL AND s.ended_ms IS NOT NULL THEN GREATEST(0,s.ended_ms-s.connected_ms) ELSE 0 END),0) FROM playback_sessions s LEFT JOIN playback_instances i ON i.projection_name=s.projection_name AND i.instance_id=s.instance_id WHERE s.projection_name=" + name);
            for (auto &row : db.Query("SELECT minute_bucket,event_type,event_count FROM playback_minute_counts WHERE projection_name=" + name + " AND minute_bucket>=FLOOR(UNIX_TIMESTAMP(NOW())/60)-4 ORDER BY minute_bucket,event_type"))
                recent.push_back({{"minute", std::stoll(row[0])}, {"event_type", row[1]}, {"count", std::stoll(row[2])}});
            auto rows = db.Query("SELECT COALESCE(MAX(last_seen_ms),0),CAST(UNIX_TIMESTAMP(NOW(3))*1000 AS SIGNED) FROM playback_instances WHERE projection_name=" + name);
            auto last = std::stoll(rows[0][0]), now = std::stoll(rows[0][1]);
            return {{"success", true}, {"event_counts", counts}, {"recent_minutes", recent}, {"sessions_observed", std::stoll(aggregate[0][0])}, {"active_connections", std::stoll(aggregate[0][1])}, {"completed_connection_ms", std::stoll(aggregate[0][2])}, {"last_event_at_ms", last}, {"statistics_lag_ms", last ? std::max<int64_t>(0, now - last) : -1}, {"active_estimate_timeout_ms", 30000}, {"projection", projection}};
        }
    };
}
