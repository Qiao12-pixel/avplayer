#include <mcp/mcp_server.h>
#include <logger/logger.h>
#include <sql/tool_call_history_repository.h>
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

using mcp::ContentItem;
using mcp::McpServer;
using mcp::Prompt;
using mcp::PromptMessage;
using mcp::Resource;
using mcp::ResourceContent;
using mcp::Role;
using mcp::Tool;
using mcp::ToolResult;
using mcp::json;

class McpServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        mcp::logger::Logger::GetInstance().Shutdown();
        mcp::logger::Logger::GetInstance().Init("mcp_server_test", "", 1024 * 1024, 3, false);
    }

    void TearDown() override {
        mcp::logger::Logger::GetInstance().Shutdown();
    }
};

Tool MakeTool(const std::string& name) {
    Tool tool;
    tool.name = name;
    tool.description = "test tool";
    tool.input_schema.properties = json::object();
    return tool;
}

ContentItem TextItem(const std::string& text) {
    ContentItem item;
    item.type = "text";
    item.text = text;
    return item;
}

std::filesystem::path UniqueHistoryDatabasePath(const std::string& name) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("mcp_tool_history_" + name + "_" + std::to_string(suffix) + ".sqlite3");
}

}  // namespace

TEST_F(McpServerTest, InitializationResultContainsServerInfoAndCapabilities) {
    McpServer server("unit-server", "1.2.3");

    const auto result = server.GetInitializationResult();

    EXPECT_EQ(result.protocol_version, mcp::LATEST_PROTOCOL_VERSION);
    EXPECT_EQ(result.server_info.name, "unit-server");
    EXPECT_EQ(result.server_info.version, "1.2.3");
    ASSERT_TRUE(result.server_cap.tools.has_value());
    ASSERT_TRUE(result.server_cap.resources.has_value());
    ASSERT_TRUE(result.server_cap.prompts.has_value());
}

TEST_F(McpServerTest, RegistersListsAndCallsTool) {
    McpServer server("unit-server", "1.0");
    server.RegisterTool(MakeTool("echo"), [&server](const json& arguments) {
        EXPECT_TRUE(server.HasTool("echo"));

        ToolResult result;
        result.content.push_back(TextItem(arguments.at("message").get<std::string>()));
        return result;
    });

    EXPECT_TRUE(server.HasTool("echo"));
    const auto tools = server.ListTools();
    ASSERT_EQ(tools.size(), 1);
    EXPECT_EQ(tools.front().name, "echo");

    const auto result = server.GetTool("echo", json{{"message", "hello"}});
    ASSERT_EQ(result.content.size(), 1);
    ASSERT_TRUE(result.content.front().text.has_value());
    EXPECT_EQ(result.content.front().text.value(), "hello");
    EXPECT_FALSE(result.is_error);
}

TEST_F(McpServerTest, ToolExceptionBecomesErrorToolResult) {
    McpServer server("unit-server", "1.0");
    server.RegisterTool(MakeTool("fail"), [](const json&) -> ToolResult {
        throw std::runtime_error("boom");
    });

    const auto result = server.GetTool("fail", json::object());

    EXPECT_TRUE(result.is_error);
    ASSERT_EQ(result.content.size(), 1);
    ASSERT_TRUE(result.content.front().text.has_value());
    EXPECT_NE(result.content.front().text->find("boom"), std::string::npos);
}

TEST_F(McpServerTest, RecordsSuccessfulToolCallHistoryWhenRepositoryIsConfigured) {
    const auto db_path = UniqueHistoryDatabasePath("success");
    auto repository = std::make_shared<mcp::sql::ToolCallHistoryRepository>(db_path);
    repository->Initialize();

    McpServer server("unit-server", "1.0");
    server.SetToolCallHistoryRepository(repository);
    server.RegisterTool(MakeTool("echo"), [](const json& arguments) {
        ToolResult result;
        result.content.push_back(TextItem(arguments.at("message").get<std::string>()));
        return result;
    });

    const auto result = server.GetTool("echo", json{{"message", "hello"}});
    const auto records = repository->ListRecent(1);

    EXPECT_FALSE(result.is_error);
    ASSERT_EQ(records.size(), 1);
    EXPECT_EQ(records.front().tool_name, "echo");
    EXPECT_FALSE(records.front().is_error);
    EXPECT_NE(records.front().arguments_json.find("\"message\":\"hello\""), std::string::npos);
    EXPECT_NE(records.front().result_json.find("hello"), std::string::npos);
    EXPECT_GE(records.front().duration_ms, 0);

    server.SetToolCallHistoryRepository(nullptr);
    repository.reset();
    std::filesystem::remove(db_path);
}

TEST_F(McpServerTest, RecordsThrownToolCallHistoryWhenRepositoryIsConfigured) {
    const auto db_path = UniqueHistoryDatabasePath("error");
    auto repository = std::make_shared<mcp::sql::ToolCallHistoryRepository>(db_path);
    repository->Initialize();

    McpServer server("unit-server", "1.0");
    server.SetToolCallHistoryRepository(repository);
    server.RegisterTool(MakeTool("fail"), [](const json&) -> ToolResult {
        throw std::runtime_error("boom");
    });

    const auto result = server.GetTool("fail", json::object());
    const auto records = repository->ListRecent(1);

    EXPECT_TRUE(result.is_error);
    ASSERT_EQ(records.size(), 1);
    EXPECT_EQ(records.front().tool_name, "fail");
    EXPECT_TRUE(records.front().is_error);
    EXPECT_EQ(records.front().error_message, "boom");
    EXPECT_NE(records.front().result_json.find("boom"), std::string::npos);

    server.SetToolCallHistoryRepository(nullptr);
    repository.reset();
    std::filesystem::remove(db_path);
}

TEST_F(McpServerTest, RejectsDuplicateToolRegistration) {
    McpServer server("unit-server", "1.0");
    server.RegisterTool(MakeTool("echo"), [](const json&) {
        ToolResult result;
        result.content.push_back(TextItem("first"));
        return result;
    });

    EXPECT_THROW(server.RegisterTool(MakeTool("echo"), [](const json&) {
        ToolResult result;
        result.content.push_back(TextItem("second"));
        return result;
    }), std::runtime_error);
}

TEST_F(McpServerTest, MissingToolThrowsBeforeSseStartEvent) {
    McpServer server("unit-server", "1.0");
    int event_count = 0;
    server.SetSseCallback([&event_count](const json&) {
        ++event_count;
    });

    EXPECT_THROW(server.GetTool("missing", json::object()), std::runtime_error);
    EXPECT_EQ(event_count, 0);
}

TEST_F(McpServerTest, EmitsSseEventsInOrderForSuccessfulToolCall) {
    McpServer server("unit-server", "1.0");
    std::vector<std::string> event_types;
    server.SetSseCallback([&event_types](const json& event) {
        event_types.push_back(event.at("type").get<std::string>());
    });
    server.RegisterTool(MakeTool("ok"), [](const json&) {
        ToolResult result;
        result.content.push_back(TextItem("ok"));
        return result;
    });

    const auto result = server.GetTool("ok", json::object());

    EXPECT_FALSE(result.is_error);
    ASSERT_EQ(event_types.size(), 2);
    EXPECT_EQ(event_types[0], "tool_call_start");
    EXPECT_EQ(event_types[1], "tool_call_end");
}

TEST_F(McpServerTest, EmitsSseEventsInOrderForFailingToolCall) {
    McpServer server("unit-server", "1.0");
    std::vector<std::string> event_types;
    server.SetSseCallback([&event_types](const json& event) {
        event_types.push_back(event.at("type").get<std::string>());
    });
    server.RegisterTool(MakeTool("fail"), [](const json&) -> ToolResult {
        throw std::runtime_error("boom");
    });

    const auto result = server.GetTool("fail", json::object());

    EXPECT_TRUE(result.is_error);
    ASSERT_EQ(event_types.size(), 2);
    EXPECT_EQ(event_types[0], "tool_call_start");
    EXPECT_EQ(event_types[1], "tool_call_error");
}

TEST_F(McpServerTest, SseCallbackCanReenterSetSseCallbackWithoutDeadlock) {
    McpServer server("unit-server", "1.0");
    int callback_calls = 0;
    server.SetSseCallback([&server, &callback_calls](const json&) {
        ++callback_calls;
        server.SetSseCallback([&callback_calls](const json&) {
            ++callback_calls;
        });
    });
    server.RegisterTool(MakeTool("ok"), [](const json&) {
        ToolResult result;
        result.content.push_back(TextItem("ok"));
        return result;
    });

    const auto result = server.GetTool("ok", json::object());

    EXPECT_FALSE(result.is_error);
    EXPECT_EQ(callback_calls, 2);
}

TEST_F(McpServerTest, ResourceProviderCanReadRegistryWithoutDeadlock) {
    McpServer server("unit-server", "1.0");
    Resource resource;
    resource.url = "file:///tmp/example.txt";
    resource.name = "example";
    server.RegisterResource(resource, [&server](const std::string& url) {
        EXPECT_TRUE(server.HasResource(url));
        ResourceContent content;
        content.url = url;
        content.text = "content";
        return content;
    });

    const auto content = server.GetResource("file:///tmp/example.txt");

    EXPECT_EQ(content.text, "content");
}

TEST_F(McpServerTest, RejectsDuplicateResourceRegistrationAndThrowsForMissingResource) {
    McpServer server("unit-server", "1.0");
    Resource resource;
    resource.url = "file:///tmp/example.txt";
    resource.name = "example";
    server.RegisterResource(resource, [](const std::string& url) {
        ResourceContent content;
        content.url = url;
        content.text = "content";
        return content;
    });

    EXPECT_THROW(server.RegisterResource(resource, [](const std::string&) {
        return ResourceContent{};
    }), std::runtime_error);
    EXPECT_THROW(server.GetResource("file:///tmp/missing.txt"), std::runtime_error);
}

TEST_F(McpServerTest, PromptGeneratorCanReadRegistryWithoutDeadlock) {
    McpServer server("unit-server", "1.0");
    Prompt prompt;
    prompt.name = "explain";
    server.RegisterPrompt(prompt, [&server](const json&) {
        EXPECT_TRUE(server.HasPrompt("explain"));
        PromptMessage message;
        message.role = Role::Assistant;
        message.content = json{{"type", "text"}, {"text", "hello"}};
        return std::vector<PromptMessage>{message};
    });

    const auto messages = server.GetPrompt("explain", json::object());

    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.front().role, Role::Assistant);
    EXPECT_EQ(messages.front().content.at("text"), "hello");
}

TEST_F(McpServerTest, RejectsDuplicatePromptRegistrationAndThrowsForMissingPrompt) {
    McpServer server("unit-server", "1.0");
    Prompt prompt;
    prompt.name = "explain";
    server.RegisterPrompt(prompt, [](const json&) {
        return std::vector<PromptMessage>{};
    });

    EXPECT_THROW(server.RegisterPrompt(prompt, [](const json&) {
        return std::vector<PromptMessage>{};
    }), std::runtime_error);
    EXPECT_THROW(server.GetPrompt("missing", json::object()), std::runtime_error);
}

TEST_F(McpServerTest, SetCapabilitiesReplacesDefaultCapabilities) {
    McpServer server("unit-server", "1.0");
    mcp::ServerCapabilities capabilities;
    capabilities.tools = mcp::ServerCapabilities::ToolsCapability{true};
    server.SetCapabilities(capabilities);

    const auto result = server.GetInitializationResult();

    ASSERT_TRUE(result.server_cap.tools.has_value());
    EXPECT_TRUE(result.server_cap.tools->list_changed);
    EXPECT_FALSE(result.server_cap.resources.has_value());
    EXPECT_FALSE(result.server_cap.prompts.has_value());
}
