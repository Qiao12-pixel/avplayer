#ifndef SERVER_APP_H
#define SERVER_APP_H

#include <string>

#include <spdlog/spdlog.h>

#include "json_rpc/jsonrpc_dispatcher.h"
#include "mcp/mcp_server.h"

namespace mcp {
    spdlog::level::level_enum StringToLogLevel(const std::string& level);

    void ConfigureMcpServer(McpServer& mcp_server);
    jsonrpc::JsonRpcDispatcher CreateDispatcher(McpServer& mcp_server);
    void InstallSignalHandlers();

    void RunHttpMode(McpServer& mcp_server, const std::string& host, int port);
    void RunStdioMode(McpServer& mcp_server);
    void RunBothModes(McpServer& mcp_server, const std::string& host, int port);

    // New modes.
    void RunWorkerMode(McpServer& mcp_server, const std::string& host, int port,
                       const std::string& zk_hosts);
    void RunProxyMode(const std::string& zk_hosts, int proxy_port);
}

#endif // SERVER_APP_H
