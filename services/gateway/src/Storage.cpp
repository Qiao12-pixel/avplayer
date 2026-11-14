#include "Storage.h"
#include <cstdlib>
#include <regex>
using json = nlohmann::json;
static std::string setting(const char *k, const char *fallback = "") {
  auto v = getenv(k);
  return v ? v : fallback;
}
Storage::Storage() {
  auto dsn = setting("AVPLAYER_MYSQL_DSN");
  std::smatch m;
  if (std::regex_match(
          dsn, m,
          std::regex(
              R"(mysql://([^:]+):([^@]*)@([^:/]+)(?::([0-9]+))?/([^?]+).*)"))) {
    mysql = mysql_init(nullptr);
    unsigned int timeout = 2;
    mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    if (!mysql_real_connect(mysql, m[3].str().c_str(), m[1].str().c_str(),
                            m[2].str().c_str(), m[5].str().c_str(),
                            m[4].matched ? std::stoi(m[4]) : 3306, nullptr,
                            0)) {
      mysql_close(mysql);
      mysql = nullptr;
    } else
      mysql_set_character_set(mysql, "utf8mb4");
  }
  if (mysql)
    sql(R"SQL(CREATE TABLE IF NOT EXISTS sessions (
        session_id VARCHAR(64) PRIMARY KEY,
        created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
    ))SQL");
  if (mysql)
    sql(R"SQL(CREATE TABLE IF NOT EXISTS media (
        media_id CHAR(32) PRIMARY KEY,
        session_id VARCHAR(64) NOT NULL,
        file_name VARCHAR(255) NOT NULL DEFAULT '',
        size_bytes BIGINT NOT NULL DEFAULT 0,
        created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
        KEY idx_media_session (session_id)
    ))SQL");
  if (mysql)
    sql(R"SQL(CREATE TABLE IF NOT EXISTS transcripts (
        media_id CHAR(32) PRIMARY KEY,
        session_id VARCHAR(64) NOT NULL,
        segment_count INT NOT NULL DEFAULT 0,
        text MEDIUMTEXT NOT NULL,
        created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
    ))SQL");
  if (mysql)
    sql(R"SQL(CREATE TABLE IF NOT EXISTS segments (
        media_id CHAR(32) NOT NULL,
        idx INT NOT NULL,
        start_ms INT NOT NULL,
        end_ms INT NOT NULL,
        text TEXT NOT NULL,
        PRIMARY KEY (media_id, idx)
    ))SQL");
  if (mysql)
    sql(R"SQL(CREATE TABLE IF NOT EXISTS task_runs (
        id BIGINT AUTO_INCREMENT PRIMARY KEY,
        session_id VARCHAR(64) NOT NULL,
        media_id CHAR(32) NOT NULL DEFAULT '',
        task VARCHAR(64) NOT NULL,
        success TINYINT(1) NOT NULL,
        result MEDIUMTEXT,
        created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
        KEY idx_task_session (session_id, created_at)
    ))SQL");
  auto url = setting("AVPLAYER_REDIS_URL");
  if (std::regex_match(
          url, m,
          std::regex(R"(redis://([^:/]+)(?::([0-9]+))?(?:/([0-9]+))?)"))) {
    timeval t{0, 300000};
    redis = redisConnectWithTimeout(m[1].str().c_str(),
                                    m[2].matched ? std::stoi(m[2]) : 6379, t);
    if (redis && redis->err) {
      redisFree(redis);
      redis = nullptr;
    }
    if (redis) {
      redisSetTimeout(redis, t);
      if (m[3].matched) {
        auto r = redisCommand(redis, "SELECT %d", std::stoi(m[3]));
        if (r)
          freeReplyObject(r);
      }
    }
  }
}
Storage::~Storage() {
  if (mysql)
    mysql_close(mysql);
  if (redis)
    redisFree(redis);
}
std::string Storage::quote(const std::string &v) {
  std::string escaped(v.size() * 2 + 1, '\0');
  auto size =
      mysql_real_escape_string(mysql, escaped.data(), v.data(), v.size());
  escaped.resize(size);
  return "'" + escaped + "'";
}
bool Storage::sql(const std::string &s) {
  return mysql && mysql_query(mysql, s.c_str()) == 0;
}
json Storage::execute(const json &j) {
  auto action = j.at("action").get<std::string>();
  auto session = j.value("session_id", "");
  auto media = j.value("media_id", "");
  if (action.rfind("cache_", 0) == 0) {
    std::string key = "avplayer:transcript:" + session + ":" + media;
    if (!redis)
      return nullptr;
    redisReply *reply = nullptr;
    if (action == "cache_get")
      reply =
          (redisReply *)redisCommand(redis, "GET %b", key.data(), key.size());
    else if (action == "cache_put") {
      auto data = j.at("segments").dump();
      reply = (redisReply *)redisCommand(
          redis, "SET %b %b EX %d", key.data(), key.size(), data.data(),
          data.size(), std::stoi(setting("AVPLAYER_REDIS_TTL", "3600")));
    } else if (action == "cache_drop")
      reply =
          (redisReply *)redisCommand(redis, "DEL %b", key.data(), key.size());
    json value = nullptr;
    if (reply) {
      if (action == "cache_get" && reply->type == REDIS_REPLY_STRING)
        value =
            json::parse(std::string(reply->str, reply->len), nullptr, false);
      freeReplyObject(reply);
    }
    return value.is_array() ? value : json(nullptr);
  }
  if (!mysql)
    return nullptr;
  if (action == "record_session") {
    sql("INSERT IGNORE INTO sessions(session_id) VALUES(" + quote(session) +
        ")");
  } else if (action == "record_media") {
    sql("INSERT IGNORE INTO sessions(session_id) VALUES(" + quote(session) +
        ")");
    sql("INSERT INTO media(media_id,session_id,file_name,size_bytes) VALUES(" +
        quote(media) + "," + quote(session) + "," +
        quote(j.value("file_name", "")) + "," +
        std::to_string(j.value("size_bytes", 0LL)) +
        ") ON DUPLICATE KEY UPDATE "
        "file_name=VALUES(file_name),size_bytes=VALUES(size_bytes)");
  } else if (action == "record_task") {
    sql("INSERT INTO task_runs(session_id,media_id,task,success,result) "
        "VALUES(" +
        quote(session) + "," + quote(media) + "," + quote(j.value("task", "")) +
        "," + (j.value("success", false) ? "1" : "0") + "," +
        quote(j.value("result", "")) + ")");
  } else if (action == "save_transcript") {
    const auto &segments = j.at("segments");
    std::string text;
    for (auto &s : segments) {
      if (!text.empty())
        text += '\n';
      text += s.value("text", "");
    }
    bool ok = sql("START TRANSACTION");
    ok = ok &&
         sql("REPLACE INTO transcripts(media_id,session_id,segment_count,text) "
             "VALUES(" +
             quote(media) + "," + quote(session) + "," +
             std::to_string(segments.size()) + "," + quote(text) + ")");
    ok = ok && sql("DELETE FROM segments WHERE media_id=" + quote(media));
    int i = 0;
    for (auto &s : segments) {
      ok = ok && sql("INSERT INTO segments(media_id,idx,start_ms,end_ms,text) "
                     "VALUES(" +
                     quote(media) + "," + std::to_string(i++) + "," +
                     std::to_string(s.value("start_ms", 0)) + "," +
                     std::to_string(s.value("end_ms", 0)) + "," +
                     quote(s.value("text", "")) + ")");
    }
    sql(ok ? "COMMIT" : "ROLLBACK");
  } else if (action == "load_transcript" || action == "list_media") {
    bool transcript = action == "load_transcript";
    if (!sql(transcript
                 ? "SELECT s.start_ms,s.end_ms,s.text FROM segments s JOIN "
                   "transcripts t ON s.media_id=t.media_id WHERE s.media_id=" +
                       quote(media) +
                       (session.empty()
                            ? ""
                            : " AND t.session_id=" + quote(session)) +
                       " ORDER BY s.idx"
                 : "SELECT media_id,file_name,size_bytes,created_at FROM media "
                   "WHERE session_id=" +
                       quote(session) + " ORDER BY created_at DESC"))
      return nullptr;
    MYSQL_RES *result = mysql_store_result(mysql);
    if (!result)
      return nullptr;
    json rows = json::array();
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
      if (transcript)
        rows.push_back({{"start_ms", std::stoi(row[0])},
                        {"end_ms", std::stoi(row[1])},
                        {"text", row[2] ? row[2] : ""}});
      else
        rows.push_back({{"media_id", row[0]},
                        {"file_name", row[1]},
                        {"size_bytes", std::stoll(row[2])},
                        {"created_at", row[3]}});
    }
    mysql_free_result(result);
    return transcript && rows.empty() ? json(nullptr) : rows;
  }
  return nullptr;
}
grpc::Status Storage::Execute(grpc::ServerContext *,
                              const avplayer::agent::v1::Struct *req,
                              avplayer::agent::v1::Struct *res) {
  try {
    std::lock_guard<std::mutex> lock(mutex);
    res->set_json(execute(json::parse(req->json())).dump());
    return grpc::Status::OK;
  } catch (const std::exception &e) {
    return {grpc::StatusCode::INVALID_ARGUMENT, e.what()};
  }
}

