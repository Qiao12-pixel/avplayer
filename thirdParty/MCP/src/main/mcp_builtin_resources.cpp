#include "mcp_builtin_resources.h"

#include <ctime>
#include <sstream>

#include "config/config.h"

namespace mcp {
    void RegisterBuiltinResources(McpServer& mcp) {
        {
            Resource res;
            res.url = "system://info";
            res.name = "System Infomation";
            res.description = "Basic system information";
            res.mime_type = "text/plain";

            mcp.RegisterResource(res, [](const std::string& url) -> ResourceContent {
                ResourceContent content;
                content.url = url;
                content.mime_type = "text/plain";

                std::ostringstream oss;
                oss << "MCP-Server - System Info\n";
                oss << "========================\n";
                std::time_t now = time(nullptr);
                char buffer[32];
                std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
                oss << "Time: " << buffer << "\n";
                content.text = oss.str();
                return content;
            });
        }

        {
            Resource res;
            res.url = "config://server";
            res.name = "Server Configuration";
            res.description = "Server configuration";
            res.mime_type = "application/json";

            mcp.RegisterResource(res, [](const std::string& url) -> ResourceContent {
                ResourceContent content;
                content.url = url;
                content.mime_type = "application/json";
                content.text = json{
                    {"port", config::MCP_CONFIG.GetServerPort()},
                    {"log_level", config::MCP_CONFIG.GetLogLevel()},
                }.dump(2);
                return content;
            });
        }
    }
}
