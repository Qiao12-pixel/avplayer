#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "logger/logger.h"
#include "mcp/mcp_server.h"
#include "sql/tool_call_history_repository.h"
#include "src/main/mcp_builtin_tools.h"

namespace {

using mcp::McpServer;
using mcp::json;

class ScopedCurrentPath {
public:
    explicit ScopedCurrentPath(const std::filesystem::path& path)
        : original_path_(std::filesystem::current_path()) {
        std::filesystem::current_path(path);
    }

    ~ScopedCurrentPath() {
        std::filesystem::current_path(original_path_);
    }

private:
    std::filesystem::path original_path_;
};

std::string ExtractText(const mcp::ToolResult& result) {
    if (result.content.empty() || !result.content.front().text.has_value()) {
        return "";
    }
    return result.content.front().text.value();
}

class BuiltinToolsTest : public ::testing::Test {
protected:
    void SetUp() override {
        mcp::logger::Logger::GetInstance().Shutdown();
        mcp::logger::Logger::GetInstance().Init("builtin_tools_test", "", 1024 * 1024, 3, false);
    }

    void TearDown() override {
        mcp::logger::Logger::GetInstance().Shutdown();
    }
};

}  // namespace

TEST_F(BuiltinToolsTest, ReadFileReturnsWorkspaceRelativeFileContent) {
    const auto workspace = std::filesystem::temp_directory_path() / "mcp_builtin_tools_read_file";
    std::filesystem::create_directories(workspace);
    const auto file_path = workspace / "hello.txt";
    {
        std::ofstream file(file_path);
        file << "hello from builtin tool";
    }

    ScopedCurrentPath scoped_path(workspace);
    McpServer server("unit-server", "1.0");
    mcp::RegisterBuiltinTools(server);

    const auto result = server.GetTool("read_file", json{{"path", "hello.txt"}});
    const auto text = ExtractText(result);

    EXPECT_FALSE(result.is_error);
    EXPECT_NE(text.find("Path: hello.txt"), std::string::npos);
    EXPECT_NE(text.find("hello from builtin tool"), std::string::npos);

    std::filesystem::remove_all(workspace);
}

TEST_F(BuiltinToolsTest, ReadFileRejectsPathTraversalOutsideWorkspace) {
    const auto workspace = std::filesystem::temp_directory_path() / "mcp_builtin_tools_traversal";
    std::filesystem::create_directories(workspace);

    ScopedCurrentPath scoped_path(workspace);
    McpServer server("unit-server", "1.0");
    mcp::RegisterBuiltinTools(server);

    const auto result = server.GetTool("read_file", json{{"path", "../outside.txt"}});
    const auto text = ExtractText(result);

    EXPECT_TRUE(result.is_error);
    EXPECT_NE(text.find("current workspace"), std::string::npos);

    std::filesystem::remove_all(workspace);
}

TEST_F(BuiltinToolsTest, ListDirectoryReturnsStructuredJsonListing) {
    const auto workspace = std::filesystem::temp_directory_path() / "mcp_builtin_tools_list_directory";
    std::filesystem::create_directories(workspace / "subdir");
    {
        std::ofstream file(workspace / "a.txt");
        file << "alpha";
    }

    ScopedCurrentPath scoped_path(workspace);
    McpServer server("unit-server", "1.0");
    mcp::RegisterBuiltinTools(server);

    const auto result = server.GetTool("list_directory", json{{"path", "."}});
    const auto text = ExtractText(result);
    const auto listing = json::parse(text);

    EXPECT_FALSE(result.is_error);
    EXPECT_EQ(listing.at("path").get<std::string>(), ".");
    EXPECT_FALSE(listing.at("truncated").get<bool>());
    ASSERT_TRUE(listing.at("entries").is_array());
    bool found_a_txt = false;
    bool found_subdir = false;
    for (const auto& entry : listing.at("entries")) {
        const auto path = entry.at("path").get<std::string>();
        if (path == "a.txt") {
            found_a_txt = true;
        }
        if (path == "subdir") {
            found_subdir = true;
        }
    }
    EXPECT_TRUE(found_a_txt);
    EXPECT_TRUE(found_subdir);

    std::filesystem::remove_all(workspace);
}

TEST_F(BuiltinToolsTest, ReadMultipleFilesReturnsStructuredBatchPayload) {
    const auto workspace = std::filesystem::temp_directory_path() / "mcp_builtin_tools_read_multiple_files";
    std::filesystem::create_directories(workspace);
    {
        std::ofstream file(workspace / "a.txt");
        file << "alpha";
    }
    {
        std::ofstream file(workspace / "b.txt");
        file << "bravo-longer";
    }

    ScopedCurrentPath scoped_path(workspace);
    McpServer server("unit-server", "1.0");
    mcp::RegisterBuiltinTools(server);

    const auto result = server.GetTool(
        "read_multiple_files",
        json{{"paths", json::array({"a.txt", "b.txt"})}, {"max_chars_per_file", 5}}
    );
    const auto payload = json::parse(ExtractText(result));

    EXPECT_FALSE(result.is_error);
    ASSERT_TRUE(payload.at("files").is_array());
    ASSERT_EQ(payload.at("files").size(), 2);
    EXPECT_EQ(payload.at("files")[0].at("path").get<std::string>(), "a.txt");
    EXPECT_FALSE(payload.at("files")[0].at("truncated").get<bool>());
    EXPECT_EQ(payload.at("files")[1].at("path").get<std::string>(), "b.txt");
    EXPECT_TRUE(payload.at("files")[1].at("truncated").get<bool>());
    EXPECT_EQ(payload.at("files")[1].at("content").get<std::string>(), "bravo");

    std::filesystem::remove_all(workspace);
}

TEST_F(BuiltinToolsTest, SearchWorkspaceFindsNestedLiteralMatches) {
    const auto workspace = std::filesystem::temp_directory_path() / "mcp_builtin_tools_search_workspace";
    std::filesystem::create_directories(workspace / "nested");
    {
        std::ofstream file(workspace / "nested" / "notes.txt");
        file << "alpha\nneedle here\nomega\n";
    }
    {
        std::ofstream file(workspace / "ignore.txt");
        file << "no hit here\n";
    }

    ScopedCurrentPath scoped_path(workspace);
    McpServer server("unit-server", "1.0");
    mcp::RegisterBuiltinTools(server);

    const auto result = server.GetTool(
        "search_workspace",
        json{{"query", "needle"}, {"path", "."}, {"max_results", 10}}
    );
    const auto payload = json::parse(ExtractText(result));

    EXPECT_FALSE(result.is_error);
    ASSERT_TRUE(payload.at("results").is_array());
    ASSERT_EQ(payload.at("results").size(), 1);
    EXPECT_EQ(payload.at("results")[0].at("path").get<std::string>(), "nested/notes.txt");
    EXPECT_EQ(payload.at("results")[0].at("line_number").get<int>(), 2);
    EXPECT_EQ(payload.at("results")[0].at("line_text").get<std::string>(), "needle here");

    std::filesystem::remove_all(workspace);
}

TEST_F(BuiltinToolsTest, CompareFilesReportsStructuredDifferences) {
    const auto workspace = std::filesystem::temp_directory_path() / "mcp_builtin_tools_compare_files";
    std::filesystem::create_directories(workspace);
    {
        std::ofstream file(workspace / "left.txt");
        file << "line1\nline2\nline3\n";
    }
    {
        std::ofstream file(workspace / "right.txt");
        file << "line1\nLINE2\nline3\nline4\n";
    }

    ScopedCurrentPath scoped_path(workspace);
    McpServer server("unit-server", "1.0");
    mcp::RegisterBuiltinTools(server);

    const auto result = server.GetTool(
        "compare_files",
        json{{"path_a", "left.txt"}, {"path_b", "right.txt"}}
    );
    const auto payload = json::parse(ExtractText(result));

    EXPECT_FALSE(result.is_error);
    EXPECT_FALSE(payload.at("equal").get<bool>());
    ASSERT_TRUE(payload.at("differences").is_array());
    ASSERT_EQ(payload.at("differences").size(), 3);

    bool found_removed_line2 = false;
    bool found_added_line2 = false;
    bool found_added_line4 = false;
    for (const auto& difference : payload.at("differences")) {
        const auto type = difference.at("type").get<std::string>();
        if (type == "removed" && difference.at("line_a").get<int>() == 2) {
            found_removed_line2 = true;
        }
        if (type == "added" && difference.at("line_b").get<int>() == 2) {
            found_added_line2 = true;
        }
        if (type == "added" && difference.at("line_b").get<int>() == 4) {
            found_added_line4 = true;
        }
    }
    EXPECT_TRUE(found_removed_line2);
    EXPECT_TRUE(found_added_line2);
    EXPECT_TRUE(found_added_line4);

    std::filesystem::remove_all(workspace);
}

TEST_F(BuiltinToolsTest, ReadCodeContextReturnsNumberedSnippet) {
    const auto workspace = std::filesystem::temp_directory_path() / "mcp_builtin_tools_read_code_context";
    std::filesystem::create_directories(workspace);
    {
        std::ofstream file(workspace / "sample.cpp");
        file << "line1\nline2\nline3\nline4\n";
    }

    ScopedCurrentPath scoped_path(workspace);
    McpServer server("unit-server", "1.0");
    mcp::RegisterBuiltinTools(server);

    const auto result = server.GetTool(
        "read_code_context",
        json{
            {"paths", json::array({"sample.cpp"})},
            {"start_line", 2},
            {"line_count", 2},
            {"allowed_suffixes", json::array({".cpp"})}
        }
    );
    const auto payload = json::parse(ExtractText(result));

    EXPECT_FALSE(result.is_error);
    ASSERT_EQ(payload.at("files").size(), 1);
    ASSERT_EQ(payload.at("files")[0].at("lines").size(), 2);
    EXPECT_EQ(payload.at("files")[0].at("lines")[0].at("line_number").get<int>(), 2);
    EXPECT_EQ(payload.at("files")[0].at("lines")[0].at("text").get<std::string>(), "line2");
    EXPECT_EQ(payload.at("files")[0].at("lines")[1].at("line_number").get<int>(), 3);

    std::filesystem::remove_all(workspace);
}

TEST_F(BuiltinToolsTest, RunCommandExecutesAllowlistedPwdInsideWorkspace) {
    const auto workspace = std::filesystem::temp_directory_path() / "mcp_builtin_tools_run_command";
    std::filesystem::create_directories(workspace / "nested");

    ScopedCurrentPath scoped_path(workspace);
    McpServer server("unit-server", "1.0");
    mcp::RegisterBuiltinTools(server);

    const auto result = server.GetTool(
        "run_command",
        json{{"command", "pwd"}, {"cwd", "nested"}}
    );
    const auto payload = json::parse(ExtractText(result));

    EXPECT_FALSE(result.is_error);
    EXPECT_EQ(payload.at("exit_code").get<int>(), 0);
    EXPECT_FALSE(payload.at("timed_out").get<bool>());
    EXPECT_NE(payload.at("output").get<std::string>().find((workspace / "nested").string()), std::string::npos);

    std::filesystem::remove_all(workspace);
}

TEST_F(BuiltinToolsTest, RunCommandRejectsPathTraversalArguments) {
    const auto workspace = std::filesystem::temp_directory_path() / "mcp_builtin_tools_run_command_reject";
    std::filesystem::create_directories(workspace);

    ScopedCurrentPath scoped_path(workspace);
    McpServer server("unit-server", "1.0");
    mcp::RegisterBuiltinTools(server);

    const auto result = server.GetTool(
        "run_command",
        json{{"command", "cat"}, {"args", json::array({"../outside.txt"})}}
    );
    const auto text = ExtractText(result);

    EXPECT_TRUE(result.is_error);
    EXPECT_NE(text.find("current workspace"), std::string::npos);

    std::filesystem::remove_all(workspace);
}

TEST_F(BuiltinToolsTest, QueryToolHistoryReturnsRecentRecords) {
    const auto workspace = std::filesystem::temp_directory_path() / "mcp_builtin_tools_query_history";
    std::filesystem::create_directories(workspace);

    ScopedCurrentPath scoped_path(workspace);
    auto repository = std::make_shared<mcp::sql::ToolCallHistoryRepository>("data/tool_call_history.sqlite3");
    repository->Initialize();

    McpServer server("unit-server", "1.0");
    server.SetToolCallHistoryRepository(repository);
    mcp::RegisterBuiltinTools(server);

    const auto echo_result = server.GetTool("echo", json{{"message", "history-test"}});
    EXPECT_FALSE(echo_result.is_error);

    const auto result = server.GetTool(
        "query_tool_history",
        json{{"tool_name", "echo"}, {"limit", 5}}
    );
    const auto payload = json::parse(ExtractText(result));

    EXPECT_FALSE(result.is_error);
    ASSERT_TRUE(payload.at("records").is_array());
    ASSERT_FALSE(payload.at("records").empty());
    EXPECT_EQ(payload.at("records")[0].at("tool_name").get<std::string>(), "echo");
    EXPECT_NE(payload.at("records")[0].at("arguments_json").get<std::string>().find("history-test"), std::string::npos);

    std::filesystem::remove_all(workspace);
}
