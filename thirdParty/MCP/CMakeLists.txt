cmake_minimum_required(VERSION 3.24)
project(MCP_Server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# 加载 Conan
# Toolchain is selected by the caller, never read from the original checkout.
if(EXISTS "${CMAKE_BINARY_DIR}/build/Debug/generators/conan_toolchain.cmake")
  include("${CMAKE_BINARY_DIR}/build/Debug/generators/conan_toolchain.cmake")
endif()

# Command Line Tools needs an absolute SDK path; Conan may emit "macosx".
if(APPLE AND CMAKE_OSX_SYSROOT STREQUAL "macosx")
  execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE MCP_MACOS_SDK OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  set(CMAKE_OSX_SYSROOT "${MCP_MACOS_SDK}" CACHE STRING "macOS SDK" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

find_package(Threads REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(CURL REQUIRED)
find_package(GTest REQUIRED)
find_package(httplib REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(ZooKeeper REQUIRED)
find_package(Hiredis REQUIRED)
enable_testing()

add_library(mcp_config
        src/config/config.cpp
        src/config/config.h
)

target_include_directories(mcp_config PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(mcp_config PUBLIC
        nlohmann_json::nlohmann_json
)

add_library(mcp_logger
        src/logger/logger.cpp
        src/logger/logger.h
)

target_include_directories(mcp_logger PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(mcp_logger PUBLIC
        spdlog::spdlog
)

add_library(mcp_sql
        src/sql/sqlite_database.cpp
        src/sql/sqlite_database.h
        src/sql/tool_call_history_repository.cpp
        src/sql/tool_call_history_repository.h
)

target_include_directories(mcp_sql PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(mcp_sql PUBLIC
        SQLite::SQLite3
)

add_library(mcp_jsonrpc
        src/json_rpc/jsonrpc_types.h
        src/json_rpc/jsonrpc_dispatcher.cpp
        src/json_rpc/jsonrpc_dispatcher.h
        src/json_rpc/stdio_jsonrpc_server.cpp
        src/json_rpc/stdio_jsonrpc_server.h
        src/json_rpc/jsonrpc_serialization.cpp
        src/json_rpc/jsonrpc_serialization.h
        mcp/mcp_types.cpp
        mcp/mcp_types.h
        mcp/mcp_server.cpp
        mcp/mcp_server.h
        src/json_rpc/http_jsonrpc.cpp
        src/json_rpc/http_jsonrpc.h
        src/utils/thread_pool.cpp
        src/utils/thread_pool.h
)

target_include_directories(mcp_jsonrpc PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(mcp_jsonrpc PUBLIC
        nlohmann_json::nlohmann_json
        httplib::httplib
        spdlog::spdlog
        mcp_config
        mcp_logger
        mcp_sql
)

add_library(mcp_builtin_tools_lib
        src/main/mcp_builtin_tools.cpp
        src/main/mcp_builtin_tools.h
)

target_include_directories(mcp_builtin_tools_lib PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(mcp_builtin_tools_lib PUBLIC
        mcp_jsonrpc
        mcp_config
        CURL::libcurl
)

add_library(mcp_job_agent_tools_lib
        src/main/mcp_job_agent_tools.cpp
        src/main/mcp_job_agent_tools.h
        src/job_agent/job_agent_database.cpp
        src/job_agent/job_agent_database.h
)

target_include_directories(mcp_job_agent_tools_lib PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(mcp_job_agent_tools_lib PUBLIC
        mcp_jsonrpc
        mcp_config
        mcp_sql
)

# ---------------------------------------------------------------------------
# ZooKeeper service registry
# ---------------------------------------------------------------------------
add_library(mcp_zk
        src/zk/zk_service_registry.cpp
        src/zk/zk_service_registry.h
)

target_include_directories(mcp_zk PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_compile_options(mcp_zk PRIVATE -Wno-deprecated-declarations)
target_link_libraries(mcp_zk PUBLIC
        ZooKeeper::zookeeper_mt
        mcp_logger
)

# ---------------------------------------------------------------------------
# Load Balancer
# ---------------------------------------------------------------------------
add_library(mcp_lb
        src/load_balancer/load_balancer.cpp
        src/load_balancer/load_balancer.h
)

target_include_directories(mcp_lb PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(mcp_lb PUBLIC
        mcp_jsonrpc
        mcp_zk
        httplib::httplib
)

# ---------------------------------------------------------------------------
# Redis cache
# ---------------------------------------------------------------------------
add_library(mcp_redis
        src/redis/redis_cache.cpp
        src/redis/redis_cache.h
)

target_include_directories(mcp_redis PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(mcp_redis PUBLIC
        Hiredis::hiredis
        mcp_logger
)

add_library(mcp_redis_tools_lib
        src/main/mcp_redis_tools.cpp
        src/main/mcp_redis_tools.h
)

target_include_directories(mcp_redis_tools_lib PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(mcp_redis_tools_lib PUBLIC
        mcp_jsonrpc
        mcp_redis
        mcp_config
)

# ---------------------------------------------------------------------------
# MCP_Server executable
# ---------------------------------------------------------------------------
add_executable(MCP_Server
        src/main/mcp_server_main.cpp
        src/main/server_app.cpp
        src/main/server_app.h
        src/main/mcp_builtin_resources.cpp
        src/main/mcp_builtin_resources.h
        src/main/mcp_builtin_prompts.cpp
        src/main/mcp_builtin_prompts.h
)

target_link_libraries(MCP_Server PRIVATE
        mcp_jsonrpc
        mcp_builtin_tools_lib
        mcp_job_agent_tools_lib
        mcp_redis_tools_lib
        mcp_config
        mcp_sql
        mcp_zk
        mcp_lb
        CURL::libcurl
        httplib::httplib
        SQLite::SQLite3
        spdlog::spdlog
)

add_executable(config_tests
        tests/config_test.cpp
)

target_compile_definitions(config_tests PRIVATE
        MCP_PROJECT_ROOT="${PROJECT_SOURCE_DIR}"
)

target_link_libraries(config_tests PRIVATE
        mcp_config
        GTest::gtest_main
)

add_executable(logger_tests
        tests/logger_test.cpp
)

target_link_libraries(logger_tests PRIVATE
        mcp_logger
        GTest::gtest_main
)

add_executable(jsonrpc_tests
        tests/jsonrpc_test.cpp
)

target_link_libraries(jsonrpc_tests PRIVATE
        mcp_jsonrpc
        GTest::gtest_main
)

add_executable(thread_pool_tests
        tests/thread_pool_test.cpp
)

target_link_libraries(thread_pool_tests PRIVATE
        mcp_jsonrpc
        GTest::gtest_main
)

add_executable(mcp_types_tests
        tests/mcp_types_test.cpp
)

target_link_libraries(mcp_types_tests PRIVATE
        mcp_jsonrpc
        GTest::gtest_main
)

add_executable(mcp_server_tests
        tests/mcp_server_test.cpp
)

target_link_libraries(mcp_server_tests PRIVATE
        mcp_jsonrpc
        GTest::gtest_main
)

add_executable(http_jsonrpc_tests
        tests/http_jsonrpc_test.cpp
)

target_link_libraries(http_jsonrpc_tests PRIVATE
        mcp_jsonrpc
        GTest::gtest_main
)

add_executable(builtin_tools_tests
        tests/builtin_tools_test.cpp
)

target_link_libraries(builtin_tools_tests PRIVATE
        mcp_builtin_tools_lib
        mcp_logger
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(config_tests)
gtest_discover_tests(logger_tests)
gtest_discover_tests(jsonrpc_tests)
gtest_discover_tests(thread_pool_tests)
gtest_discover_tests(mcp_types_tests)
gtest_discover_tests(mcp_server_tests)
gtest_discover_tests(http_jsonrpc_tests)
gtest_discover_tests(builtin_tools_tests)

# Independent native media tools: no AVPlayer source tree or gRPC dependency.
option(MCP_ENABLE_AVPLAYER "Register native media tools" ON)
if(MCP_ENABLE_AVPLAYER)
    add_library(mcp_avplayer_tools_lib src/main/mcp_avplayer_tools.cpp src/media/media_backend.cpp)
    target_link_libraries(mcp_avplayer_tools_lib PUBLIC mcp_jsonrpc Threads::Threads)
    add_executable(mcp_media_executor src/media/executor.cpp)
    target_include_directories(mcp_media_executor PRIVATE src/media)
    target_link_libraries(mcp_media_executor PRIVATE nlohmann_json::nlohmann_json Threads::Threads)
    add_dependencies(MCP_Server mcp_media_executor)
    target_link_libraries(MCP_Server PRIVATE mcp_avplayer_tools_lib)
    target_compile_definitions(MCP_Server PRIVATE MCP_WITH_AVPLAYER=1)
endif()
