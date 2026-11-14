#include "mcp_job_agent_tools.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "config/config.h"
#include "job_agent/job_agent_database.h"
#include "logger/logger.h"

namespace mcp {

namespace {

ToolResult MakeTextResult(const std::string& text, bool is_error = false) {
    ToolResult result;
    result.is_error = is_error;
    result.content.emplace_back(ContentItem{
        .type = "text",
        .text = text
    });
    return result;
}

} // anonymous namespace

void RegisterJobAgentTools(McpServer& mcp) {
    auto& cfg = mcp::config::Config::GetInstance();
    std::string db_path = cfg.GetJobAgentDbPath();
    if (db_path.empty()) {
        MCP_LOG_WARN("job_agent.db_path is not configured in server.json; tools will fail at runtime");
    }

    auto database = std::make_shared<job_agent::JobAgentDatabase>(db_path);
    try {
        database->Initialize();
        MCP_LOG_INFO("Job agent database initialized: {}", db_path);
    } catch (const std::exception& e) {
        MCP_LOG_ERROR("Failed to initialize job agent database: {}", e.what());
    }

    int tool_count = 0;

    // Tool: job_save_resume_profile
    {
        Tool tool;
        tool.name = "job_save_resume_profile";
        tool.description = "Save a parsed resume profile to the job agent database";
        tool.input_schema.properties = {
            {"profile", {{"type", "object"}, {"description", "The structured resume profile JSON"}}},
            {"raw_text", {{"type", "string"}, {"description", "The original resume text"}}}
        };
        tool.input_schema.required = {"profile", "raw_text"};

        mcp.RegisterTool(tool, [database](const json& args) -> ToolResult {
            try {
                if (!args.contains("profile") || !args["profile"].is_object()) {
                    return MakeTextResult("profile must be a JSON object", true);
                }
                if (!args.contains("raw_text") || !args["raw_text"].is_string()) {
                    return MakeTextResult("raw_text must be a string", true);
                }
                int64_t id = database->SaveResumeProfile(
                    args["profile"], args["raw_text"].get<std::string>()
                );
                return MakeTextResult((json{{"id", id}}).dump());
            } catch (const std::exception& e) {
                return MakeTextResult(std::string("Failed to save resume profile: ") + e.what(), true);
            }
        });
        ++tool_count;
    }

    // Tool: job_save_job_profile
    {
        Tool tool;
        tool.name = "job_save_job_profile";
        tool.description = "Save a parsed job profile to the job agent database";
        tool.input_schema.properties = {
            {"profile", {{"type", "object"}, {"description", "The structured job profile JSON"}}},
            {"jd_text", {{"type", "string"}, {"description", "The original job description text"}}}
        };
        tool.input_schema.required = {"profile", "jd_text"};

        mcp.RegisterTool(tool, [database](const json& args) -> ToolResult {
            try {
                if (!args.contains("profile") || !args["profile"].is_object()) {
                    return MakeTextResult("profile must be a JSON object", true);
                }
                if (!args.contains("jd_text") || !args["jd_text"].is_string()) {
                    return MakeTextResult("jd_text must be a string", true);
                }
                int64_t id = database->SaveJobProfile(
                    args["profile"], args["jd_text"].get<std::string>()
                );
                return MakeTextResult((json{{"id", id}}).dump());
            } catch (const std::exception& e) {
                return MakeTextResult(std::string("Failed to save job profile: ") + e.what(), true);
            }
        });
        ++tool_count;
    }

    // Tool: job_save_match_result
    {
        Tool tool;
        tool.name = "job_save_match_result";
        tool.description = "Save a match result to the job agent database";
        tool.input_schema.properties = {
            {"resume_id", {{"type", "integer"}, {"description", "Resume profile ID"}}},
            {"job_id", {{"type", "integer"}, {"description", "Job profile ID"}}},
            {"match_score", {{"type", "number"}, {"description", "Match score from 0-100"}}},
            {"matched_skills", {{"type", "string"}, {"description", "JSON array"}}},
            {"missing_skills", {{"type", "string"}, {"description", "JSON array"}}},
            {"strengths", {{"type", "string"}, {"description", "JSON array"}}},
            {"weaknesses", {{"type", "string"}, {"description", "JSON array"}}},
            {"suggestions", {{"type", "string"}, {"description", "JSON array"}}},
            {"report", {{"type", "string"}, {"description", "Text report"}}}
        };
        tool.input_schema.required = {"resume_id", "job_id", "match_score", "matched_skills",
                                       "missing_skills", "strengths", "weaknesses",
                                       "suggestions", "report"};

        mcp.RegisterTool(tool, [database](const json& args) -> ToolResult {
            try {
                int64_t id = database->SaveMatchResult(
                    args["resume_id"].get<int64_t>(), args["job_id"].get<int64_t>(),
                    args["match_score"].get<double>(),
                    args["matched_skills"].get<std::string>(),
                    args["missing_skills"].get<std::string>(),
                    args["strengths"].get<std::string>(),
                    args["weaknesses"].get<std::string>(),
                    args["suggestions"].get<std::string>(),
                    args["report"].get<std::string>()
                );
                return MakeTextResult((json{{"id", id}}).dump());
            } catch (const std::exception& e) {
                return MakeTextResult(std::string("Failed to save match result: ") + e.what(), true);
            }
        });
        ++tool_count;
    }

    // Tool: job_query_match_history
    {
        Tool tool;
        tool.name = "job_query_match_history";
        tool.description = "Query recent match history from the job agent database";
        tool.input_schema.properties = {
            {"limit", {{"type", "integer"}, {"description", "Max records"}, {"default", 10}}}
        };

        mcp.RegisterTool(tool, [database](const json& args) -> ToolResult {
            try {
                int limit = args.value("limit", 10);
                auto records = database->QueryMatchHistory(limit);
                json items = json::array();
                for (const auto& rec : records) {
                    items.push_back({
                        {"id", rec.id}, {"resume_id", rec.resume_id}, {"job_id", rec.job_id},
                        {"match_score", rec.match_score},
                        {"matched_skills", rec.matched_skills},
                        {"missing_skills", rec.missing_skills},
                        {"strengths", rec.strengths},
                        {"weaknesses", rec.weaknesses},
                        {"suggestions", rec.suggestions},
                        {"report", rec.report}, {"created_at", rec.created_at},
                        {"candidate_name", rec.candidate_name},
                        {"company", rec.company}, {"position", rec.position}
                    });
                }
                return MakeTextResult((json{{"records", items}}).dump());
            } catch (const std::exception& e) {
                return MakeTextResult(std::string("Failed to query match history: ") + e.what(), true);
            }
        });
        ++tool_count;
    }

    // Tool: job_get_resume_profile
    {
        Tool tool;
        tool.name = "job_get_resume_profile";
        tool.description = "Get a full resume profile by its ID";
        tool.input_schema.properties = {
            {"id", {{"type", "integer"}, {"description", "Resume profile ID"}}}
        };
        tool.input_schema.required = {"id"};

        mcp.RegisterTool(tool, [database](const json& args) -> ToolResult {
            try {
                auto profile = database->GetResumeProfileById(args["id"].get<int64_t>());
                return MakeTextResult(profile.dump());
            } catch (const std::exception& e) {
                return MakeTextResult(std::string("Failed: ") + e.what(), true);
            }
        });
        ++tool_count;
    }

    // Tool: job_get_job_profile
    {
        Tool tool;
        tool.name = "job_get_job_profile";
        tool.description = "Get a full job profile by its ID";
        tool.input_schema.properties = {
            {"id", {{"type", "integer"}, {"description", "Job post ID"}}}
        };
        tool.input_schema.required = {"id"};

        mcp.RegisterTool(tool, [database](const json& args) -> ToolResult {
            try {
                auto profile = database->GetJobProfileById(args["id"].get<int64_t>());
                return MakeTextResult(profile.dump());
            } catch (const std::exception& e) {
                return MakeTextResult(std::string("Failed: ") + e.what(), true);
            }
        });
        ++tool_count;
    }

    // ------------------------------------------------------------------
    // Interview tools
    // ------------------------------------------------------------------

    // Tool: interview_save_record
    {
        Tool tool;
        tool.name = "interview_save_record";
        tool.description = "Save an interview question and answer record";
        tool.input_schema.properties = {
            {"resume_id", {{"type", "integer"}, {"description", "Resume profile ID"}}},
            {"job_id", {{"type", "integer"}, {"description", "Job post ID"}}},
            {"question", {{"type", "string"}, {"description", "Interview question text"}}},
            {"answer", {{"type", "string"}, {"description", "Candidate answer"}}},
            {"feedback", {{"type", "string"}, {"description", "Feedback on answer"}}},
            {"score", {{"type", "number"}, {"description", "Score 0-100"}}}
        };
        tool.input_schema.required = {"resume_id", "job_id", "question"};

        mcp.RegisterTool(tool, [database](const json& args) -> ToolResult {
            try {
                int64_t id = database->SaveInterviewRecord(
                    args["resume_id"].get<int64_t>(),
                    args["job_id"].get<int64_t>(),
                    args["question"].get<std::string>(),
                    args.value("answer", std::string("")),
                    args.value("feedback", std::string("")),
                    args.value("score", 0.0)
                );
                return MakeTextResult((json{{"id", id}}).dump());
            } catch (const std::exception& e) {
                return MakeTextResult(std::string("Failed to save interview record: ") + e.what(), true);
            }
        });
        ++tool_count;
    }

    // Tool: interview_query_records
    {
        Tool tool;
        tool.name = "interview_query_records";
        tool.description = "Query interview records for a resume or job";
        tool.input_schema.properties = {
            {"resume_id", {{"type", "integer"}, {"description", "Filter by resume ID (0 = no filter)"}, {"default", 0}}},
            {"job_id", {{"type", "integer"}, {"description", "Filter by job ID (0 = no filter)"}, {"default", 0}}},
            {"limit", {{"type", "integer"}, {"description", "Max records"}, {"default", 20}}}
        };

        mcp.RegisterTool(tool, [database](const json& args) -> ToolResult {
            try {
                int64_t resume_id = args.value("resume_id", 0);
                int64_t job_id = args.value("job_id", 0);
                int limit = args.value("limit", 20);
                auto records = database->QueryInterviewRecords(resume_id, job_id, limit);
                json items = json::array();
                for (const auto& rec : records) {
                    items.push_back({
                        {"id", rec.id}, {"resume_id", rec.resume_id}, {"job_id", rec.job_id},
                        {"question", rec.question}, {"answer", rec.answer},
                        {"feedback", rec.feedback}, {"score", rec.score},
                        {"created_at", rec.created_at}
                    });
                }
                return MakeTextResult((json{{"records", items}}).dump());
            } catch (const std::exception& e) {
                return MakeTextResult(std::string("Failed to query interview records: ") + e.what(), true);
            }
        });
        ++tool_count;
    }

    // Tool: interview_update_record
    {
        Tool tool;
        tool.name = "interview_update_record";
        tool.description = "Update an interview record with answer, feedback, and score";
        tool.input_schema.properties = {
            {"id", {{"type", "integer"}, {"description", "Interview record ID"}}},
            {"answer", {{"type", "string"}, {"description", "Candidate's answer"}}},
            {"feedback", {{"type", "string"}, {"description", "Feedback on answer"}}},
            {"score", {{"type", "number"}, {"description", "Score 0-100"}}}
        };
        tool.input_schema.required = {"id", "answer", "feedback", "score"};

        mcp.RegisterTool(tool, [database](const json& args) -> ToolResult {
            try {
                database->UpdateInterviewRecord(
                    args["id"].get<int64_t>(),
                    args["answer"].get<std::string>(),
                    args["feedback"].get<std::string>(),
                    args["score"].get<double>()
                );
                return MakeTextResult("OK");
            } catch (const std::exception& e) {
                return MakeTextResult(std::string("Failed to update interview record: ") + e.what(), true);
            }
        });
        ++tool_count;
    }

    // ------------------------------------------------------------------
    // Application tools
    // ------------------------------------------------------------------

    // Tool: application_save
    {
        Tool tool;
        tool.name = "application_save";
        tool.description = "Create a new application record";
        tool.input_schema.properties = {
            {"company", {{"type", "string"}, {"description", "Company name"}}},
            {"position", {{"type", "string"}, {"description", "Job position"}}},
            {"status", {{"type", "string"}, {"description", "Status: draft/applied/interviewing/offered/rejected/withdrawn"}, {"default", "draft"}}},
            {"resume_id", {{"type", "integer"}, {"description", "Resume profile ID"}, {"default", 0}}},
            {"job_id", {{"type", "integer"}, {"description", "Job post ID"}, {"default", 0}}},
            {"notes", {{"type", "string"}, {"description", "Notes"}, {"default", ""}}},
            {"apply_date", {{"type", "string"}, {"description", "Application date YYYY-MM-DD"}, {"default", ""}}},
            {"interview_date", {{"type", "string"}, {"description", "Interview date YYYY-MM-DD"}, {"default", ""}}}
        };
        tool.input_schema.required = {"company", "position"};

        mcp.RegisterTool(tool, [database](const json& args) -> ToolResult {
            try {
                int64_t id = database->SaveApplicationRecord(
                    args["company"].get<std::string>(),
                    args["position"].get<std::string>(),
                    args.value("status", std::string("draft")),
                    args.value("resume_id", 0),
                    args.value("job_id", 0),
                    args.value("notes", std::string("")),
                    args.value("apply_date", std::string("")),
                    args.value("interview_date", std::string(""))
                );
                return MakeTextResult((json{{"id", id}}).dump());
            } catch (const std::exception& e) {
                return MakeTextResult(std::string("Failed to save application: ") + e.what(), true);
            }
        });
        ++tool_count;
    }

    // Tool: application_update_status
    {
        Tool tool;
        tool.name = "application_update_status";
        tool.description = "Update an application record status and interview date";
        tool.input_schema.properties = {
            {"id", {{"type", "integer"}, {"description", "Application record ID"}}},
            {"status", {{"type", "string"}, {"description", "New status"}}},
            {"interview_date", {{"type", "string"}, {"description", "Interview date YYYY-MM-DD (optional)"}, {"default", ""}}}
        };
        tool.input_schema.required = {"id", "status"};

        mcp.RegisterTool(tool, [database](const json& args) -> ToolResult {
            try {
                database->UpdateApplicationStatus(
                    args["id"].get<int64_t>(),
                    args["status"].get<std::string>(),
                    args.value("interview_date", std::string(""))
                );
                return MakeTextResult("OK");
            } catch (const std::exception& e) {
                return MakeTextResult(std::string("Failed to update application: ") + e.what(), true);
            }
        });
        ++tool_count;
    }

    // Tool: application_query
    {
        Tool tool;
        tool.name = "application_query";
        tool.description = "Query application records with filters";
        tool.input_schema.properties = {
            {"company", {{"type", "string"}, {"description", "Filter by company name"}, {"default", ""}}},
            {"position", {{"type", "string"}, {"description", "Filter by position"}, {"default", ""}}},
            {"status", {{"type", "string"}, {"description", "Filter by status"}, {"default", ""}}},
            {"limit", {{"type", "integer"}, {"description", "Max records"}, {"default", 20}}}
        };

        mcp.RegisterTool(tool, [database](const json& args) -> ToolResult {
            try {
                auto company = args.value("company", std::string(""));
                auto position = args.value("position", std::string(""));
                auto status = args.value("status", std::string(""));
                int limit = args.value("limit", 20);
                auto records = database->QueryApplicationRecords(company, position, status, limit);
                json items = json::array();
                for (const auto& rec : records) {
                    items.push_back({
                        {"id", rec.id}, {"company", rec.company}, {"position", rec.position},
                        {"status", rec.status}, {"resume_id", rec.resume_id}, {"job_id", rec.job_id},
                        {"notes", rec.notes}, {"apply_date", rec.apply_date},
                        {"interview_date", rec.interview_date},
                        {"created_at", rec.created_at}, {"updated_at", rec.updated_at}
                    });
                }
                return MakeTextResult((json{{"records", items}}).dump());
            } catch (const std::exception& e) {
                return MakeTextResult(std::string("Failed to query applications: ") + e.what(), true);
            }
        });
        ++tool_count;
    }

    MCP_LOG_INFO("Registered {} job agent tools", tool_count);
}

} // namespace mcp
