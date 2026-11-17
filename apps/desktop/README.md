# Qt 桌面客户端

`apps/desktop` 是可选 Qt 客户端，不属于服务端启动必需项。`src/` 放 C++ 界面和客户端逻辑，`include/` 放公共头文件，`ui/` 放 AI/Agent 等界面资源。

代码大致按用途拆分：`Core/` 管理应用与播放状态，`Engine/` 连接共享 `libs/media-core` 播放流水线，`Display/` 负责视频输出，`Network/` 与 `Streaming/` 处理后端连接和远程 WebRTC 播放；`AI/`、`Agent/` 对接智能体功能，`FaceDetect/`、`VideoFilter/` 是客户端视觉处理功能。

服务端优先从根目录 `STARTUP.md` 启动；桌面端构建目标、Qt 依赖及本地运行方式见根 `CMakeLists.txt` 和 `apps/desktop/CMakeLists.txt`。
