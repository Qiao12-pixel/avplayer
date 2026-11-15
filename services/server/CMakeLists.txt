cmake_minimum_required(VERSION 3.23)
project(AVPlayerServer LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
if(PROJECT_IS_TOP_LEVEL)
  get_filename_component(AVPLAYER_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
  # Media module performs dependency discovery and adds proto/media-core.
  add_subdirectory(../media ${CMAKE_BINARY_DIR}/media)
endif()
find_package(Boost REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_path(MYSQL_INCLUDE_DIR mysql/mysql.h REQUIRED)
find_library(MYSQL_LIBRARY NAMES mysqlclient mariadb PATHS /opt/homebrew/opt/mysql/lib REQUIRED)
find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h REQUIRED)
find_library(HIREDIS_LIBRARY hiredis REQUIRED)
# ZooKeeper is private to the unified server; media-core does not depend on it.
find_path(ZOOKEEPER_INCLUDE_DIR zookeeper/zookeeper.h REQUIRED)
find_library(ZOOKEEPER_LIBRARY zookeeper_mt REQUIRED)
add_library(server_discovery STATIC src/discovery/zookeeper/Registry.cpp)
target_compile_features(server_discovery PUBLIC cxx_std_17)
target_compile_definitions(server_discovery PRIVATE THREADED)
target_include_directories(server_discovery PUBLIC src PRIVATE ${ZOOKEEPER_INCLUDE_DIR})
target_link_libraries(server_discovery PRIVATE ${ZOOKEEPER_LIBRARY} Threads::Threads)

# One process, explicit module targets. Only Redis/MySQL targets see driver headers.
add_library(server_mysql STATIC src/storage/mysql/MySqlRepository.cpp)
target_include_directories(server_mysql PUBLIC src PRIVATE ${MYSQL_INCLUDE_DIR})
target_link_libraries(server_mysql PUBLIC nlohmann_json::nlohmann_json Threads::Threads PRIVATE ${MYSQL_LIBRARY})

add_library(server_redis STATIC src/storage/redis/RedisCache.cpp)
target_include_directories(server_redis PUBLIC src PRIVATE ${HIREDIS_INCLUDE_DIR})
target_link_libraries(server_redis PUBLIC nlohmann_json::nlohmann_json Threads::Threads PRIVATE ${HIREDIS_LIBRARY})

add_library(server_load_balancing STATIC src/load_balancing/LeastSessionsPolicy.cpp)
target_include_directories(server_load_balancing PUBLIC src)

add_library(server_rpc STATIC src/rpc/StorageService.cpp src/rpc/MediaRouter.cpp src/rpc/UploadService.cpp)
target_include_directories(server_rpc PUBLIC src)
target_link_libraries(server_rpc PUBLIC server_mysql server_redis server_load_balancing
  avplayer_media_service avplayer_proto server_discovery)

add_library(server_network STATIC
  src/integrations/mcp/WeatherService.cpp
  src/network/common/Url.cpp
  src/network/http/HttpSession.cpp
  src/network/http/HttpServer.cpp
  src/network/websocket/WebSocketSession.cpp)
target_include_directories(server_network PUBLIC src ${Boost_INCLUDE_DIRS})
target_link_libraries(server_network PUBLIC server_rpc Threads::Threads PRIVATE CURL::libcurl)

add_executable(avplayer_server src/main.cpp src/app/ServerApplication.cpp)
target_include_directories(avplayer_server PRIVATE src)
target_link_libraries(avplayer_server PRIVATE server_network server_rpc)

include(CTest)
if(BUILD_TESTING)
  add_executable(server_policy_test tests/LeastSessionsPolicyTest.cpp)
  target_link_libraries(server_policy_test PRIVATE server_load_balancing)
  add_test(NAME server_least_sessions_policy COMMAND server_policy_test)
endif()
