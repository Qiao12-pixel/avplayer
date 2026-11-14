/**
 * @file mcp_server.h
 * @brief MCP 服务器核心类
 * @author Joe
 * @date 26-4-30
 */


#ifndef MCP_SERVER_H
#define MCP_SERVER_H
#include "mcp_types.h"

#include <functional>
#include <unordered_map>
#include <memory>
#include <mutex>
// MCP 服务器核心类
// 提供 Tools、Resources、Prompts 的管理功能
namespace mcp {
    namespace sql {
        class ToolCallHistoryRepository;
    }

    class McpServer {
    public:
        // ===== Tool 相关类型 =====
        using ToolHandler = std::function<ToolResult(const json& arguments)>;
        using ResourceProvider = std::function<ResourceContent(const std::string& url)>;
        using PromptGenerator = std::function<std::vector<PromptMessage>(const json& arguments)>;

        McpServer(const std::string& name, const std::string& version);
        //获取初始化结果
        InitializeResult GetInitializationResult() const;//握手响应。它包含了ServerInfo,ServerCapabilities，是连接建立成功的标志。
        //设置服务器能力
        void SetCapabilities(const ServerCapabilities& cap);
        void SetCapbilities(const ServerCapabilities& cap);
        //Tools
        void RegisterTool(const Tool& tool, const ToolHandler handler);
        std::vector<Tool> ListTools() const;
        bool HasTool(const std::string& name) const;
        ToolResult GetTool(const std::string& name, const json& argument);
        void SetToolCallHistoryRepository(std::shared_ptr<sql::ToolCallHistoryRepository> repository);

        //Resources
        void RegisterResource(const Resource& resource, ResourceProvider provider);
        std::vector<Resource> ListResources() const;
        bool HasResource(const std::string& url) const;
        ResourceContent GetResource(const std::string& url);

        //Prompts
        void RegisterPrompt(const Prompt& prompt, PromptGenerator generator);
        std::vector<Prompt> ListPrompts() const;
        bool HasPrompt(const std::string& name) const;
        std::vector<PromptMessage> GetPrompt(const std::string& name, const json& arguments);

        //SSE事件回调--http_server
        using SseEventCallback = std::function<void(const json&)>;
        void SetSseCallback(SseEventCallback callback);

    private:
        ServerInfo m_server_info_;
        ServerCapabilities m_capabilities_;

        //Tools
        std::unordered_map<std::string, Tool> m_tools_;
        std::unordered_map<std::string, ToolHandler> m_tool_handlers_;
        mutable std::mutex m_tools_mutex_;//允许再const成员函数进行修改锁

        //Resources
        std::unordered_map<std::string, Resource> m_resources_;
        std::unordered_map<std::string, ResourceProvider> m_resources_providers_;
        mutable std::mutex m_resources_mutex_;

        //Prompts
        std::unordered_map<std::string, Prompt> m_prompts_;
        std::unordered_map<std::string, PromptGenerator> m_prompts_generators_;
        mutable std::mutex m_prompt_mutex_;

        SseEventCallback m_sse_callback_;
        mutable std::mutex m_sse__mutex_;

        std::shared_ptr<sql::ToolCallHistoryRepository> m_tool_history_repository_;
        mutable std::mutex m_tool_history_mutex_;

        std::shared_ptr<sql::ToolCallHistoryRepository> GetToolCallHistoryRepository() const;
    };
}



#endif //MCP_SERVER_H
