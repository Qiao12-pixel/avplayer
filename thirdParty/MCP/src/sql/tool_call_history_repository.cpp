#include "sql/tool_call_history_repository.h"
#include "persistence/Statement.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>

namespace mcp {
namespace sql {
namespace {
using Statement = persistence::Statement;
using persistence::ColumnText;

} // namespace

ToolCallHistoryRepository::ToolCallHistoryRepository(
    std::filesystem::path db_path)
    : db_(std::move(db_path)) {}

void ToolCallHistoryRepository::Initialize() {
  std::lock_guard<std::mutex> lock(mutex_);
  EnsureInitializedLocked();
}

void ToolCallHistoryRepository::EnsureInitializedLocked() {
  if (initialized_) {
    return;
  }

  db_.Open();
  db_.Execute(R"SQL(
                CREATE TABLE IF NOT EXISTS tool_call_history (
                    id BIGINT PRIMARY KEY AUTO_INCREMENT,
                    tool_name VARCHAR(128) NOT NULL,
                    arguments_json MEDIUMTEXT NOT NULL,
                    is_error BIGINT NOT NULL DEFAULT 0,
                    result_json MEDIUMTEXT NOT NULL,
                    error_message MEDIUMTEXT NOT NULL,
                    started_at VARCHAR(64) NOT NULL,
                    finished_at VARCHAR(64) NOT NULL,
                    duration_ms BIGINT NOT NULL DEFAULT 0,
 KEY idx_tool_history_name(tool_name), KEY idx_tool_history_time(started_at)
                );
            )SQL");

  initialized_ = true;
}

int64_t ToolCallHistoryRepository::Insert(const ToolCallRecord &record) {
  std::lock_guard<std::mutex> lock(mutex_);
  EnsureInitializedLocked();

  Statement stmt(db_.Handle(), R"SQL(
                INSERT INTO tool_call_history (
                    tool_name,
                    arguments_json,
                    is_error,
                    result_json,
                    error_message,
                    started_at,
                    finished_at,
                    duration_ms
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?);
            )SQL");

  stmt.BindText(1, record.tool_name);
  stmt.BindText(2, record.arguments_json);
  stmt.BindInt64(3, record.is_error ? 1 : 0);
  stmt.BindText(4, record.result_json);
  stmt.BindText(5, record.error_message);
  stmt.BindText(6, record.started_at);
  stmt.BindText(7, record.finished_at);
  stmt.BindInt64(8, record.duration_ms);
  stmt.StepDone();

  return std::stoll(db_.Handle()->Query("SELECT LAST_INSERT_ID()")[0][0]);
}

std::vector<ToolCallRecord> ToolCallHistoryRepository::ListRecent(int limit) {
  std::lock_guard<std::mutex> lock(mutex_);
  EnsureInitializedLocked();

  const int normalized_limit = std::max(1, std::min(limit, 500));
  Statement stmt(db_.Handle(), R"SQL(
                SELECT
                    id,
                    tool_name,
                    arguments_json,
                    is_error,
                    result_json,
                    error_message,
                    started_at,
                    finished_at,
                    duration_ms
                FROM tool_call_history
                ORDER BY id DESC
                LIMIT ?;
            )SQL");
  stmt.BindInt64(1, normalized_limit);

  std::vector<ToolCallRecord> records;
  while (stmt.StepRow()) {
    ToolCallRecord record;
    record.id = persistence::ColumnInt64(stmt.Get(), 0);
    record.tool_name = ColumnText(stmt.Get(), 1);
    record.arguments_json = ColumnText(stmt.Get(), 2);
    record.is_error = persistence::ColumnInt64(stmt.Get(), 3) != 0;
    record.result_json = ColumnText(stmt.Get(), 4);
    record.error_message = ColumnText(stmt.Get(), 5);
    record.started_at = ColumnText(stmt.Get(), 6);
    record.finished_at = ColumnText(stmt.Get(), 7);
    record.duration_ms = persistence::ColumnInt64(stmt.Get(), 8);
    records.emplace_back(std::move(record));
  }
  return records;
}

std::vector<ToolCallRecord>
ToolCallHistoryRepository::FindRecent(const std::string &tool_name_filter,
                                      bool errors_only, int limit) {
  std::lock_guard<std::mutex> lock(mutex_);
  EnsureInitializedLocked();

  const int normalized_limit = std::max(1, std::min(limit, 500));
  Statement stmt(db_.Handle(), R"SQL(
                SELECT
                    id,
                    tool_name,
                    arguments_json,
                    is_error,
                    result_json,
                    error_message,
                    started_at,
                    finished_at,
                    duration_ms
                FROM tool_call_history
                WHERE (? = '' OR tool_name = ?)
                  AND (? = 0 OR is_error = 1)
                ORDER BY id DESC
                LIMIT ?;
            )SQL");
  stmt.BindText(1, tool_name_filter);
  stmt.BindText(2, tool_name_filter);
  stmt.BindInt64(3, errors_only ? 1 : 0);
  stmt.BindInt64(4, normalized_limit);

  std::vector<ToolCallRecord> records;
  while (stmt.StepRow()) {
    ToolCallRecord record;
    record.id = persistence::ColumnInt64(stmt.Get(), 0);
    record.tool_name = ColumnText(stmt.Get(), 1);
    record.arguments_json = ColumnText(stmt.Get(), 2);
    record.is_error = persistence::ColumnInt64(stmt.Get(), 3) != 0;
    record.result_json = ColumnText(stmt.Get(), 4);
    record.error_message = ColumnText(stmt.Get(), 5);
    record.started_at = ColumnText(stmt.Get(), 6);
    record.finished_at = ColumnText(stmt.Get(), 7);
    record.duration_ms = persistence::ColumnInt64(stmt.Get(), 8);
    records.emplace_back(std::move(record));
  }
  return records;
}

const std::filesystem::path &ToolCallHistoryRepository::DatabasePath() const {
  return db_.Path();
}
} // namespace sql
} // namespace mcp
