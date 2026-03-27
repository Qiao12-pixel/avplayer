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
find_package(OpenCV REQUIRED)
find_package(stb REQUIRED)
find_package(glm REQUIRED)
include_directories(${ffmpeg_INCLUDE_DIRS})
include_directories(${stb_INCLUDE_DIRS})

set(HOMEBREW_OPENCV_FACE_INCLUDE_DIR "/opt/homebrew/Cellar/opencv/4.13.0_6/include/opencv4")
set(HOMEBREW_OPENCV_FACE_LIBRARY "/opt/homebrew/lib/libopencv_face.dylib")
set(HOMEBREW_OPENCV_CORE_LIBRARY "/opt/homebrew/lib/libopencv_core.dylib")
set(HOMEBREW_OPENCV_IMGPROC_LIBRARY "/opt/homebrew/lib/libopencv_imgproc.dylib")
set(HOMEBREW_OPENCV_OBJDETECT_LIBRARY "/opt/homebrew/lib/libopencv_objdetect.dylib")
if (EXISTS "${HOMEBREW_OPENCV_FACE_INCLUDE_DIR}/opencv2/face.hpp")
    include_directories(${HOMEBREW_OPENCV_FACE_INCLUDE_DIR})
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

)
# 为静态库添加头文件路径
#target_include_directories(${PROJECT_NAME} PUBLIC ${INCLUDE_DIRS} ${PROJECT_ROOT_DIR}/3rdparty/InspireFace/include)
# 为静态库添加链接库
target_link_libraries(${PROJECT_NAME} PRIVATE
        ffmpeg::ffmpeg
        opencv::opencv
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
if (EXISTS "${HOMEBREW_OPENCV_FACE_LIBRARY}")
    target_link_libraries(${PROJECT_NAME} PRIVATE "${HOMEBREW_OPENCV_FACE_LIBRARY}")
endif ()
if (EXISTS "${HOMEBREW_OPENCV_CORE_LIBRARY}")
    target_link_libraries(${PROJECT_NAME} PRIVATE "${HOMEBREW_OPENCV_CORE_LIBRARY}")
endif ()
if (EXISTS "${HOMEBREW_OPENCV_IMGPROC_LIBRARY}")
    target_link_libraries(${PROJECT_NAME} PRIVATE "${HOMEBREW_OPENCV_IMGPROC_LIBRARY}")
endif ()
if (EXISTS "${HOMEBREW_OPENCV_OBJDETECT_LIBRARY}")
    target_link_libraries(${PROJECT_NAME} PRIVATE "${HOMEBREW_OPENCV_OBJDETECT_LIBRARY}")
endif ()

# 添加 AVPlayer 源文件
file(GLOB_RECURSE DEMO_SOURCE_FILES ${PROJECT_ROOT_DIR}/qt/*.cpp)
# 创建 AVPlayer
add_executable(AVPlayer ${DEMO_SOURCE_FILES}
        )
# 为 AVPlayer 添加头文件路径
target_include_directories(AVPlayer PRIVATE ${PROJECT_ROOT_DIR}/qt ${PROJECT_ROOT_DIR}/LearnAV/qt)
# 为 AVPlayer 添加链接库
target_link_libraries(AVPlayer Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Multimedia Qt6::Quick ${PROJECT_NAME})
