/**
 * @file mcp_types.h
 * @brief MCP 协议核心类型定义
 * @author Joe
 * @date 26-4-29
 */

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <optional>
#include <variant>

#ifndef MCP_TYPES_H
#define MCP_TYPES_H
namespace mcp {
    using json = nlohmann::json;
    // MCP 协议版本
    constexpr const char* LATEST_PROTOCOL_VERSION = "2024-11-05";
    constexpr const char* DEFAULT_NEGOTIATED_VERSION = "2024-11-05";

    // 进度令牌类型
    using ProgressToken = std::variant<std::string, int64_t>;

    // 光标类型（用于分页）Tools list
    using Cursor = std::string;

    // 角色类型
    enum class Role {
        User,
        Assistant
    };

    //工具输入Schema(Json Schema)
    struct ToolInputSchema {
        std::string type = "object";
        json properties;
        std::vector<std::string> required;

        json to_json() const;
        static ToolInputSchema from_json(const json &j);
    };
    struct Tool {
        std::string name;
        std::string description;
        ToolInputSchema input_schema;

        json to_json() const;
        static Tool from_json(const json &j);
    };

    //工具调用结果内容
    struct ContentItem {
        std::string type;// "text", "image", "resource"
        std::optional<std::string> text;
        std::optional<std::string> data;// base64 for image
        std::optional<std::string> mime_type;//图片："image/png" 或 "image/jpeg" 文件："application/pdf"
        std::optional<std::string> url;//resource

        json to_json() const;
        static ContentItem from_json(const json &j);
    };

    struct ToolResult {
        std::vector<ContentItem> content;
        bool is_error = false;

        json to_json() const;
        static ToolResult from_json(const json &j);
    };

    struct Resource {
        std::string url;
        std::string name;
        std::optional<std::string> description;
        std::optional<std::string> mime_type;

        json to_json() const;
        static Resource from_json(const json &j);
    };

    struct ResourceContent {
        std::string url;
        std::string text;// 文本内容
        std::optional<std::string> mime_type;
        std::optional<std::string> blob;// base64 编码的二进制内容[image]

        json to_json() const;
        static ResourceContent from_json(const json &j);
    };


    struct PromptArgument {
        std::string name;
        std::optional<std::string> description;
        bool required = false;

        json to_json() const;
        static PromptArgument from_json(const json &j);
    };
    struct Prompt {
        std::string name;
        std::optional<std::string> description;
        std::vector<PromptArgument> arguments;

        json to_json() const;
        static Prompt from_json(const json &j);
    };
    struct PromptMessage {
        Role role;
        json content;

        json to_json() const;
        static PromptMessage from_json(const json &j);
    };

    struct ServerCapabilities {
        struct ToolsCapability {
            bool list_changed = false;// 工具列表是否发生变化

            json to_json() const;
            static ToolsCapability from_json(const json &j);
        };
        struct ResourceCapability {
            bool subscribe = false;
            bool list_changed = false;

            json to_json() const;
            static ResourceCapability from_json(const json &j);
        };
        struct PromptCapability {
            bool list_changed = false;

            json to_json() const;
            static PromptCapability from_json(const json &j);
        };

        std::optional<ToolsCapability> tools;
        std::optional<ResourceCapability> resources;
        std::optional<PromptCapability> prompts;
        std::optional<json> logging;

        json to_json() const;
        static ServerCapabilities from_json(const json &j);
    };

    struct ServerInfo {
        std::string name;//服务器名字
        std::string version;//服务器版本

        json to_json() const;
        static ServerInfo from_json(const json &j);
    };

    struct InitializeResult {
        std::string protocol_version;
        ServerCapabilities server_cap;
        ServerInfo server_info;

        json to_json() const;
        static InitializeResult from_json(const json &j);
    };
}



#endif //MCP_TYPES_H
