#include "sql/sqlite_database.h"

#include <stdexcept>
#include <utility>

namespace mcp {
    namespace sql {
        namespace {
            std::string SqliteError(sqlite3* db) {
                return db == nullptr ? "sqlite database is not open" : sqlite3_errmsg(db);
            }
        }

        SqliteDatabase::SqliteDatabase(std::filesystem::path db_path)
            : db_path_(std::move(db_path)) {}

        SqliteDatabase::~SqliteDatabase() {
            Close();
        }

        SqliteDatabase::SqliteDatabase(SqliteDatabase&& other) noexcept
            : db_path_(std::move(other.db_path_)),
              db_(other.db_) {
            other.db_ = nullptr;
        }

        SqliteDatabase& SqliteDatabase::operator=(SqliteDatabase&& other) noexcept {
            if (this != &other) {
                Close();
                db_path_ = std::move(other.db_path_);
                db_ = other.db_;
                other.db_ = nullptr;
            }
            return *this;
        }

        void SqliteDatabase::Open() {
            if (db_ != nullptr) {
                return;
            }

            const auto parent = db_path_.parent_path();
            if (!parent.empty()) {
                std::filesystem::create_directories(parent);
            }

            sqlite3* db = nullptr;
            const int rc = sqlite3_open(db_path_.string().c_str(), &db);
            if (rc != SQLITE_OK) {
                const std::string error = SqliteError(db);
                if (db != nullptr) {
                    sqlite3_close(db);
                }
                throw std::runtime_error("failed to open sqlite database: " + error);
            }
            db_ = db;
        }

        void SqliteDatabase::Close() noexcept {
            if (db_ != nullptr) {
                sqlite3_close(db_);
                db_ = nullptr;
            }
        }

        bool SqliteDatabase::IsOpen() const {
            return db_ != nullptr;
        }

        void SqliteDatabase::Execute(const std::string& sql) {
            if (db_ == nullptr) {
                throw std::runtime_error("sqlite database is not open");
            }

            char* error_message = nullptr;
            const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error_message);
            if (rc != SQLITE_OK) {
                std::string error = error_message == nullptr ? SqliteError(db_) : error_message;
                sqlite3_free(error_message);
                throw std::runtime_error("sqlite execute failed: " + error);
            }
        }

        sqlite3* SqliteDatabase::Handle() const {
            return db_;
        }

        const std::filesystem::path& SqliteDatabase::Path() const {
            return db_path_;
        }
    }
}
