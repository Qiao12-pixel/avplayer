# Qt desktop application: player UI plus its desktop-only domain layer
# (AI/Agent clients, OpenGL display, filters, face detection). Shared media
# decoding/recording lives in libs/media-core; the agent orchestration lives in
# services/agent and is reached over gRPC.

file(GLOB_RECURSE AVPLAYER_DESKTOP_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_library(avplayer_desktop_core STATIC ${AVPLAYER_DESKTOP_CORE_SOURCES})
add_library(avplayer::desktop_core ALIAS avplayer_desktop_core)

target_include_directories(avplayer_desktop_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_definitions(avplayer_desktop_core PRIVATE
    AVPLAYER_PROJECT_ROOT="${AVPLAYER_PROJECT_ROOT}"
    RESOURCE_DIR="${AVPLAYER_RESOURCE_DIR}"
)
target_link_libraries(avplayer_desktop_core PUBLIC
    avplayer_media_core
    avplayer_proto
    ffmpeg::ffmpeg
    ${AVPLAYER_OPENCV_LIBS}
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Multimedia
    Qt6::Network
    Qt6::Concurrent
    GLEW::GLEW
    glfw
    datachannel
    Opus::opus
    "-framework OpenGL"
)

add_executable(AVPlayer
    Main.cpp
    MainWindow.cpp
    MainWindow.h
    ui/ClickableSlider.h
    ui/ControllerWidget.cpp
    ui/ControllerWidget.h
    ui/PlayerWidget.cpp
    ui/PlayerWidget.h
    ui/agent/AgentPanel.cpp
    ui/agent/AgentPanel.h
    ui/ai/AIAssistantPanel.cpp
    ui/ai/AIAssistantPanel.h
)

target_include_directories(AVPlayer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(AVPlayer PRIVATE
    AVPLAYER_PROJECT_ROOT="${AVPLAYER_PROJECT_ROOT}"
    AVPLAYER_QT_PLUGIN_PATH="/opt/homebrew/Cellar/qtbase/${AVPLAYER_QTBASE_VERSION}/share/qt/plugins"
)
target_link_libraries(AVPlayer PRIVATE
    avplayer_desktop_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Multimedia
    Qt6::Quick
    Qt6::Network
    Qt6::Concurrent
    datachannel
)

if(APPLE AND EXISTS "/opt/homebrew/Cellar/qtbase/${AVPLAYER_QTBASE_VERSION}/lib")
    # Homebrew's `opt` symlink may point qtbase at a newer minor release than
    # qtmultimedia. Rewrite the executable's framework IDs so CLion can launch
    # it without requiring DYLD_FRAMEWORK_PATH in the run configuration.
    set(AVPLAYER_QTBASE_LIB "/opt/homebrew/Cellar/qtbase/${AVPLAYER_QTBASE_VERSION}/lib")
    set(AVPLAYER_QTMM_LIB "/opt/homebrew/Cellar/qtmultimedia/${AVPLAYER_QTMM_VERSION}/lib")
    set(AVPLAYER_QTDECL_LIB "/opt/homebrew/Cellar/qtdeclarative/${AVPLAYER_QTDECL_VERSION}/lib")
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
        "/opt/homebrew/opt/qtbase/lib/QtNetwork.framework/Versions/A/QtNetwork"
        "${AVPLAYER_QTBASE_LIB}/QtNetwork.framework/Versions/A/QtNetwork" $<TARGET_FILE:AVPlayer>
        COMMAND install_name_tool -change
        "/opt/homebrew/opt/qtbase/lib/QtConcurrent.framework/Versions/A/QtConcurrent"
        "${AVPLAYER_QTBASE_LIB}/QtConcurrent.framework/Versions/A/QtConcurrent" $<TARGET_FILE:AVPlayer>
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
endif()
