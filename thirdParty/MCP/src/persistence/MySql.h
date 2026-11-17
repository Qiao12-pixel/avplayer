#pragma once
#include <cstdlib>
#include <mutex>
#include <mysql/mysql.h>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace persistence {
inline std::string Env(const char *key, const char *fallback = "") {
  auto v = std::getenv(key);
  return v ? v : fallback;
}
inline std::string UniqueId() {
  static std::mutex mutex;
  static std::mt19937_64 random(std::random_device{}());
  std::lock_guard<std::mutex> lock(mutex);
  char text[33];
  snprintf(text, sizeof(text), "%016llx%016llx", (unsigned long long)random(),
           (unsigned long long)random());
  return text;
}
using Rows = std::vector<std::vector<std::string>>;
// A connection is owned by one repository; callers serialize access. No silent
// transaction reconnect: callers must restart the complete transaction.
class MySql {
  MYSQL *db = nullptr;
  bool transaction = false;
  std::string dsn;
  void connect() {
    if (db) {
      mysql_close(db);
      db = nullptr;
    }
    std::smatch m;
    if (!std::regex_match(
            dsn, m,
            std::regex(
                R"(mysql://([^:]+):([^@]*)@([^:/]+)(?::([0-9]+))?/([^?]+).*)")))
      throw std::invalid_argument(
          "MySQL DSN required: mysql://user:password@host:port/database");
    db = mysql_init(nullptr);
    if (!db)
      throw std::runtime_error("MySQL initialization failed");
    unsigned int timeout = 3;
    mysql_options(db, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(db, MYSQL_OPT_READ_TIMEOUT, &timeout);
    mysql_options(db, MYSQL_OPT_WRITE_TIMEOUT, &timeout);
    if (!mysql_real_connect(db, m[3].str().c_str(), m[1].str().c_str(),
                            m[2].str().c_str(), m[5].str().c_str(),
                            m[4].matched ? std::stoi(m[4]) : 3306, nullptr,
                            0)) {
      auto code = mysql_errno(db);
      mysql_close(db);
      db = nullptr;
      throw std::runtime_error("MySQL connect failed (code " +
                               std::to_string(code) + ")");
    }
    if (mysql_set_character_set(db, "utf8mb4"))
      throw std::runtime_error("MySQL charset failed");
  }

public:
  explicit MySql(std::string value = Env("AVPLAYER_MYSQL_DSN"))
      : dsn(std::move(value)) {
    connect();
  }
  ~MySql() {
    if (db)
      mysql_close(db);
  }
  MySql(const MySql &) = delete;
  void Ready() {
    if (!db || mysql_ping(db)) {
      if (transaction)
        throw std::runtime_error("MySQL transaction connection lost");
      connect();
    }
  }
  std::string Quote(const std::string &value) {
    Ready();
    std::string escaped(value.size() * 2 + 1, '\0');
    auto size = mysql_real_escape_string(db, escaped.data(), value.data(),
                                         value.size());
    escaped.resize(size);
    return "'" + escaped + "'";
  }
  Rows Query(const std::string &query) {
    Ready();
    if (mysql_real_query(db, query.data(), query.size()))
      throw std::runtime_error("MySQL query failed (code " +
                               std::to_string(mysql_errno(db)) +
                               "): " + mysql_error(db));
    Rows rows;
    auto result = mysql_store_result(db);
    if (result) {
      MYSQL_ROW row;
      auto fields = mysql_num_fields(result);
      while ((row = mysql_fetch_row(result))) {
        auto lengths = mysql_fetch_lengths(result);
        std::vector<std::string> values;
        for (unsigned i = 0; i < fields; ++i)
          values.emplace_back(row[i] ? std::string(row[i], lengths[i])
                                     : std::string());
        rows.push_back(std::move(values));
      }
      mysql_free_result(result);
    } else if (mysql_field_count(db))
      throw std::runtime_error("MySQL result unavailable");
    return rows;
  }
  uint64_t Changes() const { return mysql_affected_rows(db); }
  void Begin() {
    Query("START TRANSACTION");
    transaction = true;
  }
  void Commit() {
    Query("COMMIT");
    transaction = false;
  }
  void Rollback() noexcept {
    if (db)
      mysql_query(db, "ROLLBACK");
    transaction = false;
  }
};
class Transaction {
  MySql &db;
  bool done = false;

public:
  explicit Transaction(MySql &d) : db(d) { db.Begin(); }
  ~Transaction() {
    if (!done)
      db.Rollback();
  }
  void Commit() {
    db.Commit();
    done = true;
  }
};
} // namespace persistence
