#include <mcp/mcp_types.h>
#include <gtest/gtest.h>

#include <string>

namespace {

using mcp::ContentItem;
using mcp::InitializeResult;
using mcp::Prompt;
using mcp::PromptArgument;
using mcp::PromptMessage;
using mcp::Resource;
using mcp::ResourceContent;
using mcp::Role;
using mcp::ServerCapabilities;
using mcp::ServerInfo;
using mcp::Tool;
using mcp::ToolInputSchema;
using mcp::ToolResult;
using mcp::json;

}  // namespace

TEST(McpTypesTest, ToolUsesMcpInputSchemaFieldAndRoundTrips) {
    Tool tool;
    tool.name = "echo";
    tool.description = "Echo a message";
    tool.input_schema.properties = json{
            {"message", {{"type", "string"}}}
    };
    tool.input_schema.required = {"message"};

    const json serialized = tool.to_json();

    EXPECT_TRUE(serialized.contains("inputSchema"));
    EXPECT_FALSE(serialized.contains("input_schema"));
    EXPECT_FALSE(serialized.contains("ToolInputSchema"));
    EXPECT_EQ(serialized.at("inputSchema").at("properties").at("message").at("type"), "string");

    const Tool parsed = Tool::from_json(serialized);
    EXPECT_EQ(parsed.name, "echo");
    EXPECT_EQ(parsed.description, "Echo a message");
    EXPECT_EQ(parsed.input_schema.properties.at("message").at("type"), "string");
    ASSERT_EQ(parsed.input_schema.required.size(), 1);
    EXPECT_EQ(parsed.input_schema.required.front(), "message");
}

TEST(McpTypesTest, ToolResultUsesMcpContentFieldsAndRoundTrips) {
    ContentItem text;
    text.type = "text";
    text.text = "done";

    ContentItem image;
    image.type = "image";
    image.data = "base64-data";
    image.mime_type = "image/png";

    ToolResult result;
    result.content = {text, image};
    result.is_error = true;

    const json serialized = result.to_json();

    EXPECT_TRUE(serialized.contains("isError"));
    EXPECT_FALSE(serialized.contains("is_error"));
    EXPECT_FALSE(serialized.contains("error"));
    EXPECT_EQ(serialized.at("content").at(1).at("mimeType"), "image/png");
    EXPECT_FALSE(serialized.at("content").at(1).contains("mime_type"));
    EXPECT_FALSE(serialized.at("content").at(1).contains("mime"));

    const ToolResult parsed = ToolResult::from_json(serialized);
    ASSERT_EQ(parsed.content.size(), 2);
    EXPECT_TRUE(parsed.is_error);
    ASSERT_TRUE(parsed.content[1].mime_type.has_value());
    EXPECT_EQ(parsed.content[1].mime_type.value(), "image/png");
}

TEST(McpTypesTest, ResourceTypesUseMimeTypeAndRoundTrip) {
    Resource resource;
    resource.url = "file:///tmp/report.txt";
    resource.name = "report";
    resource.mime_type = "text/plain";

    const json serialized_resource = resource.to_json();
    EXPECT_EQ(serialized_resource.at("mimeType"), "text/plain");
    EXPECT_FALSE(serialized_resource.contains("mime_type"));

    const Resource parsed_resource = Resource::from_json(serialized_resource);
    ASSERT_TRUE(parsed_resource.mime_type.has_value());
    EXPECT_EQ(parsed_resource.mime_type.value(), "text/plain");

    ResourceContent content;
    content.url = "file:///tmp/report.txt";
    content.text = "hello";
    content.mime_type = "text/plain";

    const json serialized_content = content.to_json();
    EXPECT_EQ(serialized_content.at("mimeType"), "text/plain");
    EXPECT_FALSE(serialized_content.contains("mime_type"));

    const ResourceContent parsed_content = ResourceContent::from_json(serialized_content);
    ASSERT_TRUE(parsed_content.mime_type.has_value());
    EXPECT_EQ(parsed_content.mime_type.value(), "text/plain");
}

TEST(McpTypesTest, CapabilitiesUseListChangedAndRoundTrip) {
    ServerCapabilities capabilities;
    capabilities.tools = ServerCapabilities::ToolsCapability{true};
    capabilities.resources = ServerCapabilities::ResourceCapability{true, true};
    capabilities.prompts = ServerCapabilities::PromptCapability{true};
    capabilities.logging = json::object();

    const json serialized = capabilities.to_json();

    EXPECT_EQ(serialized.at("tools").at("listChanged"), true);
    EXPECT_EQ(serialized.at("resources").at("subscribe"), true);
    EXPECT_EQ(serialized.at("resources").at("listChanged"), true);
    EXPECT_EQ(serialized.at("prompts").at("listChanged"), true);
    EXPECT_FALSE(serialized.at("tools").contains("list_changed"));

    const ServerCapabilities parsed = ServerCapabilities::from_json(serialized);
    ASSERT_TRUE(parsed.tools.has_value());
    ASSERT_TRUE(parsed.resources.has_value());
    ASSERT_TRUE(parsed.prompts.has_value());
    EXPECT_TRUE(parsed.tools->list_changed);
    EXPECT_TRUE(parsed.resources->subscribe);
    EXPECT_TRUE(parsed.resources->list_changed);
    EXPECT_TRUE(parsed.prompts->list_changed);
    ASSERT_TRUE(parsed.logging.has_value());
    EXPECT_TRUE(parsed.logging->is_object());
}

TEST(McpTypesTest, InitializeResultUsesMcpFieldsAndRoundTrips) {
    InitializeResult result;
    result.protocol_version = "2024-11-05";
    result.server_info = ServerInfo{"mcp-test", "0.1.0"};
    result.server_cap.tools = ServerCapabilities::ToolsCapability{true};

    const json serialized = result.to_json();

    EXPECT_EQ(serialized.at("protocolVersion"), "2024-11-05");
    EXPECT_TRUE(serialized.contains("capabilities"));
    EXPECT_TRUE(serialized.contains("serverInfo"));
    EXPECT_FALSE(serialized.contains("protocol_version"));
    EXPECT_FALSE(serialized.contains("server_cap"));
    EXPECT_FALSE(serialized.contains("server_info"));
    EXPECT_FALSE(serialized.contains("serverCap"));

    const InitializeResult parsed = InitializeResult::from_json(serialized);
    EXPECT_EQ(parsed.protocol_version, "2024-11-05");
    EXPECT_EQ(parsed.server_info.name, "mcp-test");
    EXPECT_EQ(parsed.server_info.version, "0.1.0");
    ASSERT_TRUE(parsed.server_cap.tools.has_value());
    EXPECT_TRUE(parsed.server_cap.tools->list_changed);
}

TEST(McpTypesTest, PromptWithoutArgumentsParsesAsEmptyArguments) {
    const json serialized = {
            {"name", "explain"}
    };

    const Prompt prompt = Prompt::from_json(serialized);

    EXPECT_EQ(prompt.name, "explain");
    EXPECT_TRUE(prompt.arguments.empty());
}

TEST(McpTypesTest, PromptArgumentsSerializeOptionalDescriptionAndRoundTrip) {
    Prompt prompt;
    prompt.name = "summarize";
    prompt.description = "Summarize text";
    prompt.arguments = {
            PromptArgument{.name = "text", .description = "Input text", .required = true},
            PromptArgument{.name = "tone", .description = std::nullopt, .required = false}
    };

    const json serialized = prompt.to_json();

    EXPECT_EQ(serialized.at("description"), "Summarize text");
    ASSERT_EQ(serialized.at("arguments").size(), 2);
    EXPECT_EQ(serialized.at("arguments").at(0).at("description"), "Input text");
    EXPECT_FALSE(serialized.at("arguments").at(1).contains("description"));

    const Prompt parsed = Prompt::from_json(serialized);
    ASSERT_EQ(parsed.arguments.size(), 2);
    EXPECT_EQ(parsed.arguments[0].name, "text");
    ASSERT_TRUE(parsed.arguments[0].description.has_value());
    EXPECT_EQ(parsed.arguments[0].description.value(), "Input text");
    EXPECT_TRUE(parsed.arguments[0].required);
    EXPECT_FALSE(parsed.arguments[1].required);
}

TEST(McpTypesTest, PromptMessageSerializesRolesAndContent) {
    PromptMessage user_message;
    user_message.role = Role::User;
    user_message.content = json{{"type", "text"}, {"text", "hello"}};

    const json serialized_user = user_message.to_json();
    EXPECT_EQ(serialized_user.at("role"), "user");
    EXPECT_EQ(serialized_user.at("content").at("text"), "hello");

    const PromptMessage parsed_assistant = PromptMessage::from_json(json{
            {"role", "assistant"},
            {"content", {{"type", "text"}, {"text", "hi"}}}
    });
    EXPECT_EQ(parsed_assistant.role, Role::Assistant);
    EXPECT_EQ(parsed_assistant.content.at("text"), "hi");
}

TEST(McpTypesTest, EmptyCapabilitiesSerializeAsEmptyObject) {
    const ServerCapabilities capabilities;

    const json serialized = capabilities.to_json();

    EXPECT_TRUE(serialized.is_object());
    EXPECT_TRUE(serialized.empty());
}
