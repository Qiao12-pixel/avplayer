cmake_minimum_required(VERSION 3.23)
project(LearnAV VERSION 0.0.0)

# 设置编译器
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if (CMAKE_BUILD_TYPE STREQUAL Debug)
    add_definitions(-DDEBUG)
endif ()

# 设置 CCache
find_program(CCACHE_FOUND ccache)
if (CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif (CCACHE_FOUND)

# 设置路径
set(PROJECT_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resource")
set(CACHE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cache")
add_definitions(-DRESOURCE_DIR="${RESOURCE_DIR}")
add_definitions(-DCACHE_DIR="${CACHE_DIR}")

# 通过 Conan 添加依赖
find_package(ffmpeg REQUIRED)

# The Conan OpenCV package used by the original profile may omit opencv_contrib's
# face module. Prefer a local OpenCV installation that provides LBF support.
if (EXISTS "/opt/homebrew/lib/cmake/opencv5/OpenCVConfig.cmake")
    set(OpenCV_DIR "/opt/homebrew/lib/cmake/opencv5" CACHE PATH
        "OpenCV package configuration directory" FORCE)
endif ()
find_package(OpenCV REQUIRED)
set(AVPLAYER_HAS_OPENCV_FACE FALSE)
foreach (OPENCV_INCLUDE_DIR IN LISTS OpenCV_INCLUDE_DIRS)
    if (EXISTS "${OPENCV_INCLUDE_DIR}/opencv2/face.hpp")
        set(AVPLAYER_HAS_OPENCV_FACE TRUE)
    endif ()
endforeach ()
if (NOT AVPLAYER_HAS_OPENCV_FACE)
    message(FATAL_ERROR "OpenCV with the face module is required; set OpenCV_DIR to a matching OpenCV installation")
endif ()
if (TARGET opencv::opencv)
    set(AVPLAYER_OPENCV_LIBS opencv::opencv)
else ()
    set(AVPLAYER_OPENCV_LIBS ${OpenCV_LIBS})
endif ()
find_package(stb REQUIRED)
find_package(glm REQUIRED)
include_directories(${ffmpeg_INCLUDE_DIRS})
include_directories(${OpenCV_INCLUDE_DIRS})
include_directories(${stb_INCLUDE_DIRS})

# Keep all Qt modules on the same minor version. Homebrew can leave qtbase at
# 6.11 while qtmultimedia is still 6.10, which produces a link-time ABI crash.
if (EXISTS "/opt/homebrew/Cellar/qtbase/6.10.2/lib/cmake/Qt6/Qt6Config.cmake"
    AND EXISTS "/opt/homebrew/Cellar/qtmultimedia/6.10.2/lib/cmake/Qt6Multimedia/Qt6MultimediaConfig.cmake"
    AND EXISTS "/opt/homebrew/Cellar/qtdeclarative/6.10.2/lib/cmake/Qt6Quick/Qt6QuickConfig.cmake")
    set(Qt6_DIR "/opt/homebrew/Cellar/qtbase/6.10.2/lib/cmake/Qt6" CACHE PATH
        "Qt6 package configuration directory" FORCE)
    foreach (QT_COMPONENT Core Gui Widgets OpenGL OpenGLWidgets CoreTools GuiTools WidgetsTools Multimedia Quick Qml)
        if (QT_COMPONENT STREQUAL "Multimedia")
            set(QT_COMPONENT_ROOT "/opt/homebrew/Cellar/qtmultimedia/6.10.2/lib/cmake/Qt6Multimedia")
        elseif (QT_COMPONENT STREQUAL "Quick" OR QT_COMPONENT STREQUAL "Qml")
            set(QT_COMPONENT_ROOT "/opt/homebrew/Cellar/qtdeclarative/6.10.2/lib/cmake/Qt6${QT_COMPONENT}")
        else ()
            set(QT_COMPONENT_ROOT "/opt/homebrew/Cellar/qtbase/6.10.2/lib/cmake/Qt6${QT_COMPONENT}")
        endif ()
        if (EXISTS "${QT_COMPONENT_ROOT}")
            set(Qt6${QT_COMPONENT}_DIR "${QT_COMPONENT_ROOT}" CACHE PATH
                "Qt6 ${QT_COMPONENT} package configuration directory" FORCE)
        endif ()
    endforeach ()
    list(PREPEND CMAKE_PREFIX_PATH
        "/opt/homebrew/Cellar/qtbase/6.10.2"
        "/opt/homebrew/Cellar/qtmultimedia/6.10.2"
        "/opt/homebrew/Cellar/qtdeclarative/6.10.2")
endif ()

# 添加 QT
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Multimedia Quick)

find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
# 添加头文件路径
set(INCLUDE_DIRS
        ${PROJECT_ROOT_DIR}/LearnAV/include
        ${PROJECT_ROOT_DIR}/LearnAV/src
        ${Qt6_INCLUDES}
        ${Qt6OpenGL_INCLUDE_DIRS}
        ${Qt6OpenGLWidgets_INCLUDE_DIRS}
        ${Qt6Multimedia_INCLUDE_DIRS}
)

# 添加静态库源文件
file(GLOB_RECURSE LIBRARY_SOURCE_FILES ${PROJECT_ROOT_DIR}/LearnAV/*.cpp)
# 创建静态库
add_library(${PROJECT_NAME} STATIC ${LIBRARY_SOURCE_FILES}
        AV/src/AI/AIAssistantService.cpp
        AV/src/AI/AIAssistantService.h
        AV/src/AI/IPlayerControl.h
        AV/src/Core/TaskPool.cpp
        AV/src/Core/TaskPool.h
        AV/src/Reader/Interface/IDeMuxer.h
        AV/src/Define/BaseDef.h
        AV/src/Define/IAVPacket.h
        AV/src/Reader/DeMuxer.cpp
        AV/src/Reader/DeMuxer.h
        AV/src/Core/SyncNotifier.cpp
        AV/src/Core/SyncNotifier.h
        AV/src/Reader/MiniDecoder.cpp
        AV/src/Reader/MiniDecoder.h
        AV/src/Define/IAudioSamples.h
        AV/src/Define/IVideoFrame.h
        AV/src/Engine/IGLContext.h
        AV/src/Engine/GLContext.cpp
        AV/src/Reader/Interface/IVideoDecoder.h
        AV/src/Reader/Interface/IAudioDecoder.h
        AV/src/Reader/VideoDecoder.cpp
        AV/src/Reader/VideoDecoder.h
        AV/src/Reader/AudioDecoder.cpp
        AV/src/Reader/AudioDecoder.h
        AV/src/Reader/Interface/IFileReader.h
        AV/src/Reader/FileReader.cpp
        AV/src/Reader/FileReader.h
        AV/src/Reader/VideoRender.cpp
        AV/src/Reader/VideoRender.h
        AV/src/Engine/AudioRender.cpp
        AV/src/Engine/AudioRender.h
        AV/src/Engine/AVSynchronizer.cpp
        AV/src/Engine/AVSynchronizer.h
        AV/src/Display/VideoDisplay.cpp
        AV/src/Display/VideoDisplay.h
        AV/src/FaceDetect/IFaceLandmarkDetector.h
        AV/src/FaceDetect/OpenCVLbfFaceLandmarkDetector.cpp
        AV/src/FaceDetect/OpenCVLbfFaceLandmarkDetector.h
        AV/src/FaceDetect/StickerPlacementEstimator.cpp
        AV/src/FaceDetect/StickerPlacementEstimator.h
        AV/src/VideoFilter/FlipVerticalFilter.cpp
        AV/src/VideoFilter/FlipVerticalFilter.h
        AV/src/VideoFilter/GrayFilter.cpp
        AV/src/VideoFilter/GrayFilter.h
        AV/src/VideoFilter/InvertFilter.cpp
        AV/src/VideoFilter/InvertFilter.h
        AV/src/VideoFilter/StickerFilter.cpp
        AV/src/VideoFilter/StickerFilter.h
        AV/src/VideoFilter/VideoFilter.cpp
        AV/src/VideoFilter/VideoFilter.h
        AV/include/IVideoFilter.h
        AV/src/Network/NetworkSession.cpp
        AV/src/Network/NetworkSession.h
        AV/src/Writer/IMediaWriter.h
        AV/src/Writer/IRecorder.h
        AV/src/Writer/IRecordSession.h
        AV/src/Writer/IMuxer.h
        AV/src/Writer/Interface/IAudioEncoder.h
        AV/src/Writer/Interface/IVideoEncoder.h
        AV/src/Writer/AudioEncoder.cpp
        AV/src/Writer/AudioEncoder.h
        AV/src/Writer/Recorder.cpp
        AV/src/Writer/Recorder.h
        AV/src/Writer/RecordSession.cpp
        AV/src/Writer/RecordSession.h
        AV/src/Writer/MediaWriter.h
        AV/src/Writer/Muxer.cpp
        AV/src/Writer/Muxer.h
        AV/src/Writer/Mp4Muxer.cpp
        AV/src/Writer/Mp4Muxer.h
        AV/src/Writer/VideoEncoder.cpp
        AV/src/Writer/VideoEncoder.h

)
# 为静态库添加头文件路径
#target_include_directories(${PROJECT_NAME} PUBLIC ${INCLUDE_DIRS} ${PROJECT_ROOT_DIR}/3rdparty/InspireFace/include)
# 为静态库添加链接库
target_link_libraries(${PROJECT_NAME} PRIVATE
        ffmpeg::ffmpeg
        ${AVPLAYER_OPENCV_LIBS}
        stb::stb
        glm::glm
        Qt6::Widgets       # <--- 必须添加这一行
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        Qt6::Multimedia
        GLEW::GLEW
        glfw
        "-framework OpenGL" # macOS 原生 OpenGL 框架
)
# 添加 AVPlayer 源文件
file(GLOB_RECURSE DEMO_SOURCE_FILES ${PROJECT_ROOT_DIR}/qt/*.cpp)
# 创建 AVPlayer
add_executable(AVPlayer ${DEMO_SOURCE_FILES})
# 为 AVPlayer 添加头文件路径
target_include_directories(AVPlayer PRIVATE ${PROJECT_ROOT_DIR}/qt ${PROJECT_ROOT_DIR}/LearnAV/qt)
# 为 AVPlayer 添加链接库
target_link_libraries(AVPlayer Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Multimedia Qt6::Quick ${PROJECT_NAME})

if (APPLE AND EXISTS "/opt/homebrew/Cellar/qtbase/6.10.2/lib")
    # Homebrew's `opt` symlink may point qtbase at a newer minor release than
    # qtmultimedia. Rewrite the executable's framework IDs so CLion can launch
    # it without requiring DYLD_FRAMEWORK_PATH in the run configuration.
    set(AVPLAYER_QTBASE_LIB "/opt/homebrew/Cellar/qtbase/6.10.2/lib")
    set(AVPLAYER_QTMM_LIB "/opt/homebrew/Cellar/qtmultimedia/6.10.2/lib")
    set(AVPLAYER_QTDECL_LIB "/opt/homebrew/Cellar/qtdeclarative/6.10.2/lib")
    add_custom_command(TARGET AVPlayer POST_BUILD
        COMMAND install_name_tool -change
        "/opt/homebrew/opt/qtbase/lib/QtCore.framework/Versions/A/QtCore"
        "${AVPLAYER_QTBASE_LIB}/QtCore.framework/Versions/A/QtCore" $<TARGET_FILE:AVPlayer>
        COMMAND install_name_tool -change
        "/opt/homebrew/opt/qtbase/lib/QtGui.framework/Versions/A/QtGui"
        "${AVPLAYER_QTBASE_LIB}/QtGui.framework/Versions/A/QtGui" $<TARGET_FILE:AVPlayer>
        COMMAND install_name_tool -change
        "/opt/homebrew/opt/qtbase/lib/QtWidgets.framework/Versions/A/QtWidgets"
        "${AVPLAYER_QTBASE_LIB}/QtWidgets.framework/Versions/A/QtWidgets" $<TARGET_FILE:AVPlayer>
        COMMAND install_name_tool -change
        "/opt/homebrew/opt/qtbase/lib/QtOpenGL.framework/Versions/A/QtOpenGL"
        "${AVPLAYER_QTBASE_LIB}/QtOpenGL.framework/Versions/A/QtOpenGL" $<TARGET_FILE:AVPlayer>
        COMMAND install_name_tool -change
        "/opt/homebrew/opt/qtbase/lib/QtOpenGLWidgets.framework/Versions/A/QtOpenGLWidgets"
        "${AVPLAYER_QTBASE_LIB}/QtOpenGLWidgets.framework/Versions/A/QtOpenGLWidgets" $<TARGET_FILE:AVPlayer>
        COMMAND install_name_tool -change
        "/opt/homebrew/opt/qtmultimedia/lib/QtMultimedia.framework/Versions/A/QtMultimedia"
        "${AVPLAYER_QTMM_LIB}/QtMultimedia.framework/Versions/A/QtMultimedia" $<TARGET_FILE:AVPlayer>
        COMMAND install_name_tool -change
        "/opt/homebrew/opt/qtdeclarative/lib/QtQuick.framework/Versions/A/QtQuick"
        "${AVPLAYER_QTDECL_LIB}/QtQuick.framework/Versions/A/QtQuick" $<TARGET_FILE:AVPlayer>
        COMMAND install_name_tool -change
        "/opt/homebrew/opt/qtdeclarative/lib/QtQml.framework/Versions/A/QtQml"
        "${AVPLAYER_QTDECL_LIB}/QtQml.framework/Versions/A/QtQml" $<TARGET_FILE:AVPlayer>
    )
endif ()
