/**
 * @file mcp_server.cpp
 * @brief 
 * @author Joe
 * @date 26-4-30
 */


#include "mcp_server.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "../src/logger/logger.h"
#include "sql/tool_call_history_repository.h"

namespace {
    std::string FormatUtcTimestamp(const std::chrono::system_clock::time_point& time_point) {
        const auto time = std::chrono::system_clock::to_time_t(time_point);
        std::tm tm{};
#if defined(_WIN32)
        gmtime_s(&tm, &time);
#else
        gmtime_r(&time, &tm);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }

    int64_t DurationMillis(const std::chrono::system_clock::time_point& started,
                           const std::chrono::system_clock::time_point& finished) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count();
    }

    std::string ExtractErrorMessage(const mcp::ToolResult& result) {
        if (!result.is_error) {
            return "";
        }

        for (const auto& item : result.content) {
            if (item.text.has_value()) {
                return item.text.value();
            }
        }
        return "";
    }

    void RecordToolCallHistory(
        const std::shared_ptr<mcp::sql::ToolCallHistoryRepository>& repository,
        const std::string& name,
        const mcp::json& arguments,
        const mcp::ToolResult& result,
        const std::chrono::system_clock::time_point& started,
        const std::chrono::system_clock::time_point& finished,
        const std::string& error_message = "") {
        if (!repository) {
            return;
        }

        try {
            mcp::sql::ToolCallRecord record;
            record.tool_name = name;
            record.arguments_json = arguments.dump();
            record.is_error = result.is_error;
            record.result_json = result.to_json().dump();
            record.error_message = error_message.empty() ? ExtractErrorMessage(result) : error_message;
            record.started_at = FormatUtcTimestamp(started);
            record.finished_at = FormatUtcTimestamp(finished);
            record.duration_ms = DurationMillis(started, finished);
            repository->Insert(record);
        } catch (const std::exception& e) {
            MCP_LOG_WARN("record tool call history failed: {}", e.what());
        }
    }
}

namespace mcp {
    McpServer::McpServer(const std::string &name, const std::string &version) {
        m_server_info_.name = name;
        m_server_info_.version = version;

        //设置默认能力
        m_capabilities_.tools = ServerCapabilities::ToolsCapability{false};
        m_capabilities_.resources = ServerCapabilities::ResourceCapability{false, false};
        m_capabilities_.prompts = ServerCapabilities::PromptCapability{false};
    }
    InitializeResult McpServer::GetInitializationResult() const {
        InitializeResult result;
        result.protocol_version = LATEST_PROTOCOL_VERSION;
        result.server_cap = m_capabilities_;
        result.server_info = m_server_info_;
        return result;
    }

    void McpServer::SetCapabilities(const ServerCapabilities &cap) {
        m_capabilities_ = cap;
    }

    void McpServer::SetCapbilities(const ServerCapabilities &cap) {
        SetCapabilities(cap);
    }

    //Tools
    void McpServer::RegisterTool(const Tool &tool, const ToolHandler handler) {
        std::lock_guard<std::mutex> lock(m_tools_mutex_);

        if (m_tools_.find(tool.name) != m_tools_.end()) {
            throw std::runtime_error("McpServer::RegisterTool: Tool already registered" + tool.name);
        }
        m_tools_[tool.name] = tool;
        m_tool_handlers_[tool.name] = std::move(handler);
    }
    std::vector<Tool> McpServer::ListTools() const {
        std::lock_guard<std::mutex> lock(m_tools_mutex_);
        std::vector<Tool> result;
        for (const auto& [name, tool] : m_tools_) {
            result.emplace_back(tool);
        }
        return result;
    }
    bool McpServer::HasTool(const std::string &name) const {
        std::lock_guard<std::mutex> lock(m_tools_mutex_);
        return m_tools_.find(name) != m_tools_.end();
    }
    void McpServer::SetToolCallHistoryRepository(std::shared_ptr<sql::ToolCallHistoryRepository> repository) {
        std::lock_guard<std::mutex> lock(m_tool_history_mutex_);
        m_tool_history_repository_ = std::move(repository);
    }

    std::shared_ptr<sql::ToolCallHistoryRepository> McpServer::GetToolCallHistoryRepository() const {
        std::lock_guard<std::mutex> lock(m_tool_history_mutex_);
        return m_tool_history_repository_;
    }

    ToolResult McpServer::GetTool(const std::string &name, const json& argument) {
        ToolHandler handler;
        {
            std::lock_guard<std::mutex> lock(m_tools_mutex_);
            auto it = m_tool_handlers_.find(name);
            if (it == m_tool_handlers_.end()) {
                throw std::runtime_error("McpServer::GetTool: Tool not found: " + name);
            }
            handler = it->second;
        }

        MCP_LOG_INFO("McpServer::GetTool: " + name);
        const auto started = std::chrono::system_clock::now();

        // 推送工具调用开始事件
        {
            McpServer::SseEventCallback sse_callback;
            {
                std::lock_guard<std::mutex> lock(m_sse__mutex_);
                sse_callback = m_sse_callback_;
            }
            if (sse_callback) {
                sse_callback(json({
                    {"type", "tool_call_start"},
                    {"tool", name},
                    {"arguments", argument},
                    {"timestamp", std::time(nullptr)}
                }));
            }
        }
        try {
            auto result = handler(argument);
            const auto finished = std::chrono::system_clock::now();
            // 推送工具调用完成事件
            {
                McpServer::SseEventCallback sse_callback;
                {
                    std::lock_guard<std::mutex> sse_lock(m_sse__mutex_);
                    sse_callback = m_sse_callback_;
                }
                if (sse_callback) {
                    sse_callback(json({
                        {"type", "tool_call_end"},
                        {"tool", name},
                        {"success", !result.is_error},
                        {"timestamp", std::time(nullptr)}
                    }));
                }
            }
            RecordToolCallHistory(GetToolCallHistoryRepository(), name, argument, result, started, finished);
            return result;
        } catch (const std::exception &e) {
            const auto finished = std::chrono::system_clock::now();
            // 推送工具调用错误事件
            {
                McpServer::SseEventCallback sse_callback;
                {
                    std::lock_guard<std::mutex> sse_lock(m_sse__mutex_);
                    sse_callback = m_sse_callback_;
                }
                if (sse_callback) {
                    sse_callback(json({
                        {"type", "tool_call_error"},
                        {"tool", name},
                        {"error", e.what()},
                        {"timestamp", std::time(nullptr)}
                    }));
                }
            }
            ToolResult error_result;
            error_result.is_error = true;
            ContentItem item;
            item.type = "text";
            item.text = std::string("error calling tool: ") + e.what();
            error_result.content.emplace_back(item);
            RecordToolCallHistory(GetToolCallHistoryRepository(), name, argument, error_result, started, finished, e.what());
            return error_result;
        }
    }

    //Resource
    void McpServer::RegisterResource(const Resource &resource, ResourceProvider provider) {
        std::lock_guard<std::mutex> lock(m_resources_mutex_);
        if (m_resources_.find(resource.url) != m_resources_.end()) {
            throw std::runtime_error("McpServer::RegisterResource: Resource already registered");
        }
        m_resources_[resource.url] = resource;
        m_resources_providers_[resource.url] = std::move(provider);
    }

    std::vector<Resource> McpServer::ListResources() const {
        std::lock_guard<std::mutex> lock(m_resources_mutex_);
        std::vector<Resource> result;
        for (const auto& [url, resource] : m_resources_) {
            result.emplace_back(resource);
        }
        return result;
    }

    bool McpServer::HasResource(const std::string &url) const {
        std::lock_guard<std::mutex> lock(m_resources_mutex_);
        return m_resources_.find(url) != m_resources_.end();
    }

    ResourceContent McpServer::GetResource(const std::string &url) {
        ResourceProvider provider;
        {
            std::lock_guard<std::mutex> lock(m_resources_mutex_);
            auto it = m_resources_providers_.find(url);
            if (it == m_resources_providers_.end()) {
                throw std::runtime_error("McpServer::GetResource: Resource not found: " + url);
            }
            provider = it->second;
        }
        auto result = provider(url);
        return result;
    }

    //Prompts
    void McpServer::RegisterPrompt(const Prompt &prompt, PromptGenerator generator) {
        std::lock_guard<std::mutex> lock(m_prompt_mutex_);
        if (m_prompts_.find(prompt.name) != m_prompts_.end()) {
            throw std::runtime_error("Prompt already registered: " + prompt.name);
        }
        m_prompts_[prompt.name] = prompt;
        m_prompts_generators_[prompt.name] = std::move(generator);
    }
    std::vector<Prompt> McpServer::ListPrompts() const {
        std::lock_guard<std::mutex> lock(m_prompt_mutex_);
        std::vector<Prompt> result;
        for (const auto& [name, prompt] : m_prompts_) {
            result.emplace_back(prompt);
        }
        return result;
    }
    bool McpServer::HasPrompt(const std::string &name) const {
        std::lock_guard<std::mutex> lock(m_prompt_mutex_);
        return m_prompts_.find(name) != m_prompts_.end();
    }
    std::vector<PromptMessage> McpServer::GetPrompt(const std::string& name, const json& arguments) {
        PromptGenerator generator;
        {
            std::lock_guard<std::mutex> lock(m_prompt_mutex_);
            auto it = m_prompts_generators_.find(name);
            if (it == m_prompts_generators_.end()) {
                throw std::runtime_error("McpServer::GetPrompt: Prompt not found: " + name);
            }
            generator = it->second;
        }
        auto result = generator(arguments);
        MCP_LOG_INFO("McpServer::GetPrompt Use Prompt: " + name);
        return result;
    }
    //set_sse
    void McpServer::SetSseCallback(SseEventCallback callback) {
        std::lock_guard<std::mutex> lock(m_sse__mutex_);
        m_sse_callback_ = std::move(callback);
    }











}
