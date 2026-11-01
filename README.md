# 📋 项目简介

AVPlayer 是一款基于 **C++17、Qt、FFmpeg 和 OpenGL** 的桌面音视频播放器。项目包含本地/网络媒体播放、音视频同步、录制、截图与视频滤镜；OpenCV 用于人脸关键点检测和动态贴纸定位。AI 助手提供字幕转写、视频摘要、字幕语义搜索和智能剪辑，Agent 工作台通过 Python 编排客户端工具。

Agent 编排采用 Python Runtime，播放器能力由 C++ 工具执行，双方通过 `avplayer.agent.v1` JSON Lines 协议通信。各模块职责与完整数据流见下文“项目结构与数据流”。
<img width="1440" height="932" alt="image" src="https://github.com/user-attachments/assets/77833f4a-5ed4-46df-89e2-33adb9c060b3" />


# ✨ 核心功能

## 基础播放

-   基于 FFmpeg 解复用与解码常见音视频格式；实际可播放格式取决于所链接的 FFmpeg 构建
-   支持本地文件以及由 FFmpeg 打开的 HTTP(S)、RTSP、RTMP 等网络媒体源
-   播放控制：播放 / 暂停、进度拖拽和跳转到开头

## 高级功能

-   🎥 **音视频录制**：对当前媒体的解码帧重新编码，封装为 MP4
-   🎨 **视频滤镜**：灰度、反相、垂直翻转及动态贴纸
-   🧠 **人脸关键点**：通过 OpenCV LBF 检测并辅助贴纸定位
-   📸 **画面截图**：保存播放器当前显示画面
-   🤖 **AI 与 Agent**：字幕转写、文本分析、语义搜索、智能剪辑及自然语言工具编排

# 🏗️ 技术栈

| 模块 | 技术 |
| --- | --- |
| 客户端 | C++17、Qt 6 Widgets / Multimedia |
| 解复用、解码、编码与封装 | FFmpeg |
| 视频渲染 | OpenGL、GLSL、`QOpenGLWidget` |
| 人脸关键点与贴纸定位 | OpenCV LBF |
| AI 助手与 Agent | Python 3 Runtime；本地 Ollama/Qwen3 + Whisper，或豆包语音 API + 火山方舟文本模型 |
| 构建与依赖 | CMake、Conan |

## 🚀 快速开始

### 环境依赖

-   当前开发构建环境：macOS、Qt 6（各模块版本一致）；其他平台需自行验证依赖和构建配置
-   构建工具：CMake 3.23+、Conan 2.x、C++17 编译器
-   核心依赖：FFmpeg、包含 `face` 模块的 OpenCV、Qt 6 Widgets / Multimedia / OpenGL
-   AI/Agent 运行依赖：Python 3、`ffmpeg` 命令；本地模式需要 Ollama 与模型文件，豆包模式需要 API 密钥


-   **克隆项目**  
    ```
    git clone https://github.com/Qiao12-pixel/avplayer.git
    cd avplayer
    ```
    
-   **安装依赖（Conan）**
    ```
    conan install . --build=missing -s compiler.cppstd=17
    ```
    
-   **CMake 构建**
    ```
    mkdir build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j8
    ```
-   **运行程序**
    ```
    ./AVPlayer
    ```

Agent 和 AI 助手需要先安装 Python 依赖（在项目根目录执行）：

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r agent/requirements.txt
```

客户端会优先启动 `.venv/bin/python`。AI 助手和 Agent 工作台的“模型方式”会同步切换，并记住上次选择。本地模式读取 `resource/local-model.yaml`；豆包模式由 Python Runtime 读取 `config/secret_key.yaml`。模型文件需先准备好，安装方式见[AI 助手模型方式](#ai-助手模型方式)。

在项目根目录运行回归测试：

```sh
PYTHONPATH=agent .venv/bin/python -m unittest discover -s agent/tests -v
```

## 📁 项目结构与数据流

以下目录树按实际源码职责整理；`resource/` 和 `config/secret_key.yaml` 属于本地资源与配置，构建产物不在树中。

项目架构图、播放流水线图、Agent 时序图和 AI/录制数据流图已整理为可编辑的多页面 Draw.io 文件：[AVPlayer-architecture.drawio](docs/diagrams/AVPlayer-architecture.drawio)。

客户端品牌资源：[avplayer-logo.svg](resource/avplayer-logo.svg)（可编辑源稿）和 [avplayer-logo.png](resource/avplayer-logo.png)（运行时应用图标）。

```text
AVPlayer/
├── AV/
│   ├── include/                 # 公共视频滤镜接口
│   └── src/
│       ├── Reader/              # FileReader、DeMuxer、音视频解码器
│       │   └── Interface/       # 读取、解复用、解码的 Listener 接口
│       ├── Engine/              # AVSynchronizer、AudioRender、GLContext
│       ├── Display/             # VideoDisplay：YUV 纹理和 GLSL 渲染
│       ├── VideoFilter/         # 灰度、反相、翻转、贴纸滤镜
│       ├── FaceDetect/          # OpenCV LBF 人脸关键点与贴纸位置
│       ├── Writer/              # RecordSession、Recorder、编码器、MP4 封装
│       ├── Network/             # 媒体源 URL 类型与连接状态
│       ├── AI/                  # 字幕存储、AI 任务入口、剪辑计划与导出
│       ├── Agent/               # Python 桥接、JSON 协议、工具分发、片段导出
│       ├── Core/                # 线程通知与任务池
│       └── Define/              # 音视频帧、压缩包等基础数据结构
├── qt/
│   ├── Main.cpp                 # Qt 应用入口
│   ├── MainWindow.{h,cpp}       # 播放器装配、事件与工具接线
│   └── ui/
│       ├── PlayerWidget.*       # QOpenGLWidget 画面与字幕叠加
│       ├── ControllerWidget.*   # 底部播放、录制、截图与滤镜控制
│       ├── ai/                  # AI 助手侧栏界面
│       └── agent/               # Agent 工作台侧栏界面
├── agent/
│   ├── main.py                  # Python Runtime 入口、stdin/stdout 循环
│   ├── runtime.py               # LangGraph 工作流、本地/豆包双模型与转写
│   ├── ark_client.py            # 豆包方舟 LangChain 模型适配
│   ├── protocol.py              # JSON Lines 消息格式
│   ├── requirements.txt         # Python Agent 依赖
│   ├── tests/                   # Runtime 模式与工具往返测试
│   ├── tools/                   # 工具扩展说明，当前无独立 Python 工具实现
│   └── schemas/                 # 协议/参数扩展说明，当前无独立 schema 文件
├── config/
│   ├── secret_key.example.yaml  # API 配置样例
│   └── secret_key.yaml          # 本地密钥，需自行创建且不应提交
├── resource/                    # 本地示例媒体和贴纸素材
│   ├── local-model.yaml         # 本地 Qwen3 / Whisper 配置
│   ├── ollama/                  # Ollama 模型文件（下载后产生）
│   └── whisper-small/           # Whisper 模型文件（下载后产生）
├── scripts/
│   └── download_local_models.sh # 下载本地模型与 Python 依赖
├── CMakeLists.txt               # C++/Qt 目标及依赖配置
├── conanfile.txt                # Conan 依赖声明
└── README.md
```

### 播放与渲染数据流

```mermaid
flowchart LR
    Source[本地文件或网络 URL] --> Demux[DeMuxer / FFmpeg 解复用]
    Demux --> AudioDecoder[AudioDecoder]
    Demux --> VideoDecoder[VideoDecoder]
    AudioDecoder --> AudioSamples[PCM 音频采样]
    VideoDecoder --> VideoFrames[YUV420P 视频帧]
    AudioSamples --> Sync[AVSynchronizer]
    VideoFrames --> Sync
    Sync --> AudioOut[AudioRender / QAudioSink]
    Sync --> Player[PlayerWidget / VideoDisplay]
    Player --> GPU[OpenGL YUV 纹理 + GLSL 转 RGB、滤镜]
    GPU --> Overlay[Qt 叠加字幕]
```

`MainWindow` 创建 `FileReader`，由它串联解复用器与两个解码器；解码回调把音视频数据交给 `AVSynchronizer`。音频经 `QAudioSink` 输出，音频时钟可用时作为主时钟；无有效音频时回退到视频时钟。视频调度使用 `视频帧 PTS - 主时钟`，超前不超过 10 ms 时显示，使用音频时钟且落后超过 80 ms 时丢帧。视频解码器把非 YUV420P 帧转换为 YUV420P；`VideoDisplay` 将 Y、U、V 平面上传 GPU，由片段着色器完成 YUV→RGB、灰度/反相及贴纸合成。字幕依据当前呈现帧的时间戳在 `PlayerWidget` 中叠加，因此不进入编码帧本身。

拖动进度条时，`MainWindow` 将目标位置交给 `FileReader/DeMuxer`；解复用器发出 `KFlush` 标记包，音视频解码器清理旧包并刷新解码状态，视频同步队列收到刷新帧后清空旧画面及视频时钟，再从新位置的时间戳继续调度。

| 跨层数据 | 产生位置 | 消费位置 | 关键字段或用途 |
| --- | --- | --- | --- |
| `IAVPacket` | `DeMuxer` | `AudioDecoder` / `VideoDecoder` | FFmpeg 压缩包、时间基与 `KFlush` 标记 |
| `IAudioSamples` | `AudioDecoder` | `AVSynchronizer` / `Recorder` | 重采样后的 PCM、PTS、采样率与声道数 |
| `IVideoFrame` | `VideoDecoder` | `AVSynchronizer` / `Recorder` | 紧凑排列的 YUV420P、PTS、色彩范围与矩阵信息 |
| `TranscriptSegment` | 豆包语音识别或旁路 `.srt` | 字幕显示、摘要/搜索/剪辑 | 毫秒起止时间与字幕文本 |
| Agent JSON 消息 | Qt 或 Python Runtime | 对端进程 | 协议版本、消息类型、`request_id`、工具参数或结果 |

解复用器和音视频解码器各有工作线程及异步队列，`Listener` 回调把数据推向下一层。音视频数据以 `shared_ptr` 传递；包/帧销毁时通过释放回调归还流水线额度，限制上游继续生产。同步器的视频队列上限为 60 帧，超限时丢弃最早帧。音频样本进入 Qt 音频设备前经主线程排队调用，避免从解码线程直接操作 `QAudioSink`。

### 录制数据流

```mermaid
flowchart LR
    Decoded[解码后的音频采样 / 视频帧] --> Window[MainWindow 解码回调]
    Window --> Playback[同步与播放分支]
    Window --> Session[RecordSession 录制分支]
    Session --> Recorder[Recorder]
    Recorder --> AudioEncoder[AudioEncoder / AAC]
    Recorder --> VideoEncoder[VideoEncoder / H.264]
    AudioEncoder --> Muxer[Mp4Muxer]
    VideoEncoder --> Muxer
    Muxer --> MP4[临时 MP4 → 保存文件]
```

录制接收的是解码后的音视频帧，与播放分支并行；`RecordSession` 管理开始、停止、临时文件和最终保存。这里不是对播放器窗口进行屏幕录制，界面字幕和仅在显示阶段合成的滤镜/贴纸不会自动写入录制输出。

### AI 助手与 Agent 数据流

```mermaid
flowchart LR
    Goal[AgentPanel 目标] --> Bridge[AgentBridge / QProcess]
    Bridge <-->|JSON Lines| Runtime[agent/main.py + runtime.py]
    Runtime <-->|文本请求| Model[本地 Ollama 或方舟模型]
    Runtime -->|tool_call| Bridge
    Bridge -->|C++ 分发| Tools[播放器工具 / AIAssistantService / ClipExportService]
    Tools -->|tool_result| Bridge
    Bridge --> Runtime
    Runtime -->|final| Bridge
    Bridge --> UI[AgentPanel 输出]
```

### AI 助手侧栏

`AIAssistantService` 向 Python Runtime 提交任务：C++ 用 FFmpeg 提取音轨 → Python 按所选模式运行本地 Whisper 或豆包语音 API → C++ 保存 `.ai.srt` 并在播放器同步显示。摘要、语义搜索和智能剪辑由 Python 读取 C++ 返回的字幕文本，并调用本地 Ollama/Qwen3 或方舟文本模型；剪辑计划由 C++ 校验并保存。

### Agent 侧栏

将自然语言目标交给 LangGraph；模型通过 LangChain 在 C++ 工具目录中选择操作，Qt 执行后回传结果。随机片段导出由 `ClipExportService` 直接对当前本地视频调用 FFmpeg，不依赖字幕。C++ 不直接向大模型或语音 API 发起网络请求。

| Agent 工具 | 执行方式 | 前置条件 / 结果 |
| --- | --- | --- |
| `get_media_context`、`player_control` | C++ 同步 | 读取媒体状态，或对已加载媒体执行播放控制 |
| `capture_screenshot` | C++ 同步 | 需要已呈现画面；输出 PNG |
| `transcribe_subtitle` | Python 调用 C++ 音轨工具，再运行所选语音模型 | 本地媒体；生成 `.ai.srt` |
| `summarize_video`、`semantic_search` | Python 文本模型，C++ 提供字幕 | 已加载字幕；搜索还需要查询文本 |
| `smart_edit`、`export_clip` | Python 生成计划，C++ 校验并导出 | 带时间戳的字幕与本地媒体；先生成建议，再指定目录导出 MP4 |
| `export_random_clip` | `ClipExportService` 异步 | 本地媒体与 `duration_ms`；未指定绝对输出目录时保存到系统“影片/AVPlayer”，并返回绝对 MP4 路径 |

两进程通过 `avplayer.agent.v1` 协议传输 `goal`、`task`、`provider`、`provider_status`、`tool_catalog`、`tool_call`、`tool_result`、`task_result`、`status`、`final` 等消息，`request_id` 关联调用和结果。Agent 使用 LangChain 原生工具调用与 LangGraph 的模型/工具循环；当前图在本地工作线程运行，可等待异步 C++ 工具结果，但尚未接入持久化 checkpointer。Agent 面板按实际工具调用动态展示执行步骤。摘要、语义搜索与智能剪辑只分析字幕，尚无视频帧视觉理解或向量索引；`agent/tools/` 与 `agent/schemas/` 目前是扩展预留目录。

### Agent 后续阶段

当前已接入 LangChain 模型适配与 LangGraph 基础模型/工具循环，支持本地模型与豆包 API 两条路径；保持 **Qt/C++ 执行媒体操作、Python 本地进程负责模型通信和编排**。后续扩展状态恢复、真实计划节点和 Skill。参考 [LangChain Agents](https://docs.langchain.com/oss/python/langchain/agents) 与 [LangGraph 概览](https://docs.langchain.com/oss/python/langgraph/overview)。

1. **先稳定协议**：为 `goal` / `tool_call` / `tool_result` 增加结构校验、超时、取消和请求关联测试；保持 `AgentBridge` 与 Qt 工具接口不变。Python 工具适配器负责发出 JSON Lines 请求并等待对应 `request_id` 的结果。
2. **扩展 LangGraph 工作流**：在现有模型/工具循环上增加“读取媒体上下文 → 判断是否需要字幕 → 必要时转写 → 规划/选择工具 → 等待 C++ 执行结果 → 校验输出 → 汇总”等显式节点；对文件覆写等高影响动作增加用户确认节点。
3. **增加恢复与可观测性**：将 `session_id` 映射到图的 `thread_id`，先引入本地 checkpointer，再处理程序重启后的恢复、取消、超时和失败重试；Agent 面板展示真实节点进度与工具状态，而不是预置计划。LangGraph 的持久化机制可保存线程内状态，参见[持久化说明](https://docs.langchain.com/oss/python/langgraph/persistence)。
4. **最后抽取 Skills**：把“转写后摘要”“按字幕搜索并导出片段”等经过验证的组合流程做成可发现、可配置的本地 Skill；Skill 只编排现有工具，不直接操纵播放器内部对象。验收时覆盖无音轨、无字幕、切换媒体、工具失败、重复提交和导出后文件校验。

上述协议加固、显式节点、持久化与 Skills 仍是**后续计划**；基础 LangChain/LangGraph 集成已位于 `agent/runtime.py`。

## 🎯 功能演示

### 1. 基础播放

-   支持本地视频 / 网络视频地址加载，实现音视频同步播放
-   精准控制播放进度，支持 seek 精准定位
-   点击底部“截图”可保存点击时播放器显示的画面为 PNG，包含当前滤镜、贴纸和已显示的字幕；无视频时按钮不可用。

### 2. 录制与滤镜

-   录制当前媒体的解码音视频帧，输出 MP4 格式文件
-   实时切换灰度、反相、垂直翻转及动态贴纸

### 3. AI 助手与 Agent

-   对本地视频生成时间轴字幕，并在播放画面中同步显示
-   使用字幕生成摘要、搜索相关片段或生成剪辑建议；Agent 可将自然语言目标转换为工具调用

## 📝 开发计划

-   扩展 LangGraph 的状态持久化、任务取消与真实计划节点，保留现有 C++ 媒体工具和本地 JSON Lines 边界
-   新增更多滤镜特效（马赛克、复古、动态模糊）
-   提高 RTSP/RTMP 等网络媒体源的稳定性与断线恢复能力
-   优化硬件解码性能，适配更多编解码器
-   移动端（Android/iOS）跨平台适配

## 🤝 贡献

欢迎提交 Issue 或 PR！提交 PR 前请确保：

1.  代码符合 C++ 编码规范，注释清晰
2.  新增功能附带测试用例
3.  不破坏现有功能兼容性

## 📄 许可证

本项目基于 **MIT License** 开源，详见 [LICENSE](LICENSE) 文件。

## 📞 联系方式

-   作者：Qiao12-pixel
-   GitHub：[https://github.com/Qiao12-pixel](https://github.com/Qiao12-pixel)

----------




## AI 助手模型方式

AI 助手和 Agent 工作台支持“模型方式”切换：本地模型使用 Ollama/Qwen3 和 faster-whisper；豆包 API 使用方舟文本模型和豆包语音服务。两条路径的模型通信都由 Python Runtime 承担，C++ 只负责 FFmpeg 音轨提取、字幕保存、播放和导出等工具。**本地模式只连接本机 Ollama 服务；豆包模式会向服务端发送任务文本和工具结果，语音转写会上传音频，摘要/搜索/剪辑会上传字幕内容。**

**本地模型：**在 `resource/local-model.yaml` 配置模型名称和目录；运行 `bash scripts/download_local_models.sh` 可下载 Qwen3:4b 和 Whisper small 到 `resource/`。首次下载需要网络，完成后可离线推理。脚本在 Ollama Registry 不可达时，会从 Qwen 官方 GGUF 仓库下载并校验 SHA-256，再导入本地 Ollama。若两种下载方式都失败，程序会显示“本地模型待配置”，豆包模式仍可使用。

模型权重位于 `resource/ollama/` 与 `resource/whisper-small/`，已被 `.gitignore` 排除；`resource/local-model.yaml` 是可随仓库分发的配置。应用会检查 Qwen 模型是否出现在本地 Ollama 列表，以及 Whisper 快照是否同时包含 `config.json` 和 `model.bin`。缺失时只禁用对应能力并显示原因。

```yaml
provider: ollama
model: qwen3:4b
host: http://127.0.0.1:11435
models_dir: resource/ollama
whisper_model_path: resource/whisper-small
```

**豆包 API：**在项目的 `config/secret_key.yaml` 中填写 API 配置，可复制 `config/secret_key.example.yaml` 的字段结构：

```yaml
doubao_api_key: "你的火山方舟 API Key"
doubao_model: "doubao-seed-2-0-lite-260428"
doubao_speech_api_key: "你的豆包语音新版控制台 API Key"
# doubao_chat_endpoint: "https://ark.cn-beijing.volces.com/api/v3/chat/completions"
```

语音密钥需开通 `volc.bigasr.auc_turbo`。Python Runtime 从项目根目录的 `config/secret_key.yaml` 加载配置；修改后请重启播放器。缺失字段会在 AI 侧栏显示为配置未完成，不显示密钥值。该文件被 `.gitignore` 忽略，请勿提交真实密钥。

AI 助手和 Agent 工作台的模式选择会同步，且通过 Qt `QSettings` 记住上次选择。任务执行期间不能切换；如果此时尝试切换，界面会回到正在使用的模式。两种方式都由 Python 调用模型，C++ 仅通过 JSON Lines 接收工具请求并返回执行结果。

`doubao_api_key` 是火山方舟的模型调用密钥；`doubao_speech_api_key` 必须从[豆包语音控制台的 API Key 管理](https://console.volcengine.com/speech/new/setting/apikeys?projectName=default)获取，二者不能直接共用。请在同一语音项目下开通录音文件识别极速版。转写返回 `HTTP 401 / 45000010 Invalid X-Api-Key` 时，先核对语音密钥所属产品和项目。

选择豆包模式时，视频摘要、语义搜索与智能剪辑通过方舟 Chat API 调用文本模型；Seedream 属于图片生成模型，不能用于这些文本任务。播放器会自动加载视频旁的 `.ai.srt` 或 `.srt` 时间轴字幕并叠加显示；AI 侧栏可切换字幕显示。

本地媒体需能由 `ffmpeg` 读取，并且 `ffmpeg` 在 `PATH` 中。点击“字幕转写”后，软件会在视频旁保存同名 `.ai.srt` 文件。已有同名 `.ai.srt`、`.srt` 或 `.txt` 文件会自动加载；智能剪辑需要含时间戳的 `.srt`。生成剪辑建议后，点击“导出建议片段”并选择目录，各时间段会重新编码为独立的 MP4 文件。当前转写音频上限 20 MB，字幕分析上限 2 万字；网络流不支持直接转写和导出。API 密钥只从本地配置文件读取，不写入日志或其他项目文件。

### 常见问题

| 现象 | 检查项 |
| --- | --- |
| 本地文本按钮不可用 | 确认 `ollama` 可在 `PATH` 中运行，且 `qwen3:4b` 已导入 `resource/ollama/`；可重跑 `bash scripts/download_local_models.sh` |
| 本地字幕转写不可用 | 检查 `resource/whisper-small/` 的快照目录中是否同时有 `config.json` 和 `model.bin` |
| 豆包模式提示配置不完整 | 核对 `config/secret_key.yaml` 的文本模型与语音密钥字段，修改配置后重启应用 |
| 模式切换后自动回退 | 等待当前 AI/Agent 任务结束，再重新选择模型方式 |
| 本地 Agent 报 `properties must be an object` | 更新并重新构建客户端；工具参数必须是包含对象形式 `properties` 的 JSON schema，Python Runtime 会补齐空参数工具的 schema |
