#pragma once
#include "persistence/MySql.h"
#include <filesystem>
#include <memory>
namespace mcp::sql
{
    class MySqlDatabase
    {
        std::filesystem::path label;
        std::unique_ptr<persistence::MySql> db;

    public:
        // Legacy config paths are accepted only as labels; no local DB is created.
        explicit MySqlDatabase(std::filesystem::path value) : label(std::move(value)) {}
        MySqlDatabase(MySqlDatabase &&) = default;
        MySqlDatabase &operator=(MySqlDatabase &&) = default;
        void Open()
        {
            if (!db)
                db = std::make_unique<persistence::MySql>();
        }
        void Close() noexcept { db.reset(); }
        bool IsOpen() const { return bool(db); }
        void Execute(std::string sql)
        {
            if (!db)
                throw std::runtime_error("database not open");
            sql = std::regex_replace(sql, std::regex("INTEGER PRIMARY KEY AUTOINCREMENT"), "BIGINT PRIMARY KEY AUTO_INCREMENT");
            sql = std::regex_replace(sql, std::regex("CREATE INDEX IF NOT EXISTS [^;]+;"), "");
            db->Query(sql);
        }
        persistence::MySql *Handle() const { return db.get(); }
        const std::filesystem::path &Path() const { return label; }
    };
}
