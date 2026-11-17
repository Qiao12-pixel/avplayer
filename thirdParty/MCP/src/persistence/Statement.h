#pragma once
#include "MySql.h"
#include <map>
namespace persistence
{
    class Statement
    {
        MySql *db;
        std::string query;
        std::map<int, std::string> parameters;
        Rows rows;
        size_t position = 0;
        bool executed = false;
        void Execute()
        {
            if (executed)
                return;
            std::string sql;
            int index = 0;
            bool quote = false;
            for (size_t i = 0; i < query.size(); ++i)
            {
                char ch = query[i];
                if (ch == '\'' && (i == 0 || query[i - 1] != '\\'))
                    quote = !quote;
                if (ch == '?' && !quote)
                {
                    auto it = parameters.find(++index);
                    if (it == parameters.end())
                        throw std::invalid_argument("unbound SQL parameter");
                    sql += it->second;
                }
                else
                    sql += ch;
            }
            rows = db->Query(sql);
            executed = true;
        }

    public:
        Statement(MySql *connection, const std::string &sql) : db(connection), query(sql)
        {
            if (!db)
                throw std::invalid_argument("database not open");
        }
        void BindText(int index, const std::string &value) { parameters[index] = db->Quote(value); }
        void BindInt64(int index, int64_t value) { parameters[index] = std::to_string(value); }
        void BindDouble(int index, double value) { parameters[index] = std::to_string(value); }
        bool StepRow()
        {
            Execute();
            if (position >= rows.size())
                return false;
            ++position;
            return true;
        }
        void StepDone() { Execute(); }
        Statement *Get() { return this; }
        std::string Column(int n) const
        {
            if (!position || n < 0 || size_t(n) >= rows[position - 1].size())
                throw std::out_of_range("SQL column");
            return rows[position - 1][n];
        }
    };
    inline std::string ColumnText(Statement *stmt, int n) { return stmt->Column(n); }
    inline int64_t ColumnInt64(Statement *stmt, int n)
    {
        auto value = stmt->Column(n);
        return value.empty() ? 0 : std::stoll(value);
    }
    inline double ColumnDouble(Statement *stmt, int n)
    {
        auto value = stmt->Column(n);
        return value.empty() ? 0 : std::stod(value);
    }
}
