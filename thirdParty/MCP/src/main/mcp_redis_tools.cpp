#include "mcp_redis_tools.h"

#include <memory>
#include <string>

#include "config/config.h"
#include "logger/logger.h"
#include "redis/redis_cache.h"

namespace mcp {

namespace {

ToolResult MakeTextResult(const std::string& text, bool is_error = false) {
    ToolResult result;
    result.is_error = is_error;
    result.content.emplace_back(ContentItem{.type = "text", .text = text});
    return result;
}

} // anonymous namespace

void RegisterRedisTools(McpServer& mcp) {
    auto host = config::MCP_CONFIG.GetRedisHost();
    int port = config::MCP_CONFIG.GetRedisPort();
    int db = config::MCP_CONFIG.GetRedisDb();

    if (host.empty()) {
        MCP_LOG_WARN("Redis host not configured; cache tools will fail at runtime");
    }

    auto cache = std::make_shared<redis::RedisCache>(host, port, db);
    try {
        cache->Connect();
        MCP_LOG_INFO("Redis connected: {}:{}/{}", host, port, db);
    } catch (const std::exception& e) {
        MCP_LOG_ERROR("Failed to connect to Redis: {}", e.what());
    }

    int tool_count = 0;

    // Tool: cache_get
    {
        Tool tool;
        tool.name = "cache_get";
        tool.description = "Get a value from Redis cache by key";
        tool.input_schema.properties = {
            {"key", {{"type", "string"}, {"description", "Cache key"}}}
        };
        tool.input_schema.required = {"key"};

        mcp.RegisterTool(tool, [cache](const json& args) -> ToolResult {
            try {
                auto val = cache->Get(args["key"].get<std::string>());
                if (val) {
                    json result = {{"found", true}, {"value", val.value()}};
                    return MakeTextResult(result.dump());
                }
                json result = {{"found", false}};
                return MakeTextResult(result.dump());
            } catch (const std::exception& e) {
                return MakeTextResult(std::string("cache_get failed: ") + e.what(), true);
            }
        });
        ++tool_count;
    }

    // Tool: cache_set
    {
        Tool tool;
        tool.name = "cache_set";
        tool.description = "Set a value in Redis cache";
        tool.input_schema.properties = {
            {"key", {{"type", "string"}, {"description", "Cache key"}}},
            {"value", {{"type", "string"}, {"description", "Value to store"}}}
        };
        tool.input_schema.required = {"key", "value"};

        mcp.RegisterTool(tool, [cache](const json& args) -> ToolResult {
            try {
                bool ok = cache->Set(args["key"].get<std::string>(),
                                     args["value"].get<std::string>());
                json result = {{"ok", ok}};
                return MakeTextResult(result.dump());
            } catch (const std::exception& e) {
                return MakeTextResult(std::string("cache_set failed: ") + e.what(), true);
            }
        });
        ++tool_count;
    }

    // Tool: cache_setex
    {
        Tool tool;
        tool.name = "cache_setex";
        tool.description = "Set a value in Redis cache with TTL (seconds)";
        tool.input_schema.properties = {
            {"key", {{"type", "string"}, {"description", "Cache key"}}},
            {"value", {{"type", "string"}, {"description", "Value to store"}}},
            {"ttl", {{"type", "integer"}, {"description", "TTL in seconds"}}}
        };
        tool.input_schema.required = {"key", "value", "ttl"};

        mcp.RegisterTool(tool, [cache](const json& args) -> ToolResult {
            try {
                bool ok = cache->SetEx(args["key"].get<std::string>(),
                                       args["value"].get<std::string>(),
                                       args["ttl"].get<int>());
                json result = {{"ok", ok}};
                return MakeTextResult(result.dump());
            } catch (const std::exception& e) {
                return MakeTextResult(std::string("cache_setex failed: ") + e.what(), true);
            }
        });
        ++tool_count;
    }

    // Tool: cache_del
    {
        Tool tool;
        tool.name = "cache_del";
        tool.description = "Delete a key from Redis cache";
        tool.input_schema.properties = {
            {"key", {{"type", "string"}, {"description", "Cache key"}}}
        };
        tool.input_schema.required = {"key"};

        mcp.RegisterTool(tool, [cache](const json& args) -> ToolResult {
            try {
                bool ok = cache->Del(args["key"].get<std::string>());
                json result = {{"ok", ok}};
                return MakeTextResult(result.dump());
            } catch (const std::exception& e) {
                return MakeTextResult(std::string("cache_del failed: ") + e.what(), true);
            }
        });
        ++tool_count;
    }

    // Tool: cache_incr
    {
        Tool tool;
        tool.name = "cache_incr";
        tool.description = "Atomically increment a counter in Redis";
        tool.input_schema.properties = {
            {"key", {{"type", "string"}, {"description", "Counter key"}}}
        };
        tool.input_schema.required = {"key"};

        mcp.RegisterTool(tool, [cache](const json& args) -> ToolResult {
            try {
                int64_t val = cache->Incr(args["key"].get<std::string>());
                json result = {{"value", val}};
                return MakeTextResult(result.dump());
            } catch (const std::exception& e) {
                return MakeTextResult(std::string("cache_incr failed: ") + e.what(), true);
            }
        });
        ++tool_count;
    }

    MCP_LOG_INFO("Registered {} redis cache tools", tool_count);
}

} // namespace mcp
