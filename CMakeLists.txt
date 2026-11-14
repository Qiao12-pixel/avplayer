cmake_minimum_required(VERSION 3.23)
project(AVPlayer VERSION 0.2.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Toolchain / global settings
# ---------------------------------------------------------------------------
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(CMAKE_BUILD_TYPE STREQUAL Debug)
    add_definitions(-DDEBUG)
endif()

find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif()

set(AVPLAYER_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(AVPLAYER_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(AVPLAYER_RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resource")
set(AVPLAYER_CACHE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cache")

# ---------------------------------------------------------------------------
# gRPC / protobuf: desktop <-> services/agent and services/media contracts
# ---------------------------------------------------------------------------
list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/grpc" "/opt/homebrew/opt/protobuf")
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# libdatachannel: WebRTC transport shared by desktop playback and media service
# ---------------------------------------------------------------------------
include(FetchContent)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(USE_GNUTLS OFF CACHE BOOL "" FORCE)
set(USE_NICE OFF CACHE BOOL "" FORCE)
# libdatachannel's vendored libsrtp links the modern OpenSSL::Crypto target
# but also directly references the legacy Module-mode ${OPENSSL_INCLUDE_DIR}
# variable for its own target_include_directories() call. Conan's OpenSSL
# recipe provides a Config-mode shim that defines the target but never sets
# that legacy variable, so libsrtp silently gets no -I flag and fails to find
# <openssl/evp.h>. Point it at Homebrew's OpenSSL explicitly so the variable
# is populated regardless of which find_package() resolution mode wins.
if(APPLE)
    set(OPENSSL_ROOT_DIR "/opt/homebrew/opt/openssl@4" CACHE PATH "" FORCE)
    set(OPENSSL_INCLUDE_DIR "/opt/homebrew/opt/openssl@4/include" CACHE PATH "" FORCE)
    include_directories("/opt/homebrew/opt/openssl@4/include")
endif()
FetchContent_Declare(
        libdatachannel
        SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/libdatachannel-src"
        # Pinned to a pre-populated checkout rather than GIT_REPOSITORY: in
        # this sandboxed dev environment, FetchContent's own git-clone
        # subbuild step repeatedly wiped and re-cloned the 350MB checkout
        # (including submodules) on every reconfigure, which is slow and,
        # under this environment's flaky outbound git access, failed more
        # often than it succeeded. On a normal machine/CI with stable network
        # access, swap this back to:
        #   GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
        #   GIT_TAG v0.23.1
        #   GIT_SHALLOW TRUE
        #   GIT_SUBMODULES_RECURSE TRUE
        # and delete this SOURCE_DIR line; FetchContent will clone once and
        # reuse it on subsequent configures like any other dependency.
)
FetchContent_MakeAvailable(libdatachannel)

# ---------------------------------------------------------------------------
# Media / vision dependencies
# ---------------------------------------------------------------------------
find_package(ffmpeg REQUIRED)

# The Conan OpenCV package used by the original profile may omit opencv_contrib's
# face module. Prefer a local OpenCV installation that provides LBF support.
if(EXISTS "/opt/homebrew/lib/cmake/opencv5/OpenCVConfig.cmake")
    set(OpenCV_DIR "/opt/homebrew/lib/cmake/opencv5" CACHE PATH
        "OpenCV package configuration directory" FORCE)
endif()
find_package(OpenCV REQUIRED)
set(AVPLAYER_HAS_OPENCV_FACE FALSE)
foreach(OPENCV_INCLUDE_DIR IN LISTS OpenCV_INCLUDE_DIRS)
    if(EXISTS "${OPENCV_INCLUDE_DIR}/opencv2/face.hpp")
        set(AVPLAYER_HAS_OPENCV_FACE TRUE)
    endif()
endforeach()
if(NOT AVPLAYER_HAS_OPENCV_FACE)
    message(FATAL_ERROR "OpenCV with the face module is required; set OpenCV_DIR to a matching OpenCV installation")
endif()
if(TARGET opencv::opencv)
    set(AVPLAYER_OPENCV_LIBS opencv::opencv)
else()
    set(AVPLAYER_OPENCV_LIBS ${OpenCV_LIBS})
endif()
find_package(stb REQUIRED)
find_package(glm REQUIRED)

# Opus: WebRTC audio track (server encode) and Qt client decode.
find_package(Opus CONFIG QUIET)
if(NOT TARGET Opus::opus)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)
    add_library(Opus::opus INTERFACE IMPORTED)
    target_link_libraries(Opus::opus INTERFACE PkgConfig::OPUS)
endif()

# ---------------------------------------------------------------------------
# Qt: keep all modules on the same minor version
# ---------------------------------------------------------------------------
# Homebrew can leave qtbase on a newer minor than qtmultimedia/qtdeclarative
# after an unrelated `brew upgrade`/`brew reinstall`, which produces a
# link-time ABI crash. Resolve each module's actual installed Cellar version
# at configure time (rather than hardcoding one, which breaks the moment
# Homebrew bumps a revision suffix) and only pin them together when their
# major.minor agree.
function(avplayer_latest_cellar_version keg_name out_version)
    file(GLOB AVPLAYER_KEG_DIRS LIST_DIRECTORIES true "/opt/homebrew/Cellar/${keg_name}/*")
    set(AVPLAYER_LATEST "")
    foreach(AVPLAYER_KEG_DIR ${AVPLAYER_KEG_DIRS})
        if(IS_DIRECTORY "${AVPLAYER_KEG_DIR}")
            get_filename_component(AVPLAYER_KEG_VERSION "${AVPLAYER_KEG_DIR}" NAME)
            if(AVPLAYER_LATEST STREQUAL "" OR AVPLAYER_KEG_VERSION VERSION_GREATER AVPLAYER_LATEST)
                set(AVPLAYER_LATEST "${AVPLAYER_KEG_VERSION}")
            endif()
        endif()
    endforeach()
    set(${out_version} "${AVPLAYER_LATEST}" PARENT_SCOPE)
endfunction()

avplayer_latest_cellar_version(qtbase AVPLAYER_QTBASE_VERSION)
avplayer_latest_cellar_version(qtmultimedia AVPLAYER_QTMM_VERSION)
avplayer_latest_cellar_version(qtdeclarative AVPLAYER_QTDECL_VERSION)
# Compare only the major.minor prefix (e.g. "6.12"), ignoring the trailing
# "_N" Homebrew revision suffix which legitimately differs between modules.
string(REGEX MATCH "^[0-9]+\\.[0-9]+" AVPLAYER_QTBASE_MINOR "${AVPLAYER_QTBASE_VERSION}")
string(REGEX MATCH "^[0-9]+\\.[0-9]+" AVPLAYER_QTMM_MINOR "${AVPLAYER_QTMM_VERSION}")
string(REGEX MATCH "^[0-9]+\\.[0-9]+" AVPLAYER_QTDECL_MINOR "${AVPLAYER_QTDECL_VERSION}")

if(NOT AVPLAYER_QTBASE_VERSION STREQUAL ""
    AND AVPLAYER_QTBASE_MINOR STREQUAL AVPLAYER_QTMM_MINOR
    AND AVPLAYER_QTBASE_MINOR STREQUAL AVPLAYER_QTDECL_MINOR
    AND EXISTS "/opt/homebrew/Cellar/qtbase/${AVPLAYER_QTBASE_VERSION}/lib/cmake/Qt6/Qt6Config.cmake")
    set(Qt6_DIR "/opt/homebrew/Cellar/qtbase/${AVPLAYER_QTBASE_VERSION}/lib/cmake/Qt6" CACHE PATH
        "Qt6 package configuration directory" FORCE)
    foreach(QT_COMPONENT Core Gui Widgets OpenGL OpenGLWidgets Network Concurrent CoreTools GuiTools WidgetsTools Multimedia Quick Qml)
        if(QT_COMPONENT STREQUAL "Multimedia")
            set(QT_COMPONENT_ROOT "/opt/homebrew/Cellar/qtmultimedia/${AVPLAYER_QTMM_VERSION}/lib/cmake/Qt6Multimedia")
        elseif(QT_COMPONENT STREQUAL "Quick" OR QT_COMPONENT STREQUAL "Qml")
            set(QT_COMPONENT_ROOT "/opt/homebrew/Cellar/qtdeclarative/${AVPLAYER_QTDECL_VERSION}/lib/cmake/Qt6${QT_COMPONENT}")
        else()
            set(QT_COMPONENT_ROOT "/opt/homebrew/Cellar/qtbase/${AVPLAYER_QTBASE_VERSION}/lib/cmake/Qt6${QT_COMPONENT}")
        endif()
        if(EXISTS "${QT_COMPONENT_ROOT}")
            set(Qt6${QT_COMPONENT}_DIR "${QT_COMPONENT_ROOT}" CACHE PATH
                "Qt6 ${QT_COMPONENT} package configuration directory" FORCE)
        endif()
    endforeach()
    list(PREPEND CMAKE_PREFIX_PATH
        "/opt/homebrew/Cellar/qtbase/${AVPLAYER_QTBASE_VERSION}"
        "/opt/homebrew/Cellar/qtmultimedia/${AVPLAYER_QTMM_VERSION}"
        "/opt/homebrew/Cellar/qtdeclarative/${AVPLAYER_QTDECL_VERSION}")
else()
    message(WARNING "Qt modules are on mismatched minor versions "
            "(qtbase=${AVPLAYER_QTBASE_VERSION} qtmultimedia=${AVPLAYER_QTMM_VERSION} "
            "qtdeclarative=${AVPLAYER_QTDECL_VERSION}); falling back to default CMake "
            "package resolution, which may produce a link-time ABI crash. Run "
            "`brew upgrade qtbase qtmultimedia qtdeclarative` to realign them.")
endif()

find_package(Qt6 REQUIRED COMPONENTS
    Core Gui Widgets OpenGL OpenGLWidgets Multimedia Quick Network Concurrent)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)

# ---------------------------------------------------------------------------
# Deployment units
# ---------------------------------------------------------------------------
add_subdirectory(proto)
add_subdirectory(libs/media-core)

# media_server is independently buildable from services/media; in the
# top-level build both its executable and the smoke client are available.
option(AVPLAYER_BUILD_STREAMING_SMOKE_CLIENT
    "Build the standalone WebRTC receive smoke client" ON)
add_subdirectory(services/media)

add_subdirectory(apps/desktop)

add_subdirectory(services/gateway)
