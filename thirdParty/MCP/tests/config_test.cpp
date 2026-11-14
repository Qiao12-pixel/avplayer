#include <config/config.h>
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path WriteTempConfig(const std::string& content) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() /
                ("mcp_config_test_" + std::to_string(now) + ".json");

    std::ofstream file(path);
    file << content;
    file.close();

    return path;
}

bool LoadConfigFromPath(const std::filesystem::path& path) {
    auto config_path = path.string();
    return mcp::config::MCP_CONFIG.LoadFromFile(config_path);
}

}  // namespace

TEST(ConfigTest, LoadsCompleteConfigAndReturnsConfiguredValues) {
    const auto path = WriteTempConfig(R"({
        "server": {
            "port": 9001
        },
        "logging": {
            "log_file_path": "/tmp/mcp-test.log",
            "log_level": "debug",
            "log_file_size": 4096,
            "log_file_count": 7,
            "log_console_output": false
        },
        "thread_pool": {
            "size": 3,
            "max_queue_size": 9,
            "pooled_methods": ["custom/run", "tools/call"]
        },
        "image_generation": {
            "default_provider": "doubao",
            "doubao": {
                "api_key": "test-doubao-key",
                "model": "test-doubao-model",
                "api_url": "https://example.com/images"
            },
            "gemini": {
                "api_key": "test-gemini-key",
                "model": "test-gemini-model"
            }
        }
    })");

    ASSERT_TRUE(LoadConfigFromPath(path));

    const auto& config = mcp::config::MCP_CONFIG;
    EXPECT_TRUE(config.IsLoad());
    EXPECT_EQ(config.GetServerPort(), 9001);
    EXPECT_EQ(config.GetLogFilePath(), "/tmp/mcp-test.log");
    EXPECT_EQ(config.GetLogLevel(), "debug");
    EXPECT_EQ(config.GetLogFileSize(), 4096);
    EXPECT_EQ(config.GetLogFileCount(), 7);
    EXPECT_FALSE(config.GetLogConsoleOutput());
    EXPECT_EQ(config.GetThreadPoolSize(), 3);
    EXPECT_EQ(config.GetThreadPoolMaxQueueSize(), 9);
    EXPECT_EQ(config.GetThreadPoolPooledMethods(), std::vector<std::string>({"custom/run", "tools/call"}));
    EXPECT_EQ(config.GetImageGenerationDefaultProvider(), "doubao");
    EXPECT_EQ(config.GetDoubaoImageApiKey(), "test-doubao-key");
    EXPECT_EQ(config.GetDoubaoImageModel(), "test-doubao-model");
    EXPECT_EQ(config.GetDoubaoImageApiUrl(), "https://example.com/images");
    EXPECT_EQ(config.GetGeminiImageApiKey(), "test-gemini-key");
    EXPECT_EQ(config.GetGeminiImageModel(), "test-gemini-model");
}

TEST(ConfigTest, LoadsServerJsonFromProjectConfigDirectory) {
    const auto path = std::filesystem::path(MCP_PROJECT_ROOT) / "tests" / "fixtures" / "server.json";

    ASSERT_TRUE(LoadConfigFromPath(path));

    const auto& config = mcp::config::MCP_CONFIG;
    EXPECT_TRUE(config.IsLoad());
    EXPECT_EQ(config.GetServerPort(), 8080);
    EXPECT_EQ(config.GetLogFilePath(), "../logs/server.log");
    EXPECT_EQ(config.GetLogLevel(), "debug");
    EXPECT_EQ(config.GetLogFileSize(), 52428800);
    EXPECT_EQ(config.GetLogFileCount(), 5);
    EXPECT_TRUE(config.GetLogConsoleOutput());
    EXPECT_EQ(config.GetThreadPoolSize(), 16);
    EXPECT_EQ(config.GetThreadPoolMaxQueueSize(), 128);
    EXPECT_EQ(config.GetThreadPoolPooledMethods(), std::vector<std::string>({
        "tools/call",
        "resources/read",
        "prompts/get"
    }));
    EXPECT_EQ(config.GetImageGenerationDefaultProvider(), "doubao");
    EXPECT_EQ(config.GetDoubaoImageApiKey(), "");
    EXPECT_EQ(config.GetDoubaoImageModel(), "doubao-seedream-4-5-251128");
    EXPECT_EQ(config.GetDoubaoImageApiUrl(), "https://ark.cn-beijing.volces.com/api/v3/images/generations");
    EXPECT_EQ(config.GetGeminiImageApiKey(), "");
    EXPECT_EQ(config.GetGeminiImageModel(), "gemini-3.1-flash-image-preview");
}

TEST(ConfigTest, UsesDefaultsWhenSectionsOrFieldsAreMissing) {
    const auto path = WriteTempConfig("{}");

    ASSERT_TRUE(LoadConfigFromPath(path));

    const auto& config = mcp::config::MCP_CONFIG;
    EXPECT_EQ(config.GetServerPort(), 8080);
    EXPECT_EQ(config.GetLogFilePath(), "../logs/server.log");
    EXPECT_EQ(config.GetLogLevel(), "info");
    EXPECT_EQ(config.GetLogFileSize(), 10 * 1024 * 1024);
    EXPECT_EQ(config.GetLogFileCount(), 5);
    EXPECT_TRUE(config.GetLogConsoleOutput());
    EXPECT_EQ(config.GetThreadPoolSize(), 4);
    EXPECT_EQ(config.GetThreadPoolMaxQueueSize(), 128);
    EXPECT_EQ(config.GetThreadPoolPooledMethods(), std::vector<std::string>({
        "tools/call",
        "resources/read",
        "prompts/get"
    }));
    EXPECT_EQ(config.GetImageGenerationDefaultProvider(), "doubao");
    EXPECT_EQ(config.GetDoubaoImageApiKey(), "");
    EXPECT_EQ(config.GetDoubaoImageModel(), "doubao-seedream-4-5-251128");
    EXPECT_EQ(config.GetDoubaoImageApiUrl(), "https://ark.cn-beijing.volces.com/api/v3/images/generations");
    EXPECT_EQ(config.GetGeminiImageApiKey(), "");
    EXPECT_EQ(config.GetGeminiImageModel(), "gemini-3.1-flash-image-preview");
}

TEST(ConfigTest, ReturnsFalseForMissingFile) {
    const auto valid_path = WriteTempConfig(R"({
        "server": {
            "port": 9002
        }
    })");
    ASSERT_TRUE(LoadConfigFromPath(valid_path));
    ASSERT_TRUE(mcp::config::MCP_CONFIG.IsLoad());

    auto missing_path = std::filesystem::temp_directory_path() / "mcp_config_test_missing.json";
    auto missing_path_string = missing_path.string();

    EXPECT_FALSE(mcp::config::MCP_CONFIG.LoadFromFile(missing_path_string));
    EXPECT_FALSE(mcp::config::MCP_CONFIG.IsLoad());
}

TEST(ConfigTest, ReturnsFalseForInvalidJson) {
    const auto path = WriteTempConfig("{ invalid json");

    EXPECT_FALSE(LoadConfigFromPath(path));
}

TEST(ConfigTest, RejectsPortBelowValidRange) {
    const auto path = WriteTempConfig(R"({
        "server": {
            "port": 0
        }
    })");

    EXPECT_FALSE(LoadConfigFromPath(path));
}

TEST(ConfigTest, RejectsPortAboveValidRange) {
    const auto path = WriteTempConfig(R"({
        "server": {
            "port": 65536
        }
    })");

    EXPECT_FALSE(LoadConfigFromPath(path));
}

TEST(ConfigTest, RejectsInvalidLogLevel) {
    const auto path = WriteTempConfig(R"({
        "logging": {
            "log_level": "verbose"
        }
    })");

    EXPECT_FALSE(LoadConfigFromPath(path));
}

TEST(ConfigTest, RejectsZeroLogFileSize) {
    const auto path = WriteTempConfig(R"({
        "logging": {
            "log_file_size": 0
        }
    })");

    EXPECT_FALSE(LoadConfigFromPath(path));
}

TEST(ConfigTest, RejectsNonPositiveLogFileCount) {
    const auto path = WriteTempConfig(R"({
        "logging": {
            "log_file_count": 0
        }
    })");

    EXPECT_FALSE(LoadConfigFromPath(path));
}

TEST(ConfigTest, RejectsZeroThreadPoolSize) {
    const auto path = WriteTempConfig(R"({
        "thread_pool": {
            "size": 0
        }
    })");

    EXPECT_FALSE(LoadConfigFromPath(path));
}

TEST(ConfigTest, RejectsZeroThreadPoolMaxQueueSize) {
    const auto path = WriteTempConfig(R"({
        "thread_pool": {
            "max_queue_size": 0
        }
    })");

    EXPECT_FALSE(LoadConfigFromPath(path));
}

TEST(ConfigTest, RejectsNonArrayThreadPoolPooledMethods) {
    const auto path = WriteTempConfig(R"({
        "thread_pool": {
            "pooled_methods": "tools/call"
        }
    })");

    EXPECT_FALSE(LoadConfigFromPath(path));
}

TEST(ConfigTest, RejectsNonStringThreadPoolPooledMethods) {
    const auto path = WriteTempConfig(R"({
        "thread_pool": {
            "pooled_methods": ["tools/call", 42]
        }
    })");

    EXPECT_FALSE(LoadConfigFromPath(path));
}

TEST(ConfigTest, RejectsDuplicateThreadPoolPooledMethods) {
    const auto path = WriteTempConfig(R"({
        "thread_pool": {
            "pooled_methods": ["tools/call", "tools/call"]
        }
    })");

    EXPECT_FALSE(LoadConfigFromPath(path));
}

TEST(ConfigTest, RejectsInvalidImageGenerationDefaultProvider) {
    const auto path = WriteTempConfig(R"({
        "image_generation": {
            "default_provider": "unknown"
        }
    })");

    EXPECT_FALSE(LoadConfigFromPath(path));
}

TEST(ConfigTest, RejectsEmptyDoubaoImageModel) {
    const auto path = WriteTempConfig(R"({
        "image_generation": {
            "doubao": {
                "model": ""
            }
        }
    })");

    EXPECT_FALSE(LoadConfigFromPath(path));
}

TEST(ConfigTest, RejectsEmptyDoubaoImageApiUrl) {
    const auto path = WriteTempConfig(R"({
        "image_generation": {
            "doubao": {
                "api_url": ""
            }
        }
    })");

    EXPECT_FALSE(LoadConfigFromPath(path));
}
