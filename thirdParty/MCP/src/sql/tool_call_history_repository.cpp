#include "sql/tool_call_history_repository.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>



namespace mcp {
    namespace sql {
        namespace {
            class Statement {
            public:
                Statement(sqlite3* db, const std::string& sql) : db_(db) {
                    if (db_ == nullptr) {
                        throw std::runtime_error("sqlite database is not open");
                    }
                    const int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt_, nullptr);
                    if (rc != SQLITE_OK) {
                        throw std::runtime_error("sqlite prepare failed: " + std::string(sqlite3_errmsg(db_)));
                    }
                    // MCP_LOG_INFO("sqlite prepared successfully");
                }

                ~Statement() {
                    if (stmt_ != nullptr) {
                        sqlite3_finalize(stmt_);
                    }
                }

                Statement(const Statement&) = delete;
                Statement& operator=(const Statement&) = delete;

                sqlite3_stmt* Get() const {
                    return stmt_;
                }

                void BindText(int index, const std::string& value) {
                    const int rc = sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT);
                    if (rc != SQLITE_OK) {
                        throw std::runtime_error("sqlite bind text failed: " + std::string(sqlite3_errmsg(db_)));
                    }
                }

                void BindInt64(int index, int64_t value) {
                    const int rc = sqlite3_bind_int64(stmt_, index, value);
                    if (rc != SQLITE_OK) {
                        throw std::runtime_error("sqlite bind int64 failed: " + std::string(sqlite3_errmsg(db_)));
                    }
                }

                bool StepRow() {
                    const int rc = sqlite3_step(stmt_);
                    if (rc == SQLITE_ROW) {
                        return true;
                    }
                    if (rc == SQLITE_DONE) {
                        return false;
                    }
                    throw std::runtime_error("sqlite step failed: " + std::string(sqlite3_errmsg(db_)));
                }

                void StepDone() {
                    const int rc = sqlite3_step(stmt_);
                    if (rc != SQLITE_DONE) {
                        throw std::runtime_error("sqlite step failed: " + std::string(sqlite3_errmsg(db_)));
                    }
                }

            private:
                sqlite3* db_ = nullptr;
                sqlite3_stmt* stmt_ = nullptr;
            };

            std::string ColumnText(sqlite3_stmt* stmt, int column) {
                const auto* text = sqlite3_column_text(stmt, column);
                return text == nullptr ? "" : reinterpret_cast<const char*>(text);
            }
        }

        ToolCallHistoryRepository::ToolCallHistoryRepository(std::filesystem::path db_path)
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
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    tool_name TEXT NOT NULL,
                    arguments_json TEXT NOT NULL DEFAULT '{}',
                    is_error INTEGER NOT NULL DEFAULT 0,
                    result_json TEXT NOT NULL DEFAULT '{}',
                    error_message TEXT NOT NULL DEFAULT '',
                    started_at TEXT NOT NULL,
                    finished_at TEXT NOT NULL,
                    duration_ms INTEGER NOT NULL DEFAULT 0
                );
            )SQL");
            db_.Execute(R"SQL(
                CREATE INDEX IF NOT EXISTS idx_tool_call_history_started_at
                ON tool_call_history(started_at);
            )SQL");
            db_.Execute(R"SQL(
                CREATE INDEX IF NOT EXISTS idx_tool_call_history_tool_name
                ON tool_call_history(tool_name);
            )SQL");
            initialized_ = true;
        }

        int64_t ToolCallHistoryRepository::Insert(const ToolCallRecord& record) {
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

            return sqlite3_last_insert_rowid(db_.Handle());
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
                record.id = sqlite3_column_int64(stmt.Get(), 0);
                record.tool_name = ColumnText(stmt.Get(), 1);
                record.arguments_json = ColumnText(stmt.Get(), 2);
                record.is_error = sqlite3_column_int64(stmt.Get(), 3) != 0;
                record.result_json = ColumnText(stmt.Get(), 4);
                record.error_message = ColumnText(stmt.Get(), 5);
                record.started_at = ColumnText(stmt.Get(), 6);
                record.finished_at = ColumnText(stmt.Get(), 7);
                record.duration_ms = sqlite3_column_int64(stmt.Get(), 8);
                records.emplace_back(std::move(record));
            }
            return records;
        }

        std::vector<ToolCallRecord> ToolCallHistoryRepository::FindRecent(const std::string& tool_name_filter,
                                                                          bool errors_only,
                                                                          int limit) {
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
                record.id = sqlite3_column_int64(stmt.Get(), 0);
                record.tool_name = ColumnText(stmt.Get(), 1);
                record.arguments_json = ColumnText(stmt.Get(), 2);
                record.is_error = sqlite3_column_int64(stmt.Get(), 3) != 0;
                record.result_json = ColumnText(stmt.Get(), 4);
                record.error_message = ColumnText(stmt.Get(), 5);
                record.started_at = ColumnText(stmt.Get(), 6);
                record.finished_at = ColumnText(stmt.Get(), 7);
                record.duration_ms = sqlite3_column_int64(stmt.Get(), 8);
                records.emplace_back(std::move(record));
            }
            return records;
        }

        const std::filesystem::path& ToolCallHistoryRepository::DatabasePath() const {
            return db_.Path();
        }
    }
}
