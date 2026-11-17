#pragma once
#include "MySql.h"
namespace persistence {
inline void EnsureSchema(MySql &db) {
  db.Query(R"SQL(CREATE TABLE IF NOT EXISTS mcp_jobs (
 job_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 session_id VARCHAR(64) NOT NULL,
 media_id CHAR(32) NOT NULL,
 queue_name VARCHAR(128) NOT NULL DEFAULT 'avplayer.media.tasks',
 request_json JSON NOT NULL,
 status_json JSON NOT NULL,
 state ENUM('queued','running','succeeded','failed','cancelled') NOT NULL DEFAULT 'queued',
 result_json JSON,
 error_text TEXT,
 cancel_requested BOOLEAN NOT NULL DEFAULT FALSE,
 lease_owner VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin,
 lease_until DATETIME(6),
 execution_version BIGINT UNSIGNED NOT NULL DEFAULT 0,
 attempts INT UNSIGNED NOT NULL DEFAULT 0,
 created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
 updated_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
 KEY idx_job_lease(state,lease_until), KEY idx_job_session(session_id,created_at)
) ENGINE=InnoDB)SQL");
  db.Query(R"SQL(CREATE TABLE IF NOT EXISTS event_outbox (
 seq BIGINT UNSIGNED NOT NULL AUTO_INCREMENT UNIQUE,
 event_id CHAR(32) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
 source VARCHAR(32) NOT NULL,
 aggregate_id VARCHAR(128) NOT NULL,
 topic VARCHAR(128) NOT NULL,
 payload JSON NOT NULL,
 delivered BOOLEAN NOT NULL DEFAULT FALSE,
 lease_owner VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin,
 lease_until DATETIME(6),
 execution_version BIGINT UNSIGNED NOT NULL DEFAULT 0,
 created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
 KEY idx_outbox_dispatch(delivered,lease_until,created_at)
) ENGINE=InnoDB)SQL");
  db.Query(R"SQL(CREATE TABLE IF NOT EXISTS consumed_events (
 projection_name VARCHAR(64) NOT NULL,
 event_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
 payload JSON NOT NULL,
 consumed_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
 PRIMARY KEY(projection_name,event_id)
) ENGINE=InnoDB)SQL");
  db.Query(R"SQL(CREATE TABLE IF NOT EXISTS playback_event_counts (
 projection_name VARCHAR(64) NOT NULL,event_type VARCHAR(64) NOT NULL,
 event_count BIGINT UNSIGNED NOT NULL DEFAULT 0,
 PRIMARY KEY(projection_name,event_type)
) ENGINE=InnoDB)SQL");
  db.Query(R"SQL(CREATE TABLE IF NOT EXISTS playback_minute_counts (
 projection_name VARCHAR(64) NOT NULL,minute_bucket BIGINT NOT NULL,
 event_type VARCHAR(64) NOT NULL,event_count BIGINT UNSIGNED NOT NULL DEFAULT 0,
 PRIMARY KEY(projection_name,minute_bucket,event_type)
) ENGINE=InnoDB)SQL");
  db.Query(R"SQL(CREATE TABLE IF NOT EXISTS playback_sessions (
 projection_name VARCHAR(64) NOT NULL,playback_id VARCHAR(128) NOT NULL,
 session_id VARCHAR(64) NOT NULL,media_id CHAR(32) NOT NULL,
 instance_id VARCHAR(128) NOT NULL,connected_ms BIGINT,ended_ms BIGINT,
 PRIMARY KEY(projection_name,playback_id)
) ENGINE=InnoDB)SQL");
  db.Query(R"SQL(CREATE TABLE IF NOT EXISTS playback_instances (
 projection_name VARCHAR(64) NOT NULL,instance_id VARCHAR(128) NOT NULL,
 last_seen_ms BIGINT NOT NULL,
 PRIMARY KEY(projection_name,instance_id)
) ENGINE=InnoDB)SQL");
  db.Query(R"SQL(CREATE TABLE IF NOT EXISTS mcp_tool_history (
 id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
 tool_name VARCHAR(128) NOT NULL,arguments_json JSON NOT NULL,
 result_text MEDIUMTEXT NOT NULL,is_error BOOLEAN NOT NULL,
 duration_ms BIGINT NOT NULL,called_at DATETIME(6) DEFAULT CURRENT_TIMESTAMP(6)
) ENGINE=InnoDB)SQL");
  db.Query(
      R"SQL(CREATE TABLE IF NOT EXISTS mcp_admission (singleton_id INT PRIMARY KEY) ENGINE=InnoDB)SQL");
  db.Query(R"SQL(INSERT IGNORE INTO mcp_admission(singleton_id) VALUES(1))SQL");
  auto columns = db.Query("SELECT COUNT(*) FROM information_schema.columns "
                          "WHERE table_schema=DATABASE() AND "
                          "table_name='mcp_jobs' AND column_name='queue_name'");
  if (columns[0][0] == "0") {
    try {
      db.Query("ALTER TABLE mcp_jobs ADD COLUMN queue_name VARCHAR(128) NOT "
               "NULL DEFAULT 'avplayer.media.tasks'");
    } catch (...) {
      auto check =
          db.Query("SELECT COUNT(*) FROM information_schema.columns WHERE "
                   "table_schema=DATABASE() AND table_name='mcp_jobs' AND "
                   "column_name='queue_name'");
      if (check[0][0] == "0")
        throw;
    }
  }
}
} // namespace persistence
