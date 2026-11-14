/**
 * @file logger.h
 * @brief 封装spdlog日志
 * @author Joe
 * @date 26-4-28
 */
#ifndef LOGGER_H
#define LOGGER_H

#include <chrono>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/fmt/ostr.h>
#include <memory>
#include <string>

namespace mcp {
    namespace logger {
        class Logger {
        public:
            static Logger& GetInstance();
            void Init(const std::string& logger_name = "mcp",
                      const std::string& logger_path = "mcp.log",
                      size_t max_file_size = 10 * 1024 * 1024,
                      size_t max_file_nums = 10,
                      bool console_output = true);
            void SetLevel(spdlog::level::level_enum level);
            //获取默认logger
            std::shared_ptr<spdlog::logger> GetLogger();
            //刷新所有日志缓冲区
            void Flush();
            //关闭日志系统
            void Shutdown();

        private:
            Logger() = default;
            ~Logger();

            Logger(const Logger&) = delete;
            Logger& operator=(const Logger&) = delete;

            std::shared_ptr<spdlog::logger> m_logger_;
            bool m_initialized_ = false;
        };
    }
}
// ================================
// 便利宏定义
// ================================
#define MCP_LOG_INIT(...) mcp::logger::Logger::GetInstance().Init(__VA_ARGS__)
#define MCP_LOG_SET_LEVEL(level) mcp::logger::Logger::GetInstance().SetLevel(level)
#define MCP_LOG_FLUSH() mcp::logger::Logger::GetInstance().Flush()
#define MCP_LOG_SHUTDOWN() mcp::logger::Logger::GetInstance().Shutdown()

/**
 * TRACE 级别日志宏
 * 使用示例: MCP_LOG_TRACE("这是一条trace日志: {}", value);
 */
#define MCP_LOG_TRACE(fmt, ...) \
    do { \
        auto m_logger = mcp::logger::Logger::GetInstance().GetLogger(); \
        if (m_logger) m_logger->trace("[{}:{}] " fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while(0)

/**
 * DEBUG 级别日志宏
 * 使用示例: MCP_LOG_DEBUG("调试信息: {}", debug_value);
 */
#define MCP_LOG_DEBUG(fmt, ...) \
    do { \
        auto m_logger = mcp::logger::Logger::GetInstance().GetLogger(); \
        if (m_logger) m_logger->debug("[{}:{}] " fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while(0)

/**
 * INFO 级别日志宏
 * 使用示例: MCP_LOG_INFO("程序启动成功");
 */
#define MCP_LOG_INFO(fmt, ...) \
    do { \
        auto m_logger = mcp::logger::Logger::GetInstance().GetLogger(); \
        if (m_logger) m_logger->info("[{}:{}] " fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while(0)

/**
 * WARN 级别日志宏
 * 使用示例: MCP_LOG_WARN("警告: 配置文件不存在，使用默认配置");
 */
#define MCP_LOG_WARN(fmt, ...) \
    do { \
        auto m_logger = mcp::logger::Logger::GetInstance().GetLogger(); \
        if (m_logger) m_logger->warn("[{}:{}] " fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while(0)

/**
 * ERROR 级别日志宏
 * 使用示例: MCP_LOG_ERROR("错误: 无法连接到服务器 {}", server_addr);
 */
#define MCP_LOG_ERROR(fmt, ...) \
    do { \
        auto m_logger = mcp::logger::Logger::GetInstance().GetLogger(); \
        if (m_logger) m_logger->error("[{}:{}] " fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while(0)

/**
 * CRITICAL 级别日志宏
 * 使用示例: MCP_LOG_CRITICAL("严重错误: 系统即将崩溃");
 */
#define MCP_LOG_CRITICAL(fmt, ...) \
    do { \
        auto m_logger = mcp::logger::Logger::GetInstance().GetLogger(); \
        if (m_logger) m_logger->critical("[{}:{}] " fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while(0)

// ================================
// 带位置信息的日志宏 (用于调试)
// ================================

/**
 * 带文件名和行号的DEBUG日志宏
 * 使用示例: MCP_LOG_DEBUG_LOC("在这里出现了问题: {}", error_msg);
 */
#define MCP_LOG_DEBUG_LOC(fmt, ...) \
    do { \
        auto m_logger = mcp::logger::Logger::GetInstance().GetLogger(); \
        if (m_logger) m_logger->debug("[{}:{}] " fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while(0)

/**
 * 带文件名和行号的ERROR日志宏
 * 使用示例: MCP_LOG_ERROR_LOC("错误发生在这里: {}", error_details);
 */
#define MCP_LOG_ERROR_LOC(fmt, ...) \
    do { \
        auto m_logger = mcp::logger::Logger::GetInstance().GetLogger(); \
        if (m_logger) m_logger->error("[{}:{}] " fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while(0)

// ================================
// 性能测试宏 (可选)
// ================================

/**
 * 函数执行时间测量宏
 * 使用示例: 在函数开始处添加 MCP_LOG_FUNC_TIMER();
 */
#define MCP_LOG_FUNC_TIMER() \
    struct FuncTimer { \
        std::chrono::steady_clock::time_point start; \
        const char* func_name; \
        FuncTimer(const char* name) : start(std::chrono::steady_clock::now()), func_name(name) {} \
        ~FuncTimer() { \
            auto end = std::chrono::steady_clock::now(); \
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start); \
            MCP_LOG_DEBUG("Function {} execution time: {}us", func_name, duration.count()); \
        } \
    } _func_timer(__FUNCTION__)

// ================================
// 条件日志宏
// ================================

/**
 * 条件DEBUG日志宏
 * 使用示例: MCP_LOG_DEBUG_IF(condition, "只有满足条件才记录: {}", value);
 */
#define MCP_LOG_DEBUG_IF(condition, ...) \
    do { \
        if (condition) { \
            auto m_logger = mcp::logger::Logger::GetInstance().GetLogger(); \
            if (m_logger) m_logger->debug(__VA_ARGS__); \
        } \
    } while(0)

/**
 * 条件ERROR日志宏
 * 使用示例: MCP_LOG_ERROR_IF(error_code != 0, "操作失败，错误码: {}", error_code);
 */
#define MCP_LOG_ERROR_IF(condition, ...) \
    do { \
        if (condition) { \
            auto m_logger = mcp::logger::Logger::GetInstance().GetLogger(); \
            if (m_logger) m_logger->error(__VA_ARGS__); \
        } \
    } while(0)
#endif //LOGGER_H
