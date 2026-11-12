# Shared media pipeline: demux/decode, A/V sync, frame types, and the
# recorder/encoder/muxer used by both the Qt desktop app and media_server.
# This target must not depend on desktop-only code (AV/, Qt Widgets, OpenGL).
add_library(avplayer_media_core STATIC)
add_library(avplayer::media_core ALIAS avplayer_media_core)

target_sources(avplayer_media_core PRIVATE
    src/Core/SyncNotifier.cpp
    src/Engine/AVSynchronizer.cpp
    src/Engine/AudioRender.cpp
    src/Reader/AudioDecoder.cpp
    src/Reader/DeMuxer.cpp
    src/Reader/FileReader.cpp
    src/Reader/VideoDecoder.cpp
    src/Writer/AudioEncoder.cpp
    src/Writer/Mp4Muxer.cpp
    src/Writer/Muxer.cpp
    src/Writer/RecordSession.cpp
    src/Writer/Recorder.cpp
    src/Writer/VideoEncoder.cpp
)

# Public: consumers include "Reader/FileReader.h", "Writer/IRecorder.h", ...
target_include_directories(avplayer_media_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(NOT DEFINED AVPLAYER_CACHE_DIR)
    set(AVPLAYER_CACHE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../cache")
endif()
target_compile_definitions(avplayer_media_core PRIVATE
    CACHE_DIR="${AVPLAYER_CACHE_DIR}"
)

# Conan's generated target only exposes include directories for the Debug
# configuration; keep Release/standalone builds working from the package
# variables as well.
if(DEFINED ffmpeg_INCLUDE_DIRS)
    target_include_directories(avplayer_media_core PUBLIC ${ffmpeg_INCLUDE_DIRS})
endif()

target_link_libraries(avplayer_media_core PUBLIC
    ffmpeg::ffmpeg
    Qt6::Core
    Qt6::Multimedia
)

# VideoRender/MiniDecoder are legacy standalone demo helpers: they pull in
# GLFW/GLEW/OpenCV and are not used by either deployed target, so they are
# intentionally not compiled into the shared library.
