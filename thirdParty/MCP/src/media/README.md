# MCP 独立原生媒体工具

此模块在 MCP 仓库中独立实现，不依赖 AVPlayer 源码、proto、gRPC、网关、媒体服务或 Worker。
与 AVPlayer 保持相同工具参数和产物布局，便于同一 Agent 切换到此实现。

## 代码

- media_backend：查询原文件、JSON/SRT字幕读写、计划保存、本地任务队列与状态管理。
- executor：原生 C++ 媒体执行器，通过 posix_spawnp 调用 ffmpeg/ffprobe；不使用 shell。
- inference/speech.py：独立 Whisper/云端语音模型后端，仅输出推理segments。
- ../main/mcp_avplayer_tools：注册10个媒体工具和2个任务查询/取消工具，直接调用本模块。

## 环境

- AVPLAYER_STORAGE_ROOT：媒体根目录，布局 session/media/original.*，需与上传方共享。
- MCP_MEDIA_ROOT：MCP 仓库根目录，定位 src/media/inference/speech.py。
- AVPLAYER_PROJECT_ROOT：模型/凭据配置根目录，包含 resource/local-model.yaml 和 config/secret_key.yaml；独立部署可自备配置，不要求是AVPlayer仓库。
- AVPLAYER_WORKER_PYTHON：模型推理解释器，需 faster-whisper、PyYAML（仅转写使用）。
- MCP_MEDIA_EXECUTOR：原生执行器路径，默认与 MCP_Server 同目录的 mcp_media_executor。
- MCP_MEDIA_SLOTS：并行任务数，默认2，范围1..32；等待队列上限16。

MCP_MEDIA_EXECUTOR 与 MCP_MEDIA_ROOT 可显式配置，媒体工具不读取 AVPLAYER_DATA_SERVER。
工具通过现有 /jsonrpc 暴露，不是新版Streamable HTTP。

## 功能与边界

字幕存储为本地 JSON/SRT，剪辑计划为JSON；此独立实现不复用AVPlayer的Redis/MySQL缓存，因此读取的是共享文件最新结果。
耗时任务返回job_id并在本地队列执行，Get/Cancel按session隔离。产物名含job_id，存储在原媒体或clips目录。
任务状态在进程内，最多保留4096条，达到上限后拒绝；没有任务记录自动淘汰、持久恢复、ZooKeeper分配或跨MCP实例共享路由。
重启后已存在执行记录的job_id拒绝重提，避免不明确重复执行。超时/取消终止执行进程组。
MCP仍不是多租户鉴权入口，session/media参数须由可信调用方绑定。

## 构建与独立验证

```sh
cmake -S . -B cmake-build-debug -DMCP_ENABLE_AVPLAYER=ON
cmake --build cmake-build-debug --target MCP_Server -j4
python3 tests/media_standalone.py
```

测试只启动MCP并生成临时素材，不启动任何AVPlayer C++进程；覆盖工具发现、文件工具、真实FFmpeg、任务ID、取消和会话隔离。
实际Agent转写验证位于AVPlayer/tests/mcp_independent_agent.py（只启动Agent+MCP）。
