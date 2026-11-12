# AVPlayer 架构说明（当前代码状态）

> 本文描述仓库**当前工作区**的实际结构，而不是目标态。目标态与演进建议见第 11 节。
>
> 相关文档：[README](README.md)、[启动说明](STARTUP.md)、[部署说明](deploy/README.md)、
> [Agent 服务](services/agent/README.md)、[媒体服务](services/media/README.md)。

---

## 1. 系统概览

AVPlayer 由四个可独立部署/运行的单元组成：

| 单元 | 形态 | 入口 |
| --- | --- | --- |
| Qt 桌面端 | C++ / Qt 6 可执行文件 | `AVPlayer` |
| Web 客户端 | Vite + TypeScript 静态站点 | `apps/web` |
| Agent 服务 | Python gRPC 服务 + Web 网关 | `avplayer-agent`、`avplayer-web-gateway` |
| 媒体服务 | C++ gRPC / WebRTC 服务 | `media_server` |

核心分层原则：

- **媒体面**：源文件解复用、解码、H.264/Opus 编码、WebRTC 推流由 `services/media` 负责；
- **控制面**：上传、会话、信令、播放控制、Agent 工具消息走 gRPC；
- **数据面**：`services/agent` 与 `services/media` 共享同一存储目录；
- **客户端**：Qt 默认本地播放（可切换拉流），Web 只能拉流。

需要特别澄清：**gRPC 不承载编码后的音视频**。gRPC 负责上传、信令和控制；
WebRTC 负责传输 H.264 视频 + Opus 音频。客户端仍然需要解码收到的 H.264/Opus
才能显示/播放，浏览器用内置解码器，Qt 用 libavcodec + libopus。

---

## 2. 部署视图

```mermaid
flowchart LR
    subgraph Clients[客户端]
        Qt[Qt 桌面端<br/>AVPlayer]
        Web[Web 客户端<br/>Vite / nginx]
    end

    subgraph Server[服务端]
        Agent[services/agent<br/>avplayer-agent :50061]
        Gateway[services/agent<br/>web-gateway :50065]
        Media[services/media<br/>media_server :50062]
    end

    Storage[(共享 storage)]

    Qt -->|gRPC UploadMedia / Converse| Agent
    Qt -->|gRPC Signal / Control| Media
    Qt <-->|WebRTC H.264 + Opus| Media

    Web -->|HTTP / WebSocket| Gateway
    Gateway -->|gRPC StreamingService| Media
    Web <-->|WebRTC H.264 + Opus| Media

    Agent --> Storage
    Media --> Storage
```

默认端口（均可用环境变量覆盖）：

| 端口 | 服务 | 协议 |
| --- | --- | --- |
| 50061 | `avplayer-agent` | gRPC `MediaService` / `AgentService` |
| 50062 | `media_server` | gRPC `StreamingService` + WebRTC |
| 50065 | `avplayer-web-gateway` | HTTP `/control`、`/config`、`/health`；WS `/signal` |
| 8080 | Compose 中的 Web 静态站点 | HTTP（nginx 反向代理到网关） |

---

## 3. 构建视图

根 [`CMakeLists.txt`](CMakeLists.txt) 只做依赖发现和子目录装配：

```mermaid
flowchart TD
    Proto[avplayer_proto<br/>proto/] --> Desktop
    MediaCore[avplayer_media_core<br/>libs/media-core/] --> Desktop
    MediaCore --> MediaSrv[media_server]
    Proto --> MediaSrv
    DesktopCore[avplayer_desktop_core] --> App[AVPlayer]
    MediaCore --> DesktopCore
    Proto --> DesktopCore
```

| CMake 目标 | 位置 | 依赖边界 |
| --- | --- | --- |
| `avplayer_proto` | [`proto/CMakeLists.txt`](proto/CMakeLists.txt) | 生成 C++/gRPC stubs，两个 C++ 部署单元共享 |
| `avplayer_media_core` | [`libs/media-core/CMakeLists.txt`](libs/media-core/CMakeLists.txt) | FFmpeg + Qt Core/Multimedia；不含桌面 UI、OpenGL |
| `avplayer_desktop_core` | [`apps/desktop/CMakeLists.txt`](apps/desktop/CMakeLists.txt) | media-core + proto + Qt Widgets/OpenGL + OpenCV + Opus |
| `AVPlayer` | 同上 | 只包含 `Main.cpp`、`MainWindow.*`、`ui/` |
| `media_server` | [`services/media/CMakeLists.txt`](services/media/CMakeLists.txt) | media-core + proto + datachannel + Qt Core/Multimedia + Opus |
| `streaming_smoke_client` | 同上（可选） | proto + datachannel，无 Qt GUI |

`media_server` 可以不经过根工程独立构建：

```sh
cmake -S services/media -B build/media -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/media --target media_server
```

---

## 4. 模块职责

### 4.1 `apps/desktop` — Qt 桌面端

```text
apps/desktop/
├── Main.cpp                 # QApplication 入口
├── MainWindow.{h,cpp}       # 装配播放链路、UI、Agent/AI 事件
├── include/IVideoFilter.h
├── src/
│   ├── AI/                  # AIAssistantService、TranscriptStore、豆包客户端、AIConfig
│   ├── Agent/               # AgentBridge（gRPC）、AgentProtocol、ToolDispatcher
│   ├── Core/TaskPool        # 桌面线程池
│   ├── Display/VideoDisplay # OpenGL YUV 纹理、GLSL、滤镜/贴纸合成
│   ├── Engine/GLContext     # 离屏 GL 上下文
│   ├── FaceDetect/          # OpenCV LBF 关键点与贴纸定位
│   ├── Network/             # URL 类型判定与连接状态
│   ├── Streaming/           # WebRtcPlaybackSource：拉流 + 本地 H.264 解码
│   └── VideoFilter/         # 灰度/反相/翻转/贴纸
└── ui/                      # PlayerWidget、ControllerWidget、AI/Agent 侧栏
```

职责要点：

- `MainWindow::startPlayback` 是播放总入口，根据 `AVPLAYER_STREAMING_MODE`
  选择本地 `FileReader` 或 `WebRtcPlaybackSource`；
- `AIAssistantService` 负责 AI 侧栏状态、任务请求、字幕旁路文件、剪辑导出；
- `AgentBridge` 是唯一的 Agent gRPC 客户端，负责上传媒体、`Converse` 双向流、
  工具结果回传；
- `ToolDispatcher` 只注册桌面端工具（`player_control`、`capture_screenshot`、
  `export_clip`）。

### 4.2 `libs/media-core` — 共享媒体核心

```text
libs/media-core/src/
├── Reader/    # FileReader、DeMuxer、AudioDecoder、VideoDecoder、接口
├── Engine/    # AVSynchronizer、AudioRender、IFrameSink
├── Core/      # SyncNotifier
├── Define/    # IAVPacket、IAudioSamples、IVideoFrame、AudioCompat
└── Writer/    # Recorder、RecordSession、AAC/H.264 编码、MP4 封装
```

- 桌面端和服务端共用同一套解复用/解码/同步/编码代码；
- `AudioCompat.h` 兼容 FFmpeg 4 与 FFmpeg 6+ 的声道布局 API；
- `Reader/MiniDecoder`、`Reader/VideoRender` 是未编译的旧演示代码，不参与任何
  部署单元。

### 4.3 `services/media` — C++ 媒体服务

```text
services/media/src/
├── main_media_server.cpp    # 进程入口：监听地址、存储根
├── MediaServer.{h,cpp}      # StreamingService 实现、会话表
├── MediaSession.{h,cpp}     # 单会话：FileReader + AVSynchronizer + WebRTC + 录制
├── WebRtcFrameSink.{h,cpp}  # H.264 编码 + RTP 打包
├── WebRtcAudioSink.{h,cpp}  # PCM → Opus 编码 + RTP 打包
└── streaming_smoke_client.cpp # 可选收流冒烟客户端
```

单次播放的流程：

1. 客户端通过 `Signal` 发送 SDP offer；
2. `MediaServer` 用 `session_id/media_id` 在共享存储里找到 `original.*`；
3. 创建 `MediaSession`，启动 `FileReader` 与同步器；
4. `WebRtcFrameSink` 把视频帧编码为 H.264，`WebRtcAudioSink` 把 PCM 重采样为
   48 kHz 并编码为 Opus，两者经 libdatachannel 推流；
5. `Control` 处理播放、暂停、seek、状态查询和服务端录制。

音视频帧使用真实 PTS 作为 RTP 时间戳，客户端据此显示字幕和做 A/V 同步。

### 4.4 `services/agent` — Python Agent 服务

```text
services/agent/avplayer_agent/
├── __main__.py        # avplayer-agent 入口
├── server.py          # MediaService / AgentService servicer
├── session_manager.py # 每 session 一个 AgentRuntime
├── runtime.py         # LangGraph 循环、模型选择、转写
├── ark_client.py      # 豆包方舟模型适配
├── local_tools.py     # 服务端 ffmpeg 工具
├── storage.py         # 会话存储布局
├── cache.py           # 可选 Redis 字幕缓存（失败自动退化）
├── db.py              # 可选 MySQL 持久化（失败自动退化）
├── web_gateway.py     # 浏览器 ↔ StreamingService 网关
├── settings.py        # 全部环境变量
└── protocol.py        # 消息 envelope
```

职责要点：

- `MediaService.UploadMedia`：把客户端流写入共享存储；
- `AgentService.Converse`：每个会话一条双向流，承载 Agent/AI 消息；
- `SessionManager`：按 `session_id` 隔离 `AgentRuntime` 和输出队列；
- `local_tools.py`：在服务端执行 `get_media_context`、`extract_audio`、
  `get_transcript`、`save_transcript`、`set_edit_plan`、`export_video`；
- `cache.py` / `db.py`：字幕读取顺序是 Redis → 磁盘 → MySQL，任一层不可用
  都静默退化为纯磁盘；MySQL 另存 `media`/`task_runs` 审计记录；
- `web_gateway.py`：浏览器无法直接调用双向 gRPC，因此提供 WebSocket/HTTP
  适配层；gRPC 流结束时同步关闭 WebSocket，避免浏览器挂在死连接上。

### 4.5 `apps/web` — Web 客户端

- Vite + TypeScript，单页应用；
- 通过 WebSocket `/signal` 与 media_server 协商 WebRTC；
- 通过 HTTP `/control` 播放/暂停/seek；
- 通过 `/config` 获取 ICE 配置；
- 通过 `POST /upload` 上传本地视频，成功后自动填入 `media_id`；未上传时
  点击“连接并播放”会先自动上传再协商；
- 通过 `/agent` WebSocket 使用 AI 助手与 Agent 工作台；
- 通过 `/download` 下载转写、剪辑和录制产物；
- 浏览器接收 H.264 + Opus 并本地解码渲染；
- 布局固定为一屏高度、隐藏滚动条，内容超出时面板内部滚动并在底部渐隐提示。

### 4.6 `proto` / `deploy` / `tests`

- [`proto/agent.proto`](proto/agent.proto)：唯一服务端契约来源；
- `deploy/`：Compose、Dockerfile、nginx、`.env.example`；
- `tests/`：gRPC 冒烟测试与手动转写检查。

---

## 5. 关键运行链路

### 5.1 本地播放（Qt 默认路径）

```mermaid
flowchart LR
    UI[MainWindow] --> FR[FileReader]
    FR --> DM[DeMuxer]
    DM --> AD[AudioDecoder]
    DM --> VD[VideoDecoder]
    AD --> SYNC[AVSynchronizer]
    VD --> SYNC
    SYNC --> AR[AudioRender / QAudioSink]
    SYNC --> PW[PlayerWidget / VideoDisplay]
```

- `m_streamingMode` 默认关闭（`AVPLAYER_STREAMING_MODE=0`）；
- 音频时钟可用时作为主时钟，否则回退视频时钟；
- 字幕由 `PlayerWidget` 按当前 PTS 叠加，不进入编码帧；
- 录制分支从解码回调并行取帧，写入 `cache/recordings/`。

### 5.2 上传与共享存储

上传是异步、按需的：

- 本地播放立即开始，同时后台为 `desktop` session 上传一份媒体（不阻塞 UI）；
- AI 侧栏第一次提交任务时，才懒上传到 `ai-sidebar` session；
- 拉流模式先异步上传 `desktop`，完成后自动连接 `media_server`；
- 服务端生成 `media_id`，文件落在
  `storage/<session_id>/<media_id>/original.<ext>`；
- `media_server` 与 `local_tools.py` 都从该目录读取。

### 5.3 桌面 WebRTC 拉流

开启 `AVPLAYER_STREAMING_MODE=1` 后：

1. 先上传媒体，拿到 `desktop` session 的 `media_id`；
2. `WebRtcPlaybackSource` 连接 `media_server:50062`；
3. 客户端发送 offer，服务端返回 answer；
4. 服务端解码源文件、编码 H.264 + Opus、推流；
5. 客户端用 libavcodec/libopus 解码，视频推入 `PlayerWidget`，音频交给 `QAudioSink`；
6. 播放/暂停/seek 走 `StreamingService.Control`。

当前限制：

- 音频/视频同步基于 RTP 时间戳和本地音频设备，尚未实现抖动缓冲；
- 录制由服务端完成，输出到共享存储，客户端只拿到路径；
- 该模式仍需要本地文件作为上传源，网络 URL 由客户端先上传。

### 5.4 Web 播放

```mermaid
sequenceDiagram
    participant B as Browser
    participant G as web_gateway
    participant M as media_server
    participant A as services/agent
    B->>G: WS /signal offer (session_id, media_id)
    G->>M: gRPC Signal
    M-->>G: answer + ICE
    G-->>B: answer + ICE
    M-->>B: WebRTC H.264 + Opus
    B->>G: POST /control play/pause/seek/status/record
    G->>M: gRPC Control
    B->>G: WS /agent (AI/Agent messages)
    G->>A: gRPC AgentService.Converse
```

Web 客户端通过网关 `POST /upload` 直接上传媒体，上传成功后自动填入
`media_id`；浏览器同时协商音频和视频轨道。

### 5.5 AI 助手任务

```mermaid
sequenceDiagram
    participant UI as AIAssistantPanel
    participant S as AIAssistantService
    participant A as services/agent
    UI->>S: RequestTranscription / Summary / Search / EditPlan
    S->>A: Converse task
    A->>A: extract_audio + Whisper/Doubao 或文本模型
    A-->>S: task_result
    S-->>UI: 任务结果文本
```

- 转写、摘要、搜索、剪辑计划都在服务端完成；
- 服务端把字幕写入 `storage/.../transcript.srt|.json`、剪辑计划写入
  `edit_plan.json`；
- `task_result` 现在携带结构化 `segments`，桌面端通过
  `ApplyServerTranscript` / `ApplyServerEditPlan` 注入 `TranscriptStore` 和
  `AIAssistantService`，因此播放器字幕、摘要/搜索/剪辑按钮状态会随服务端结果更新；
- `ApplyEditPlan` 已接入服务端返回的 `smart_edit` 片段。

### 5.6 Agent 工具循环

```mermaid
flowchart LR
    Goal[AgentPanel 目标] --> Bridge[AgentBridge]
    Bridge -->|Converse goal| Runtime[services/agent runtime]
    Runtime -->|tool_call| Bridge
    Bridge -->|桌面工具| Tools[player_control / screenshot / export_clip]
    Tools -->|tool_result| Bridge
    Bridge --> Runtime
    Runtime -->|final| Bridge
    Bridge --> Panel[AgentPanel]
```

- 模型可见工具由客户端 `tool_catalog` 与服务端 `local_tools.TOOLS` 合并；
- `get_transcript`、`extract_audio`、`save_transcript`、`set_edit_plan` 是内部
  工具，不作为模型直接可选项；
- 服务端工具通过 `SessionManager` 注入的 `tool_executor` 在进程内执行；
- 桌面工具通过 `tool_call` 回到 Qt，由 `ToolDispatcher` 执行。

### 5.7 录制

- 本地模式：`RecordSession` 接收解码后的音视频帧，停止时选择保存路径；
- 拉流模式：录制在 `services/media` 的 `MediaSession` 内完成，输出到
  `storage/<session>/<media_id>/recording-<millis>.mp4`，客户端只发送
  `record_start` / `record_stop` 并显示返回路径；
- 两种模式都是视频 H.264 + 音频 AAC，经 `Mp4Muxer` 封装；
- 本地临时文件在 `cache/recordings/recording_<millis>.mp4`。

---

## 6. 协议与数据模型

### 6.1 gRPC 服务

| 服务 | 进程 | RPC | 用途 |
| --- | --- | --- | --- |
| `MediaService` | `services/agent` | `UploadMedia(stream MediaChunk) -> MediaHandle` | 上传媒体 |
| `AgentService` | `services/agent` | `Converse(stream ClientMessage) -> stream ServerMessage` | Agent/AI 双向流 |
| `StreamingService` | `services/media` | `Signal(stream SignalMessage) -> stream SignalMessage` | WebRTC 信令 |
| `StreamingService` | `services/media` | `Control(ControlRequest) -> ControlAck` | 播放/暂停/seek/status/录制 |

`proto` 中的 `Struct` 是 JSON 字符串包装，避免为每种 payload 维护独立消息。
`request_id` 用于关联 `tool_call` 与 `tool_result`。

### 6.2 消息类型

- 客户端 → 服务端：`goal`、`task`、`provider`、`tool_catalog`、`tool_result`、
  `shutdown`
- 服务端 → 客户端：`ready`、`provider_status`、`status`、`tool_call`、
  `task_result`、`plan`、`final`、`error`

---

## 7. 存储布局

```text
storage/
└── <session_id>/
    └── <media_id>/
        ├── original.<ext>      # 上传的源媒体
        ├── transcript.srt      # 服务端转写
        ├── transcript.json     # 带毫秒时间戳的片段
        ├── edit_plan.json      # 智能剪辑计划
        └── clips/              # export_video / export_plan 产物
```

- `services/agent/avplayer_agent/storage.py` 负责创建路径；
- `services/media` 只读 `original.*`；
- 存储根由 `AVPLAYER_STORAGE_ROOT` 控制，默认 `<project>/storage`；
- Compose 中 agent 与 media 挂载同一个卷。

本地录制临时文件另存于 `cache/recordings/`。

### 7.1 缓存与持久化（可选）

Redis 与 MySQL 都可以用环境变量关闭；磁盘始终是事实来源，禁用后行为与纯磁盘版本完全一致：

| 层 | 内容 | 读写顺序 |
| --- | --- | --- |
| Redis | `avplayer:transcript:<session_id>:<media_id>`，TTL 默认 3600s | 字幕读取第一优先 |
| 磁盘 | `storage/` 下全部产物 | 唯一事实来源，`media_server` 直接读 |
| MySQL | `sessions` / `media` / `transcripts` / `segments` / `task_runs` | 磁盘缺失时的兜底，并记录任务审计 |

- 写入路径：`save_transcript` 先落盘，再写 Redis 和 MySQL；
- 读取路径：Redis 命中直接返回；未命中读磁盘并回填缓存；磁盘也缺失才查 MySQL；
- 表结构在首次连接时由 `db.py` 用 `CREATE TABLE IF NOT EXISTS` 自动创建；
- 连接失败、认证失败或驱动缺失都会自我禁用，不会让任务失败。

---

## 8. 配置

| 变量 | 使用方 | 默认 |
| --- | --- | --- |
| `AVPLAYER_PROJECT_ROOT` | agent、desktop、media | 当前工作目录 / 源码根 |
| `AVPLAYER_STORAGE_ROOT` | agent、media | `<project>/storage` |
| `AVPLAYER_AGENT_BIND` / `AVPLAYER_AGENT_PORT` | agent | `[::]` / `50061` |
| `AVPLAYER_AGENT_SERVER` | gateway | `localhost:50061` |
| `AVPLAYER_MEDIA_SERVER` | gateway、desktop | `localhost:50062` |
| `AVPLAYER_MEDIA_SERVER_ADDRESS` | media | `0.0.0.0:50062` |
| `AVPLAYER_WEB_GATEWAY_HOST` / `_PORT` | gateway | `127.0.0.1` / `50065` |
| `AVPLAYER_ICE_SERVERS` | media、gateway | `stun:stun.l.google.com:19302` |
| `AVPLAYER_AI_PROVIDER` | agent | `local` |
| `AVPLAYER_OLLAMA_HOST` | agent | `local-model.yaml` 中的值 |
| `AVPLAYER_STREAMING_MODE` | desktop | `0`（本地播放） |
| `AVPLAYER_REDIS_URL` | agent | `redis://127.0.0.1:6379/0`，置空禁用 |
| `AVPLAYER_REDIS_TTL` | agent | `3600` 秒 |
| `AVPLAYER_MYSQL_DSN` | agent | 空（禁用）；Compose 为 `mysql://avplayer:avplayer@mysql:3306/avplayer` |

模型配置：

- 本地：`resource/local-model.yaml` + Ollama + faster-whisper；
- 云端：`config/secret_key.yaml`（豆包语音 + 方舟文本）。

---

## 9. 并发模型

| 组件 | 并发方式 |
| --- | --- |
| `FileReader` / `DeMuxer` / 解码器 | 各自工作线程 + 队列 + `SyncNotifier` |
| `AVSynchronizer` | 视频队列（上限 60 帧）、主时钟调度、丢帧策略 |
| Qt UI | 主线程 + `QTimer` 渲染 tick；音频设备调用排队到主线程 |
| `AgentBridge` | 独立 gRPC 读线程 + 写锁；`UploadMedia` 使用独立 channel，经 QtConcurrent 异步执行 |
| `media_server` | gRPC 线程池；每个 `Signal` 会话一个 `MediaSession` + tick 线程 |
| `services/agent` | gRPC 线程池；每 session 一个 `AgentRuntime` 工作线程；会话消息按**每条连接一个队列广播**，避免多连接互抢 |
| `web_gateway` | asyncio；每个 WebSocket 一个 gRPC `Signal`/`Converse` 流；流结束即关闭对应 WebSocket |

---

## 10. 当前可用性矩阵

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| 本地播放 / seek / 滤镜 / 截图 | ✅ | 默认路径 |
| 本地录制 | ✅ | MP4，AAC + H.264 |
| Web 音视频拉流 | ✅ | H.264 + Opus |
| Qt 音视频拉流 | ✅ | `AVPLAYER_STREAMING_MODE=1`，含音频/状态/服务端录制 |
| 服务端转写 | ✅ | 结果写入 storage，并回注桌面 |
| 桌面字幕显示 | ✅ | 服务端 segments 直接注入 |
| 摘要 / 搜索 | ✅ | 转写状态回注后按钮可用 |
| 智能剪辑计划 | ✅ | 服务端 segments 回注并导出 |
| `export_clip` | ✅ | 使用回注后的剪辑片段 |
| `export_video` | ✅ | 服务端 ffmpeg；按 `start_ms/end_ms` 精确截取，或 `duration_ms` 随机截取，可多次调用 |
| 拉流模式录制 | ✅ | 服务端 `record_start` / `record_stop` |
| Web 上传 | ✅ | 网关 `/upload` |
| Web 录制 | ✅ | 服务端录制，`/download` 下载 |
| Web AI 助手 | ✅ | `/agent` WebSocket，转写/摘要/搜索/剪辑 |
| Web Agent 工作台 | ✅ | 目标、计划、工具状态与输出 |
| Web 剪辑导出 | ✅ | `export_plan` + `/download` |
| Web 动态贴纸 | ❌ | 浏览器端暂未实现人脸贴纸 |
| 音频回传 | ✅ | Opus |
| 字幕缓存（Redis） | ✅ | 可选；未启动时退化为磁盘 |
| 元数据持久化（MySQL） | ✅ | 可选；`media` / `task_runs` 审计 |

---

## 11. 演进建议

1. **抽象播放源**：`IPlaybackSource` + `LocalPlaybackSource`/`RemotePlaybackSource`，
   让 Qt 在两种模式间切换而不影响 `MainWindow` 的 UI 状态。
2. **音视频同步增强**：加入抖动缓冲、音频主时钟和首帧对齐，降低弱网下的漂移。
3. **服务端 URL 拉流**：网络源由 `services/media` 直接打开，不再要求客户端先上传。
4. **生产化**：TURN、HTTPS、鉴权、会话过期、Docker 网络下的 WebRTC 可达性。
5. **多客户端共享会话**：同一媒体多端观看时复用解码/编码管线，而不是每会话重复解码。

---

## 12. 验证命令

```sh
# C++ 构建
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug --target AVPlayer media_server -j8

# 独立媒体服务构建
cmake -S services/media -B build/media -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/media --target media_server

# Python 单元测试（runtime/缓存/数据库/导出；不需要 Redis/MySQL）
PYTHONPATH=services/agent .venv/bin/python -m unittest discover -s services/agent/tests -v

# gRPC 冒烟
PYTHONPATH=services/agent .venv/bin/python tests/grpc_smoke_client.py

# 本地统一启动
scripts/dev.sh

# Compose 配置校验
docker compose -f deploy/docker-compose.yml config

# 当前架构基准（推流帧率/延迟/控制面/转写/导出）
.venv/bin/python scripts/bench_metrics.py --help
```

测量方法与实测结果见 [BENCHMARK.md](BENCHMARK.md)。
