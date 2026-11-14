#include "job_agent/job_agent_database.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <utility>

namespace mcp {
namespace job_agent {

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
    }

    ~Statement() {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    sqlite3_stmt* Get() const { return stmt_; }

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

    void BindDouble(int index, double value) {
        const int rc = sqlite3_bind_double(stmt_, index, value);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("sqlite bind double failed: " + std::string(sqlite3_errmsg(db_)));
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

} // anonymous namespace

JobAgentDatabase::JobAgentDatabase(std::filesystem::path db_path)
    : db_(std::move(db_path)) {}

void JobAgentDatabase::Initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    EnsureInitialized();
}

void JobAgentDatabase::EnsureInitialized() {
    if (initialized_) {
        return;
    }

    db_.Open();
    db_.Execute(R"SQL(
        CREATE TABLE IF NOT EXISTS resume_profiles (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT,
            education TEXT,
            skills TEXT,
            projects TEXT,
            raw_text TEXT NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );
    )SQL");
    db_.Execute(R"SQL(
        CREATE TABLE IF NOT EXISTS job_posts (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            company TEXT,
            position TEXT,
            jd_text TEXT NOT NULL,
            required_skills TEXT,
            preferred_skills TEXT,
            responsibilities TEXT,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );
    )SQL");
    db_.Execute(R"SQL(
        CREATE TABLE IF NOT EXISTS match_results (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            resume_id INTEGER NOT NULL,
            job_id INTEGER NOT NULL,
            match_score REAL,
            matched_skills TEXT,
            missing_skills TEXT,
            strengths TEXT,
            weaknesses TEXT,
            suggestions TEXT,
            report TEXT,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (resume_id) REFERENCES resume_profiles(id),
            FOREIGN KEY (job_id) REFERENCES job_posts(id)
        );
    )SQL");
    db_.Execute(R"SQL(
        CREATE TABLE IF NOT EXISTS interview_records (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            resume_id INTEGER NOT NULL,
            job_id INTEGER NOT NULL,
            question TEXT NOT NULL,
            answer TEXT,
            feedback TEXT,
            score REAL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (resume_id) REFERENCES resume_profiles(id),
            FOREIGN KEY (job_id) REFERENCES job_posts(id)
        );
    )SQL");
    db_.Execute(R"SQL(
        CREATE TABLE IF NOT EXISTS application_records (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            company TEXT NOT NULL,
            position TEXT NOT NULL,
            status TEXT NOT NULL DEFAULT 'draft',
            resume_id INTEGER,
            job_id INTEGER,
            notes TEXT,
            apply_date TEXT,
            interview_date TEXT,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (resume_id) REFERENCES resume_profiles(id),
            FOREIGN KEY (job_id) REFERENCES job_posts(id)
        );
    )SQL");
    initialized_ = true;
}

// -----------------------------------------------------------------------
// Resume / Job / Match
// -----------------------------------------------------------------------

int64_t JobAgentDatabase::SaveResumeProfile(const json& profile, const std::string& raw_text) {
    std::lock_guard<std::mutex> lock(mutex_);
    EnsureInitialized();

    Statement stmt(db_.Handle(), R"SQL(
        INSERT INTO resume_profiles (name, education, skills, projects, raw_text)
        VALUES (?, ?, ?, ?, ?);
    )SQL");

    stmt.BindText(1, profile.value("name", "Unknown"));
    stmt.BindText(2, profile.contains("education") ? profile["education"].dump() : "[]");
    stmt.BindText(3, profile.contains("skills") ? profile["skills"].dump() : "[]");
    stmt.BindText(4, profile.contains("projects") ? profile["projects"].dump() : "[]");
    stmt.BindText(5, raw_text);
    stmt.StepDone();

    return sqlite3_last_insert_rowid(db_.Handle());
}

int64_t JobAgentDatabase::SaveJobProfile(const json& profile, const std::string& jd_text) {
    std::lock_guard<std::mutex> lock(mutex_);
    EnsureInitialized();

    Statement stmt(db_.Handle(), R"SQL(
        INSERT INTO job_posts (company, position, jd_text, required_skills, preferred_skills, responsibilities)
        VALUES (?, ?, ?, ?, ?, ?);
    )SQL");

    stmt.BindText(1, profile.value("company", "Unknown"));
    stmt.BindText(2, profile.value("position", "Unknown"));
    stmt.BindText(3, jd_text);
    stmt.BindText(4, profile.contains("required_skills") ? profile["required_skills"].dump() : "[]");
    stmt.BindText(5, profile.contains("preferred_skills") ? profile["preferred_skills"].dump() : "[]");
    stmt.BindText(6, profile.contains("responsibilities") ? profile["responsibilities"].dump() : "[]");
    stmt.StepDone();

    return sqlite3_last_insert_rowid(db_.Handle());
}

json JobAgentDatabase::GetResumeProfileById(int64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    EnsureInitialized();

    Statement stmt(db_.Handle(),
        "SELECT * FROM resume_profiles WHERE id = ?;");
    stmt.BindInt64(1, id);

    if (stmt.StepRow()) {
        json profile;
        profile["id"] = sqlite3_column_int64(stmt.Get(), 0);
        profile["name"] = ColumnText(stmt.Get(), 1);
        profile["education"] = json::parse(ColumnText(stmt.Get(), 2));
        profile["skills"] = json::parse(ColumnText(stmt.Get(), 3));
        profile["projects"] = json::parse(ColumnText(stmt.Get(), 4));
        profile["raw_text"] = ColumnText(stmt.Get(), 5);
        profile["created_at"] = ColumnText(stmt.Get(), 6);
        return profile;
    }
    return json::object();
}

json JobAgentDatabase::GetJobProfileById(int64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    EnsureInitialized();

    Statement stmt(db_.Handle(),
        "SELECT * FROM job_posts WHERE id = ?;");
    stmt.BindInt64(1, id);

    if (stmt.StepRow()) {
        json profile;
        profile["id"] = sqlite3_column_int64(stmt.Get(), 0);
        profile["company"] = ColumnText(stmt.Get(), 1);
        profile["position"] = ColumnText(stmt.Get(), 2);
        profile["jd_text"] = ColumnText(stmt.Get(), 3);
        profile["required_skills"] = json::parse(ColumnText(stmt.Get(), 4));
        profile["preferred_skills"] = json::parse(ColumnText(stmt.Get(), 5));
        profile["responsibilities"] = json::parse(ColumnText(stmt.Get(), 6));
        profile["created_at"] = ColumnText(stmt.Get(), 7);
        return profile;
    }
    return json::object();
}

int64_t JobAgentDatabase::SaveMatchResult(
    int64_t resume_id, int64_t job_id, double match_score,
    const std::string& matched_skills, const std::string& missing_skills,
    const std::string& strengths, const std::string& weaknesses,
    const std::string& suggestions, const std::string& report
) {
    std::lock_guard<std::mutex> lock(mutex_);
    EnsureInitialized();

    Statement stmt(db_.Handle(), R"SQL(
        INSERT INTO match_results
            (resume_id, job_id, match_score, matched_skills, missing_skills,
             strengths, weaknesses, suggestions, report)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
    )SQL");

    stmt.BindInt64(1, resume_id);
    stmt.BindInt64(2, job_id);
    stmt.BindDouble(3, match_score);
    stmt.BindText(4, matched_skills);
    stmt.BindText(5, missing_skills);
    stmt.BindText(6, strengths);
    stmt.BindText(7, weaknesses);
    stmt.BindText(8, suggestions);
    stmt.BindText(9, report);
    stmt.StepDone();

    return sqlite3_last_insert_rowid(db_.Handle());
}

std::vector<MatchHistoryRecord> JobAgentDatabase::QueryMatchHistory(int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    EnsureInitialized();

    const int normalized_limit = std::max(1, std::min(limit, 500));
    Statement stmt(db_.Handle(), R"SQL(
        SELECT mr.*, rp.name AS candidate_name, jp.company, jp.position
        FROM match_results mr
        JOIN resume_profiles rp ON mr.resume_id = rp.id
        JOIN job_posts jp ON mr.job_id = jp.id
        ORDER BY mr.created_at DESC
        LIMIT ?;
    )SQL");
    stmt.BindInt64(1, normalized_limit);

    std::vector<MatchHistoryRecord> records;
    while (stmt.StepRow()) {
        MatchHistoryRecord rec;
        rec.id = sqlite3_column_int64(stmt.Get(), 0);
        rec.resume_id = sqlite3_column_int64(stmt.Get(), 1);
        rec.job_id = sqlite3_column_int64(stmt.Get(), 2);
        rec.match_score = sqlite3_column_double(stmt.Get(), 3);
        rec.matched_skills = ColumnText(stmt.Get(), 4);
        rec.missing_skills = ColumnText(stmt.Get(), 5);
        rec.strengths = ColumnText(stmt.Get(), 6);
        rec.weaknesses = ColumnText(stmt.Get(), 7);
        rec.suggestions = ColumnText(stmt.Get(), 8);
        rec.report = ColumnText(stmt.Get(), 9);
        rec.created_at = ColumnText(stmt.Get(), 10);
        rec.candidate_name = ColumnText(stmt.Get(), 11);
        rec.company = ColumnText(stmt.Get(), 12);
        rec.position = ColumnText(stmt.Get(), 13);
        records.emplace_back(std::move(rec));
    }
    return records;
}

// -----------------------------------------------------------------------
// Interview
// -----------------------------------------------------------------------

int64_t JobAgentDatabase::SaveInterviewRecord(
    int64_t resume_id, int64_t job_id, const std::string& question,
    const std::string& answer, const std::string& feedback, double score
) {
    std::lock_guard<std::mutex> lock(mutex_);
    EnsureInitialized();

    Statement stmt(db_.Handle(), R"SQL(
        INSERT INTO interview_records (resume_id, job_id, question, answer, feedback, score)
        VALUES (?, ?, ?, ?, ?, ?);
    )SQL");

    stmt.BindInt64(1, resume_id);
    stmt.BindInt64(2, job_id);
    stmt.BindText(3, question);
    stmt.BindText(4, answer);
    stmt.BindText(5, feedback);
    stmt.BindDouble(6, score);
    stmt.StepDone();

    return sqlite3_last_insert_rowid(db_.Handle());
}

void JobAgentDatabase::UpdateInterviewRecord(
    int64_t id, const std::string& answer, const std::string& feedback, double score
) {
    std::lock_guard<std::mutex> lock(mutex_);
    EnsureInitialized();

    Statement stmt(db_.Handle(), R"SQL(
        UPDATE interview_records
        SET answer = ?, feedback = ?, score = ?
        WHERE id = ?;
    )SQL");

    stmt.BindText(1, answer);
    stmt.BindText(2, feedback);
    stmt.BindDouble(3, score);
    stmt.BindInt64(4, id);
    stmt.StepDone();
}

std::vector<InterviewRecord> JobAgentDatabase::QueryInterviewRecords(
    int64_t resume_id, int64_t job_id, int limit
) {
    std::lock_guard<std::mutex> lock(mutex_);
    EnsureInitialized();

    const int normalized_limit = std::max(1, std::min(limit, 500));

    // Build dynamic WHERE clause.
    std::string sql = R"SQL(
        SELECT * FROM interview_records WHERE 1=1
    )SQL";
    if (resume_id > 0) {
        sql += " AND resume_id = " + std::to_string(resume_id);
    }
    if (job_id > 0) {
        sql += " AND job_id = " + std::to_string(job_id);
    }
    sql += " ORDER BY created_at DESC LIMIT ?;";

    Statement stmt(db_.Handle(), sql);
    stmt.BindInt64(1, normalized_limit);

    std::vector<InterviewRecord> records;
    while (stmt.StepRow()) {
        InterviewRecord rec;
        rec.id = sqlite3_column_int64(stmt.Get(), 0);
        rec.resume_id = sqlite3_column_int64(stmt.Get(), 1);
        rec.job_id = sqlite3_column_int64(stmt.Get(), 2);
        rec.question = ColumnText(stmt.Get(), 3);
        rec.answer = ColumnText(stmt.Get(), 4);
        rec.feedback = ColumnText(stmt.Get(), 5);
        rec.score = sqlite3_column_double(stmt.Get(), 6);
        rec.created_at = ColumnText(stmt.Get(), 7);
        records.emplace_back(std::move(rec));
    }
    return records;
}

// -----------------------------------------------------------------------
// Application
// -----------------------------------------------------------------------

int64_t JobAgentDatabase::SaveApplicationRecord(
    const std::string& company, const std::string& position, const std::string& status,
    int64_t resume_id, int64_t job_id, const std::string& notes,
    const std::string& apply_date, const std::string& interview_date
) {
    std::lock_guard<std::mutex> lock(mutex_);
    EnsureInitialized();

    Statement stmt(db_.Handle(), R"SQL(
        INSERT INTO application_records
            (company, position, status, resume_id, job_id, notes, apply_date, interview_date)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?);
    )SQL");

    stmt.BindText(1, company);
    stmt.BindText(2, position);
    stmt.BindText(3, status);
    if (resume_id > 0) stmt.BindInt64(4, resume_id); else stmt.BindText(4, "");
    if (job_id > 0) stmt.BindInt64(5, job_id); else stmt.BindText(5, "");
    stmt.BindText(6, notes);
    stmt.BindText(7, apply_date);
    stmt.BindText(8, interview_date);
    stmt.StepDone();

    return sqlite3_last_insert_rowid(db_.Handle());
}

void JobAgentDatabase::UpdateApplicationStatus(
    int64_t id, const std::string& status, const std::string& interview_date
) {
    std::lock_guard<std::mutex> lock(mutex_);
    EnsureInitialized();

    Statement stmt(db_.Handle(), R"SQL(
        UPDATE application_records
        SET status = ?, interview_date = ?, updated_at = CURRENT_TIMESTAMP
        WHERE id = ?;
    )SQL");

    stmt.BindText(1, status);
    stmt.BindText(2, interview_date);
    stmt.BindInt64(3, id);
    stmt.StepDone();
}

std::vector<ApplicationRecord> JobAgentDatabase::QueryApplicationRecords(
    const std::string& company, const std::string& position, const std::string& status, int limit
) {
    std::lock_guard<std::mutex> lock(mutex_);
    EnsureInitialized();

    const int normalized_limit = std::max(1, std::min(limit, 500));

    std::string sql = R"SQL(
        SELECT * FROM application_records WHERE 1=1
    )SQL";
    if (!company.empty()) {
        sql += " AND company LIKE '%' || ? || '%'";
    }
    if (!position.empty()) {
        sql += " AND position LIKE '%' || ? || '%'";
    }
    if (!status.empty()) {
        sql += " AND status = ?";
    }
    sql += " ORDER BY updated_at DESC LIMIT ?;";

    Statement stmt(db_.Handle(), sql);
    int bind_index = 1;
    if (!company.empty()) {
        stmt.BindText(bind_index++, company);
    }
    if (!position.empty()) {
        stmt.BindText(bind_index++, position);
    }
    if (!status.empty()) {
        stmt.BindText(bind_index++, status);
    }
    stmt.BindInt64(bind_index, normalized_limit);

    std::vector<ApplicationRecord> records;
    while (stmt.StepRow()) {
        ApplicationRecord rec;
        rec.id = sqlite3_column_int64(stmt.Get(), 0);
        rec.company = ColumnText(stmt.Get(), 1);
        rec.position = ColumnText(stmt.Get(), 2);
        rec.status = ColumnText(stmt.Get(), 3);
        rec.resume_id = sqlite3_column_int64(stmt.Get(), 4);
        rec.job_id = sqlite3_column_int64(stmt.Get(), 5);
        rec.notes = ColumnText(stmt.Get(), 6);
        rec.apply_date = ColumnText(stmt.Get(), 7);
        rec.interview_date = ColumnText(stmt.Get(), 8);
        rec.created_at = ColumnText(stmt.Get(), 9);
        rec.updated_at = ColumnText(stmt.Get(), 10);
        records.emplace_back(std::move(rec));
    }
    return records;
}

} // namespace job_agent
} // namespace mcp
