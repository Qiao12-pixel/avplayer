# media-core

跨 Qt 桌面客户端与 C++ 服务端复用的媒体处理库。它封装 FFmpeg 解复用、解码、音视频时钟同步及编码/封装基础能力；不负责 HTTP、WebRTC 信令、MySQL、Redis 或服务注册。

源码按处理阶段分目录：`Reader/` 读取容器并产出 packet，`Core/` 与 `Define/` 放公共类型和流水线基础，`Writer/` 将编码后的 packet 写入目标容器；`Interface/` 定义读写端接口。桌面端通过 `apps/desktop` 使用它，服务端媒体会话由 `services/media` 在其上组织实时传输和录制。

修改此库会影响客户端和/或服务端的媒体流水线。构建目标和依赖见 `CMakeLists.txt`。
