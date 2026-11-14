#ifndef TOOL_CALL_HISTORY_REPOSITORY_H
#define TOOL_CALL_HISTORY_REPOSITORY_H

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "sql/sqlite_database.h"

namespace mcp {
    namespace sql {
        struct ToolCallRecord {
            int64_t id = 0;
            std::string tool_name;
            std::string arguments_json;
            bool is_error = false;
            std::string result_json;
            std::string error_message;
            std::string started_at;
            std::string finished_at;
            int64_t duration_ms = 0;
        };

        class ToolCallHistoryRepository {
        public:
            explicit ToolCallHistoryRepository(std::filesystem::path db_path);

            void Initialize();
            int64_t Insert(const ToolCallRecord& record);
            std::vector<ToolCallRecord> ListRecent(int limit);
            std::vector<ToolCallRecord> FindRecent(const std::string& tool_name_filter,
                                                  bool errors_only,
                                                  int limit);

            const std::filesystem::path& DatabasePath() const;

        private:
            void EnsureInitializedLocked();

            SqliteDatabase db_;
            bool initialized_ = false;
            std::mutex mutex_;
        };
    }
}

#endif // TOOL_CALL_HISTORY_REPOSITORY_H
