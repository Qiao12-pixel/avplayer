#include <logger/logger.h>
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path MakeTempLogPath(const std::string& name) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto dir = std::filesystem::temp_directory_path() /
               ("mcp_logger_test_" + std::to_string(now));
    std::filesystem::create_directories(dir);
    return dir / name;
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        mcp::logger::Logger::GetInstance().Shutdown();
    }

    void TearDown() override {
        mcp::logger::Logger::GetInstance().Shutdown();
    }
};

}  // namespace

TEST_F(LoggerTest, InitCreatesLoggerAndWritesToFile) {
    const auto log_path = MakeTempLogPath("server.log");

    mcp::logger::Logger::GetInstance().Init("mcp_logger_file_test",
                                            log_path.string(),
                                            1024 * 1024,
                                            3,
                                            false);

    auto logger = mcp::logger::Logger::GetInstance().GetLogger();
    ASSERT_NE(logger, nullptr);

    logger->info("logger file smoke test");
    mcp::logger::Logger::GetInstance().Flush();

    ASSERT_TRUE(std::filesystem::exists(log_path));
    EXPECT_NE(ReadFile(log_path).find("logger file smoke test"), std::string::npos);
}

TEST_F(LoggerTest, GetLoggerAutoInitializesDefaultLogger) {
    auto logger = mcp::logger::Logger::GetInstance().GetLogger();

    ASSERT_NE(logger, nullptr);
    EXPECT_EQ(logger->name(), "mcp");
}

TEST_F(LoggerTest, SetLevelUpdatesLoggerLevel) {
    const auto log_path = MakeTempLogPath("level.log");
    mcp::logger::Logger::GetInstance().Init("mcp_logger_level_test",
                                            log_path.string(),
                                            1024 * 1024,
                                            3,
                                            false);

    mcp::logger::Logger::GetInstance().SetLevel(spdlog::level::debug);

    auto logger = mcp::logger::Logger::GetInstance().GetLogger();
    ASSERT_NE(logger, nullptr);
    EXPECT_EQ(logger->level(), spdlog::level::debug);
}

TEST_F(LoggerTest, ShutdownAllowsReinitialization) {
    const auto first_path = MakeTempLogPath("first.log");
    mcp::logger::Logger::GetInstance().Init("mcp_logger_reinit_first",
                                            first_path.string(),
                                            1024 * 1024,
                                            3,
                                            false);
    mcp::logger::Logger::GetInstance().Shutdown();

    const auto second_path = MakeTempLogPath("second.log");
    mcp::logger::Logger::GetInstance().Init("mcp_logger_reinit_second",
                                            second_path.string(),
                                            1024 * 1024,
                                            3,
                                            false);

    auto logger = mcp::logger::Logger::GetInstance().GetLogger();
    ASSERT_NE(logger, nullptr);
    EXPECT_EQ(logger->name(), "mcp_logger_reinit_second");
}

TEST_F(LoggerTest, LoggingMacrosCompileAndWriteMessages) {
    const auto log_path = MakeTempLogPath("macros.log");
    MCP_LOG_INIT("mcp_logger_macro_test", log_path.string(), 1024 * 1024, 3, false);
    MCP_LOG_SET_LEVEL(spdlog::level::debug);

    MCP_LOG_DEBUG("debug macro {}", 42);
    MCP_LOG_INFO("info macro");
    MCP_LOG_WARN("warn macro");
    MCP_LOG_ERROR_IF(true, "conditional error {}", 7);
    MCP_LOG_DEBUG_IF(false, "this should not be written");
    MCP_LOG_FLUSH();

    const auto content = ReadFile(log_path);
    EXPECT_NE(content.find("debug macro 42"), std::string::npos);
    EXPECT_NE(content.find("info macro"), std::string::npos);
    EXPECT_NE(content.find("warn macro"), std::string::npos);
    EXPECT_NE(content.find("conditional error 7"), std::string::npos);
    EXPECT_EQ(content.find("this should not be written"), std::string::npos);
}
