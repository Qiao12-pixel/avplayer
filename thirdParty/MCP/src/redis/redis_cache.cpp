#include "redis/redis_cache.h"

#include <chrono>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <hiredis/hiredis.h>

#include "logger/logger.h"

namespace mcp {
namespace redis {

RedisCache::RedisCache(const std::string& host, int port, int db, int timeout_sec)
    : host_(host), port_(port), db_(db), timeout_sec_(timeout_sec) {}

RedisCache::~RedisCache() {
    if (ctx_ != nullptr) {
        redisFree(ctx_);
        ctx_ = nullptr;
    }
}

RedisCache::RedisCache(RedisCache&& other) noexcept
    : host_(std::move(other.host_)), port_(other.port_), db_(other.db_),
      timeout_sec_(other.timeout_sec_), ctx_(other.ctx_), connected_(other.connected_) {
    other.ctx_ = nullptr;
    other.connected_ = false;
}

RedisCache& RedisCache::operator=(RedisCache&& other) noexcept {
    if (this != &other) {
        if (ctx_) redisFree(ctx_);
        host_ = std::move(other.host_);
        port_ = other.port_;
        db_ = other.db_;
        timeout_sec_ = other.timeout_sec_;
        ctx_ = other.ctx_;
        connected_ = other.connected_;
        other.ctx_ = nullptr;
        other.connected_ = false;
    }
    return *this;
}

void RedisCache::Connect() {
    struct timeval tv;
    tv.tv_sec = timeout_sec_;
    tv.tv_usec = 0;

    ctx_ = redisConnectWithTimeout(host_.c_str(), port_, tv);
    if (ctx_ == nullptr) {
        throw std::runtime_error("redisConnect failed: out of memory");
    }
    if (ctx_->err != REDIS_OK) {
        std::string err = ctx_->errstr;
        redisFree(ctx_);
        ctx_ = nullptr;
        throw std::runtime_error("redisConnect failed: " + err);
    }

    // Select DB.
    if (db_ > 0) {
        std::string cmd = "SELECT " + std::to_string(db_);
        auto* reply = static_cast<redisReply*>(redisCommand(ctx_, cmd.c_str()));
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            std::string err = reply ? reply->str : "unknown";
            if (reply) freeReplyObject(reply);
            redisFree(ctx_);
            ctx_ = nullptr;
            throw std::runtime_error("redis SELECT failed: " + err);
        }
        freeReplyObject(reply);
    }

    connected_ = true;
    MCP_LOG_INFO("Redis connected: {}:{}/{}", host_, port_, db_);
}

bool RedisCache::IsConnected() const {
    return connected_;
}

std::optional<std::string> RedisCache::Get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* reply = static_cast<redisReply*>(redisCommand(ctx_, "GET %s", key.c_str()));
    if (!reply) return std::nullopt;

    std::optional<std::string> result;
    if (reply->type == REDIS_REPLY_STRING) {
        result = std::string(reply->str, reply->len);
    }
    freeReplyObject(reply);
    return result;
}

bool RedisCache::Set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* reply = static_cast<redisReply*>(redisCommand(ctx_, "SET %s %s", key.c_str(), value.c_str()));
    if (!reply) return false;
    bool ok = (reply->type != REDIS_REPLY_ERROR);
    freeReplyObject(reply);
    return ok;
}

bool RedisCache::SetEx(const std::string& key, const std::string& value, int ttl_sec) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* reply = static_cast<redisReply*>(redisCommand(ctx_, "SETEX %s %d %s",
        key.c_str(), ttl_sec, value.c_str()));
    if (!reply) return false;
    bool ok = (reply->type != REDIS_REPLY_ERROR);
    freeReplyObject(reply);
    return ok;
}

bool RedisCache::Expire(const std::string& key, int ttl_sec) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* reply = static_cast<redisReply*>(redisCommand(ctx_, "EXPIRE %s %d", key.c_str(), ttl_sec));
    if (!reply) return false;
    bool ok = (reply->type != REDIS_REPLY_ERROR);
    freeReplyObject(reply);
    return ok;
}

bool RedisCache::Del(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* reply = static_cast<redisReply*>(redisCommand(ctx_, "DEL %s", key.c_str()));
    if (!reply) return false;
    bool ok = (reply->type != REDIS_REPLY_ERROR);
    freeReplyObject(reply);
    return ok;
}

int64_t RedisCache::Incr(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* reply = static_cast<redisReply*>(redisCommand(ctx_, "INCR %s", key.c_str()));
    if (!reply || reply->type != REDIS_REPLY_INTEGER) {
        if (reply) freeReplyObject(reply);
        return -1;
    }
    int64_t val = reply->integer;
    freeReplyObject(reply);
    return val;
}

} // namespace redis
} // namespace mcp
