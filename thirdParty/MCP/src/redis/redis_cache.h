#ifndef REDIS_CACHE_H
#define REDIS_CACHE_H

#include <memory>
#include <mutex>
#include <optional>
#include <string>

struct redisContext;

namespace mcp {
namespace redis {

class RedisCache {
public:
    RedisCache(const std::string& host, int port, int db = 0, int timeout_sec = 2);
    ~RedisCache();

    RedisCache(const RedisCache&) = delete;
    RedisCache& operator=(const RedisCache&) = delete;

    RedisCache(RedisCache&& other) noexcept;
    RedisCache& operator=(RedisCache&& other) noexcept;

    // Connect to Redis. Throws on failure.
    void Connect();

    // Basic operations.
    std::optional<std::string> Get(const std::string& key);
    bool Set(const std::string& key, const std::string& value);
    bool SetEx(const std::string& key, const std::string& value, int ttl_sec);
    bool Expire(const std::string& key, int ttl_sec);
    bool Del(const std::string& key);
    int64_t Incr(const std::string& key);

    // Check if connected.
    bool IsConnected() const;

private:
    std::string ExecuteCommand(const std::string& cmd);

    std::string host_;
    int port_;
    int db_;
    int timeout_sec_;
    redisContext* ctx_ = nullptr;
    bool connected_ = false;
    std::mutex mutex_;
};

} // namespace redis
} // namespace mcp

#endif // REDIS_CACHE_H
