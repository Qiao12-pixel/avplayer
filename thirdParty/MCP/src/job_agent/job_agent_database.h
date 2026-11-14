#ifndef JOB_AGENT_DATABASE_H
#define JOB_AGENT_DATABASE_H

#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "sql/sqlite_database.h"

namespace mcp {
namespace job_agent {

using json = nlohmann::json;

struct ResumeProfileRecord {
    int64_t id = 0;
    std::string name;
    std::string education;
    std::string skills;
    std::string projects;
    std::string raw_text;
    std::string created_at;
};

struct JobPostRecord {
    int64_t id = 0;
    std::string company;
    std::string position;
    std::string jd_text;
    std::string required_skills;
    std::string preferred_skills;
    std::string responsibilities;
    std::string created_at;
};

struct MatchResultRecord {
    int64_t id = 0;
    int64_t resume_id = 0;
    int64_t job_id = 0;
    double match_score = 0.0;
    std::string matched_skills;
    std::string missing_skills;
    std::string strengths;
    std::string weaknesses;
    std::string suggestions;
    std::string report;
    std::string created_at;
};

struct MatchHistoryRecord {
    int64_t id = 0;
    int64_t resume_id = 0;
    int64_t job_id = 0;
    double match_score = 0.0;
    std::string matched_skills;
    std::string missing_skills;
    std::string strengths;
    std::string weaknesses;
    std::string suggestions;
    std::string report;
    std::string created_at;
    std::string candidate_name;
    std::string company;
    std::string position;
};

struct InterviewRecord {
    int64_t id = 0;
    int64_t resume_id = 0;
    int64_t job_id = 0;
    std::string question;
    std::string answer;
    std::string feedback;
    double score = 0.0;
    std::string created_at;
};

struct ApplicationRecord {
    int64_t id = 0;
    std::string company;
    std::string position;
    std::string status;
    int64_t resume_id = 0;
    int64_t job_id = 0;
    std::string notes;
    std::string apply_date;
    std::string interview_date;
    std::string created_at;
    std::string updated_at;
};

class JobAgentDatabase {
public:
    explicit JobAgentDatabase(std::filesystem::path db_path);
    ~JobAgentDatabase() = default;

    void Initialize();

    // Resume / Job / Match operations.
    int64_t SaveResumeProfile(const json& profile, const std::string& raw_text);
    int64_t SaveJobProfile(const json& profile, const std::string& jd_text);
    json GetResumeProfileById(int64_t id);
    json GetJobProfileById(int64_t id);
    int64_t SaveMatchResult(
        int64_t resume_id, int64_t job_id, double match_score,
        const std::string& matched_skills, const std::string& missing_skills,
        const std::string& strengths, const std::string& weaknesses,
        const std::string& suggestions, const std::string& report
    );
    std::vector<MatchHistoryRecord> QueryMatchHistory(int limit);

    // Interview operations.
    int64_t SaveInterviewRecord(
        int64_t resume_id, int64_t job_id, const std::string& question,
        const std::string& answer, const std::string& feedback, double score
    );
    void UpdateInterviewRecord(
        int64_t id, const std::string& answer, const std::string& feedback, double score
    );
    std::vector<InterviewRecord> QueryInterviewRecords(int64_t resume_id, int64_t job_id, int limit);

    // Application operations.
    int64_t SaveApplicationRecord(
        const std::string& company, const std::string& position, const std::string& status,
        int64_t resume_id, int64_t job_id, const std::string& notes,
        const std::string& apply_date, const std::string& interview_date
    );
    void UpdateApplicationStatus(int64_t id, const std::string& status, const std::string& interview_date);
    std::vector<ApplicationRecord> QueryApplicationRecords(
        const std::string& company, const std::string& position, const std::string& status, int limit
    );

private:
    void EnsureInitialized();

    sql::SqliteDatabase db_;
    bool initialized_ = false;
    std::mutex mutex_;
};

} // namespace job_agent
} // namespace mcp

#endif // JOB_AGENT_DATABASE_H
