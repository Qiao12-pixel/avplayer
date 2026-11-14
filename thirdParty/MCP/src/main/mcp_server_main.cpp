#include <iostream>
#include <string>
#include <filesystem>
#include <cstdlib>

#include "config/config.h"
#include "logger/logger.h"
#include "mcp/mcp_server.h"
#include "server_app.h"

using namespace mcp;

int main(int argc, char* argv[]) {
    auto executor=(std::filesystem::absolute(argv[0]).parent_path()/"mcp_media_executor").string();
    setenv("MCP_MEDIA_EXECUTOR",executor.c_str(),0);
    std::string config_file = "../config/server.json";
    std::string mode = "standalone";
    std::string host;
    int port = 0;
    std::string zk_hosts;
    int proxy_port = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_file = argv[++i];
        } else if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
        } else if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--zk-hosts" && i + 1 < argc) {
            zk_hosts = argv[++i];
        } else if (arg == "--proxy-port" && i + 1 < argc) {
            proxy_port = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]\n"
                      << "Options:\n"
                      << "  --mode MODE       Server mode: standalone (default), worker, proxy\n"
                      << "  --config FILE     Configuration file path\n"
                      << "  --host HOST       Server host (for HTTP/worker mode)\n"
                      << "  --port PORT       Server port (for HTTP/worker mode)\n"
                      << "  --zk-hosts HOSTS  ZooKeeper connection string (for worker/proxy mode)\n"
                      << "  --proxy-port PORT Proxy listen port (for proxy mode)\n"
                      << "  --help, -h        Show this help\n"
                      << "\nExamples:\n"
                      << "  " << argv[0] << " --mode standalone --port 8081\n"
                      << "  " << argv[0] << " --mode worker --port 8081 --zk-hosts localhost:2181\n"
                      << "  " << argv[0] << " --mode proxy --proxy-port 8090 --zk-hosts localhost:2181\n";
            return 0;
        }
    }

    if (mode != "standalone" && mode != "worker" && mode != "proxy") {
        std::cerr << "Invalid mode: " << mode
                  << " (must be standalone, worker, or proxy)" << std::endl;
        return 1;
    }

    // Proxy mode doesn't need a full MCP server; it only connects to ZooKeeper.
    if (mode == "proxy") {
        if (proxy_port == 0) {
            std::cerr << "proxy mode requires --proxy-port" << std::endl;
            return 1;
        }
        if (zk_hosts.empty()) {
            std::cerr << "proxy mode requires --zk-hosts" << std::endl;
            return 1;
        }

        MCP_LOG_INIT("mcp_proxy", "logs/proxy.log", 52428800, 5, true);
        MCP_LOG_SET_LEVEL(spdlog::level::info);

        try {
            InstallSignalHandlers();
            RunProxyMode(zk_hosts, proxy_port);
            MCP_LOG_INFO("Proxy shutdown complete");
        } catch (const std::exception& e) {
            MCP_LOG_ERROR("Fatal error: {}", e.what());
            std::cerr << "Fatal error: " << e.what() << std::endl;
            return 1;
        }

        MCP_LOG_SHUTDOWN();
        return 0;
    }

    // standalone / worker: load config and start MCP server.
    if (!config::MCP_CONFIG.LoadFromFile(config_file)) {
        std::cerr << "Failed to load config from: " << config_file << std::endl;
        return 1;
    }

    if (host.empty()) {
        host = "0.0.0.0";
    }
    if (port == 0) {
        port = config::MCP_CONFIG.GetServerPort();
    }

    MCP_LOG_INIT("mcp_server", config::MCP_CONFIG.GetLogFilePath(),
                 config::MCP_CONFIG.GetLogFileSize(), config::MCP_CONFIG.GetLogFileCount(),
                 config::MCP_CONFIG.GetLogConsoleOutput());
    MCP_LOG_SET_LEVEL(StringToLogLevel(config::MCP_CONFIG.GetLogLevel()));

    try {
        McpServer mcp_server("mcp-server", "1.0.0");
        ConfigureMcpServer(mcp_server);
        InstallSignalHandlers();

        if (mode == "worker") {
            if (zk_hosts.empty()) {
                MCP_LOG_ERROR("worker mode requires --zk-hosts");
                return 1;
            }
            RunWorkerMode(mcp_server, host, port, zk_hosts);
        } else {
            // standalone mode (original behavior)
            RunHttpMode(mcp_server, host, port);
        }

        MCP_LOG_INFO("Server shutdown complete");
    } catch (const std::exception& e) {
        MCP_LOG_ERROR("Fatal error: {}", e.what());
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    MCP_LOG_SHUTDOWN();
    return 0;
}
