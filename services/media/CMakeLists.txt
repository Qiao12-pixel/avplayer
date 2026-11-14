cmake_minimum_required(VERSION 3.23)
project(AVPlayerMediaServer VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# This directory is an independent deployment unit: it only needs
# libs/media-core + proto, never the Qt desktop application.
if(PROJECT_IS_TOP_LEVEL)
    get_filename_component(AVPLAYER_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

    list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/grpc" "/opt/homebrew/opt/protobuf")
    find_package(Protobuf CONFIG REQUIRED)
    find_package(gRPC CONFIG REQUIRED)
    find_package(Qt6 REQUIRED COMPONENTS Core Multimedia)

    find_package(Opus CONFIG QUIET)
    if(NOT TARGET Opus::opus)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)
        add_library(Opus::opus INTERFACE IMPORTED)
        target_link_libraries(Opus::opus INTERFACE PkgConfig::OPUS)
    endif()

    # Conan provides ffmpeg::ffmpeg on macOS; Debian/Ubuntu images provide
    # FFmpeg through pkg-config instead.
    find_package(ffmpeg CONFIG QUIET)
    if(NOT TARGET ffmpeg::ffmpeg)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
            libavcodec libavformat libavutil libswscale libswresample)
        add_library(ffmpeg::ffmpeg INTERFACE IMPORTED)
        target_link_libraries(ffmpeg::ffmpeg INTERFACE PkgConfig::FFMPEG)
    endif()

    include(FetchContent)
    set(NO_TESTS ON CACHE BOOL "" FORCE)
    set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
    set(USE_GNUTLS OFF CACHE BOOL "" FORCE)
    set(USE_NICE OFF CACHE BOOL "" FORCE)
    if(APPLE)
        set(OPENSSL_ROOT_DIR "/opt/homebrew/opt/openssl@4" CACHE PATH "" FORCE)
        set(OPENSSL_INCLUDE_DIR "/opt/homebrew/opt/openssl@4/include" CACHE PATH "" FORCE)
        include_directories("/opt/homebrew/opt/openssl@4/include")
    endif()
    FetchContent_Declare(libdatachannel
        GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
        GIT_TAG v0.23.1
        GIT_SHALLOW TRUE
        GIT_SUBMODULES_RECURSE TRUE)
    FetchContent_MakeAvailable(libdatachannel)

    add_subdirectory("${AVPLAYER_SOURCE_ROOT}/proto" "${CMAKE_BINARY_DIR}/proto")
    add_subdirectory("${AVPLAYER_SOURCE_ROOT}/libs/media-core" "${CMAKE_BINARY_DIR}/media-core")
else()
    # Configured from the repository root, which already provides
    # avplayer_proto, avplayer_media_core, ffmpeg, Qt6 and datachannel.
    if(NOT DEFINED AVPLAYER_SOURCE_ROOT)
        set(AVPLAYER_SOURCE_ROOT "${CMAKE_SOURCE_DIR}")
    endif()
endif()

add_executable(media_server
    src/main_media_server.cpp
    src/MediaServer.cpp
    src/MediaServer.h
    src/MediaSession.cpp
    src/MediaSession.h
    src/WebRtcFrameSink.cpp
    src/WebRtcFrameSink.h
    src/WebRtcAudioSink.cpp
    src/WebRtcAudioSink.h
)

target_include_directories(media_server PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(media_server PRIVATE
    AVPLAYER_PROJECT_ROOT="${AVPLAYER_SOURCE_ROOT}"
)
target_link_libraries(media_server PRIVATE
    avplayer_media_core
    avplayer_proto
    datachannel
    Opus::opus
    Qt6::Core
    Qt6::Multimedia
    ffmpeg::ffmpeg
)

option(AVPLAYER_BUILD_STREAMING_SMOKE_CLIENT
    "Build the standalone WebRTC receive smoke client" OFF)
if(AVPLAYER_BUILD_STREAMING_SMOKE_CLIENT)
    add_executable(streaming_smoke_client src/streaming_smoke_client.cpp)
    target_link_libraries(streaming_smoke_client PRIVATE
        avplayer_proto
        datachannel
    )
endif()

if(NOT TARGET avplayer_discovery)
  add_subdirectory(../../libs/service-discovery ${CMAKE_BINARY_DIR}/service-discovery)
endif()
target_link_libraries(media_server PRIVATE avplayer_discovery)
