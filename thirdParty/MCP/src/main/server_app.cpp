#include "server_app.h"

#include <atomic>
#include <condition_variable>
#include <csignal>
#include <ctime>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "config/config.h"
#include "json_rpc/http_jsonrpc.h"
#include "json_rpc/stdio_jsonrpc_server.h"
#include "load_balancer/load_balancer.h"
#include "logger/logger.h"
#include "mcp_builtin_prompts.h"
#include "mcp_builtin_resources.h"
#include "mcp_builtin_tools.h"
#include "mcp_job_agent_tools.h"
#include "mcp_redis_tools.h"
#ifdef MCP_WITH_AVPLAYER
#include "mcp_avplayer_tools.h"
#endif
#include "sql/tool_call_history_repository.h"
#include "zk/zk_service_registry.h"

namespace mcp {
    namespace {
        std::atomic<bool> g_running{true};
        std::unique_ptr<jsonrpc::HttpJsonRpcServer> g_http_server{nullptr};

        void SignalHandler(int signal) {
            std::cerr << "\nReceived signal " << signal << ", shutting down..." << std::endl;
            g_running = false;
            if (g_http_server) {
                g_http_server->Stop();
            }
        }
    }

    spdlog::level::level_enum StringToLogLevel(const std::string& level) {
        if (level == "trace") {
            return spdlog::level::trace;
        } else if (level == "debug") {
            return spdlog::level::debug;
        } else if (level == "info") {
            return spdlog::level::info;
        } else if (level == "warn") {
            return spdlog::level::warn;
        } else if (level == "error") {
            return spdlog::level::err;
        } else if (level == "critical") {
            return spdlog::level::critical;
        }
        return spdlog::level::info;
    }

    void ConfigureMcpServer(McpServer& mcp_server) {
        ServerCapabilities capabilities;
        capabilities.tools = ServerCapabilities::ToolsCapability{false};
        capabilities.resources = ServerCapabilities::ResourceCapability{false, false};
        capabilities.prompts = ServerCapabilities::PromptCapability{false};
        mcp_server.SetCapabilities(capabilities);

        RegisterBuiltinTools(mcp_server);
        RegisterBuiltinResources(mcp_server);
        RegisterBuiltinPrompts(mcp_server);
        RegisterJobAgentTools(mcp_server);
        RegisterRedisTools(mcp_server);
#ifdef MCP_WITH_AVPLAYER
        RegisterAVPlayerTools(mcp_server);
#endif

        try {
            auto history_repository =
                std::make_shared<sql::ToolCallHistoryRepository>("data/tool_call_history.sqlite3");
            history_repository->Initialize();
            mcp_server.SetToolCallHistoryRepository(history_repository);
            MCP_LOG_INFO("Tool call history database enabled: {}", history_repository->DatabasePath().string());
        } catch (const std::exception& e) {
            MCP_LOG_WARN("Tool call history database disabled: {}", e.what());
        }

        MCP_LOG_INFO("MCP setup complete: {} tools, {} resources, {} prompts",
                     mcp_server.ListTools().size(),
                     mcp_server.ListResources().size(),
                     mcp_server.ListPrompts().size());
    }

    jsonrpc::JsonRpcDispatcher CreateDispatcher(McpServer& mcp_server) {
        jsonrpc::JsonRpcDispatcher dispatcher;
        dispatcher.EnableThreadPool(config::MCP_CONFIG.GetThreadPoolSize(),
                                    config::MCP_CONFIG.GetThreadPoolMaxQueueSize(),
                                    config::MCP_CONFIG.GetThreadPoolPooledMethods());

        dispatcher.RegisterHandler("initialize", [&mcp_server](const json& /*param*/) -> json {
            MCP_LOG_INFO("Initializing MCP server");
            return mcp_server.GetInitializationResult().to_json();
        });

        dispatcher.RegisterHandler("notifications/initialized", [](const json&) -> json { return json::object(); });

        dispatcher.RegisterHandler("tools/list", [&mcp_server](const json& /*params*/) -> json {
            json tools_arr = json::array();
            for (const auto& tool : mcp_server.ListTools()) {
                tools_arr.emplace_back(tool.to_json());
            }
            return {{"tools", tools_arr}};
        });

        dispatcher.RegisterHandler("tools/call", [&mcp_server](const json& params) -> json {
            std::string name = params.value("name", "");
            json arguments = params.value("arguments", json::object());
            MCP_LOG_INFO("Calling tool: {}", name);
            auto result = mcp_server.GetTool(name, arguments);
            return result.to_json();
        });

        dispatcher.RegisterHandler("resources/list", [&mcp_server](const json& /*params*/) -> json {
            json resources_arr = json::array();
            for (const auto& res : mcp_server.ListResources()) {
                resources_arr.emplace_back(res.to_json());
            }
            return {{"resources", resources_arr}};
        });

        dispatcher.RegisterHandler("resources/read", [&mcp_server](const json& params) -> json {
            std::string url = params.value("url", "");
            MCP_LOG_INFO("Reading resources: {}", url);
            auto content = mcp_server.GetResource(url);
            json contents_arr = json::array();
            contents_arr.emplace_back(content.to_json());
            return {{"content", contents_arr}};
        });

        dispatcher.RegisterHandler("prompts/list", [&mcp_server](const json& /*params*/) -> json {
            json prompts_arr = json::array();
            for (const auto& prompt : mcp_server.ListPrompts()) {
                prompts_arr.emplace_back(prompt.to_json());
            }
            return {{"prompts", prompts_arr}};
        });

        dispatcher.RegisterHandler("prompts/get", [&mcp_server](const json& params) -> json {
            std::string name = params.value("name", "");
            json arguments = params.value("arguments", json::object());
            MCP_LOG_INFO("Reading prompts: {}", name);
            auto messages = mcp_server.GetPrompt(name, arguments);
            json messages_arr = json::array();
            for (const auto& message : messages) {
                messages_arr.emplace_back(message.to_json());
            }
            return {{"messages", messages_arr}};
        });

        return dispatcher;
    }

    void InstallSignalHandlers() {
        std::signal(SIGINT, SignalHandler);
        std::signal(SIGTERM, SignalHandler);
    }

    void RunHttpMode(McpServer& mcp_server, const std::string& host, int port) {
        MCP_LOG_INFO("Running HTTP Server on {}:{}", host, port);
        auto dispatcher = CreateDispatcher(mcp_server);
        g_http_server = std::make_unique<jsonrpc::HttpJsonRpcServer>(std::move(dispatcher), host, port);

        struct {
            std::vector<json> events;
            std::mutex mutex;
            std::condition_variable cv;
        } event_queue;

        mcp_server.SetSseCallback([&event_queue](const json& event) {
            std::lock_guard<std::mutex> lock(event_queue.mutex);
            event_queue.events.emplace_back(event);
            event_queue.cv.notify_all();
        });

        g_http_server->RegisterSseEndpoint("/sse/events", [&mcp_server](const auto& send) {
            MCP_LOG_INFO("SSE events client connected");
            send(json({{"type", "connected"}, {"message", "Server events stream"}}).dump());

            int count = 0;
            while (g_running.load()) {
                json status = {
                    {"type", "server_status"},
                    {"timestamp", std::time(nullptr)},
                    {"tools_count", mcp_server.ListTools().size()},
                    {"resources_count", mcp_server.ListResources().size()},
                    {"prompts_count", mcp_server.ListPrompts().size()},
                    {"uptime_seconds", count++}
                };
                send(status.dump());
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
            MCP_LOG_INFO("SSE events client disconnected");
        });

        g_http_server->RegisterSseEndpoint("/sse/tool_calls", [&event_queue](const auto& send) {
            MCP_LOG_INFO("SSE tool_calls client connected");
            send(json({{"type", "connected"}, {"message", "Tool calls monitoring"}}).dump());
            while (g_running.load()) {
                std::unique_lock<std::mutex> lock(event_queue.mutex);

                event_queue.cv.wait_for(lock, std::chrono::seconds(1), [&event_queue]() {
                    return !event_queue.events.empty();
                });
                for (const auto& event : event_queue.events) {
                    send(event.dump());
                }
                event_queue.events.clear();
            }
            MCP_LOG_INFO("SSE tool calls client disconnected");
        });

        g_http_server->Run();
        MCP_LOG_INFO("HTTP Server terminated");
    }

    void RunStdioMode(McpServer& mcp_server) {
        MCP_LOG_INFO("MCP server started");
        auto dispatcher = CreateDispatcher(mcp_server);
        jsonrpc::StdioJsonRpcServer stdio_server(std::move(dispatcher));

        stdio_server.Run();

        MCP_LOG_INFO("MCP server stopped");
    }

    void RunBothModes(McpServer& mcp_server, const std::string& host, int port) {
        MCP_LOG_INFO("Starting both HTTP and stdio servers");

        std::thread http_thread([&mcp_server, &host, port]() {
            RunHttpMode(mcp_server, host, port);
        });
        RunStdioMode(mcp_server);

        if (http_thread.joinable()) {
            http_thread.join();
        }
    }

    void RunWorkerMode(McpServer& mcp_server, const std::string& host, int port,
                       const std::string& zk_hosts) {
        MCP_LOG_INFO("Starting worker mode on {}:{} with ZooKeeper {}", host, port, zk_hosts);

        // Register with ZooKeeper.
        zk::ZkServiceRegistry zk(zk_hosts);
        zk.Connect();
        zk.Register(host, port);

        // Run HTTP server (blocking).
        RunHttpMode(mcp_server, host, port);

        // Cleanup.
        zk.Unregister();
        MCP_LOG_INFO("Worker mode shutdown complete");
    }

    void RunProxyMode(const std::string& zk_hosts, int proxy_port) {
        MCP_LOG_INFO("Starting proxy mode on port {} with ZooKeeper {}", proxy_port, zk_hosts);

        int default_weight = 1;
        try {
            default_weight = config::MCP_CONFIG.GetLbDefaultWeight();
        } catch (...) { /* use default */ }

        lb::LoadBalancer lb(proxy_port, zk_hosts, default_weight);
        lb.Start();

        MCP_LOG_INFO("Proxy mode shutdown complete");
    }
}
