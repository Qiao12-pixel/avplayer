#ifndef SQLITE_DATABASE_H
#define SQLITE_DATABASE_H

#include <sqlite3.h>

#include <filesystem>
#include <string>

namespace mcp {
    namespace sql {
        class SqliteDatabase {
        public:
            explicit SqliteDatabase(std::filesystem::path db_path);
            ~SqliteDatabase();

            SqliteDatabase(const SqliteDatabase&) = delete;
            SqliteDatabase& operator=(const SqliteDatabase&) = delete;

            SqliteDatabase(SqliteDatabase&& other) noexcept;
            SqliteDatabase& operator=(SqliteDatabase&& other) noexcept;

            void Open();
            void Close() noexcept;
            bool IsOpen() const;

            void Execute(const std::string& sql);

            sqlite3* Handle() const;
            const std::filesystem::path& Path() const;

        private:
            std::filesystem::path db_path_;
            sqlite3* db_ = nullptr;
        };
    }
}

#endif // SQLITE_DATABASE_H
