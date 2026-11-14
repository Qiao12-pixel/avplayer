/**
 * @file logger.cpp
 * @brief 
 * @author Joe
 * @date 26-4-28
 */

#include "logger.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <filesystem>
#include <iostream>

namespace mcp {
    namespace logger {
        Logger &Logger::GetInstance() {
            static Logger instance;
            return instance;
        }

        void Logger::Init(const std::string &logger_name, const std::string &logger_path,
                          size_t max_file_size, size_t max_file_nums, bool console_output) {
            if (m_initialized_) {
                spdlog::warn("Logger already initialized");
                return;
            }
            try {
                std::vector<spdlog::sink_ptr> sinks;
                //控制台输出日志
                if (console_output) {
                    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                    console_sink->set_level(spdlog::level::trace);
                    console_sink->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%n] %v");
                    sinks.push_back(console_sink);
                }
                //日志文件输出日志
                if (!logger_path.empty()) {
                    //create logger path if not exist
                    std::filesystem::path log_path(logger_path);
                    std::filesystem::path log_dir = log_path.parent_path();

                    if (!log_dir.empty() && !std::filesystem::exists(log_dir)) {
                        std::filesystem::create_directories(log_dir);
                    }
                    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logger_path, max_file_size, max_file_nums);
                    file_sink->set_level(spdlog::level::trace);
                    file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] [%t] %v");
                    sinks.push_back(file_sink);
                }
                m_logger_ = std::make_shared<spdlog::logger>(logger_name, sinks.begin(), sinks.end());
                m_logger_->set_level(spdlog::level::info);
                m_logger_->flush_on(spdlog::level::info);

                spdlog::register_logger(m_logger_);
                spdlog::set_default_logger(m_logger_);

                m_initialized_ = true;
                m_logger_->info("Logger initialized successfully - name: {}, file: {}, console:{}", logger_name, logger_path.empty() ? "disabled" : logger_path, console_output ? "enabled" : "disabled");

            } catch (const spdlog::spdlog_ex& ex) {
                std::cerr << "Logger initialization failed: " << ex.what() << std::endl;
                throw;
            }
        }
        void Logger::SetLevel(spdlog::level::level_enum level) {
            if (m_logger_) {
                m_logger_->set_level(level);
                m_logger_->info("Logger set to level {}", spdlog::level::to_string_view(level));
            }
        }
        std::shared_ptr<spdlog::logger> Logger::GetLogger() {
            if (!m_initialized_) {
                Init();
            }
            return m_logger_;
        }

        void Logger::Flush() {
            if (m_logger_) {
                m_logger_->flush();
            }
        }
        void Logger::Shutdown() {
            if (!m_initialized_) {
                return;
            }
            try {
                if (m_logger_) {
                    m_logger_->flush();
                }
            } catch (...) {
                //避免在析构阶段抛异常
            }
            spdlog::shutdown();
            m_logger_.reset();
            m_initialized_ = false;
        }
        Logger::~Logger() {
            Shutdown();
        }




    }
}
