# AVPlayer 启动说明

本文说明如何启动完整项目：Qt 桌面端、Web 客户端、Python Agent 服务、
Web 网关和 C++ 媒体服务。

相关文档：

- [README](README.md) — 项目概览与功能
- [ARCHITECTURE.md](ARCHITECTURE.md) — 分层、依赖边界与运行链路
- [deploy/README.md](deploy/README.md) — Docker Compose 与部署配置
- [services/agent/README.md](services/agent/README.md) — Agent 服务
- [services/media/README.md](services/media/README.md) — 媒体服务

---

## 1. 启动拓扑

```text
Qt AVPlayer  ──gRPC 50061──> services/agent
     │                              │
     │                              └──gRPC 50062──> services/media
     │                                                   │
     └──────────── WebRTC H.264 + Opus <─────────────────┘

浏览器 ──HTTPS 5173 / HTTP 8080──> Web (Vite / nginx)
     │
     ├──WS /signal  ──> web-gateway :50065 ──gRPC──> media_server :50062
     ├──WS /agent   ──> web-gateway :50065 ──gRPC──> agent :50061
     ├──POST /upload ─> web-gateway :50065 ──gRPC──> agent :50061
     └──POST /control, GET /download, /config ──> web-gateway
```

| 进程 | 默认地址 | 作用 |
| --- | --- | --- |
| `avplayer-agent` | `127.0.0.1:50061` | 上传、AI/Agent 编排、服务端工具 |
| `media_server` | `0.0.0.0:50062` | 解码/编码、WebRTC 推流、服务端录制 |
| `avplayer-web-gateway` | `127.0.0.1:50065` | WebSocket/HTTP ↔ gRPC 网关 |
| Vite Web | `https://localhost:5173` | 本地 Web 客户端 |
| nginx Web（Compose） | `http://localhost:8080` | 容器化 Web 客户端 |

Agent、Gateway、Web 只监听本机时，Qt 和浏览器都在同一台机器上即可。

---

## 2. 环境要求

- macOS；Qt 6（各模块版本一致）
- CMake 3.23+、Conan 2.x、C++17 编译器
- FFmpeg、包含 `face` 模块的 OpenCV
- Qt 6 Widgets / Multimedia / OpenGL
- libopus（macOS 由 Conan/Homebrew 提供；Linux 安装 `libopus-dev`）
- Python 3.10+、`ffmpeg` 命令
- Node.js 20+、npm

可选：

- 本地模型：Ollama + Qwen3 + faster-whisper
- 云端模型：豆包语音 API + 火山方舟文本模型

---

## 3. 一次性准备

### 3.1 构建 C++ 目标

```sh
cd /Users/joe/CLionProjects/AVPlayer

conan install . --build=missing -s compiler.cppstd=17
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug --target AVPlayer media_server -j8
```

构建产物：

- `cmake-build-debug/apps/desktop/AVPlayer`
- `cmake-build-debug/services/media/media_server`

可选构建烟雾客户端：

```sh
cmake --build cmake-build-debug --target streaming_smoke_client
```

### 3.2 安装 Agent 服务

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -e 'services/agent[dev]'
```

`scripts/dev.sh` 也会设置 `PYTHONPATH=services/agent`，但首次仍建议安装一次，
确保 `avplayer-agent`、`avplayer-web-gateway` 命令和依赖可用。

### 3.3 安装 Web 依赖

```sh
npm --prefix apps/web install
```

`scripts/dev.sh` 会在 `apps/web/node_modules` 不存在时自动执行 `npm install`。

### 3.4 本机 Redis / MySQL（可选）

两者都不是必需：未启动时 Agent 自动退化为纯磁盘，任务照常完成。

```sh
brew install redis mysql
brew services start redis
brew services start mysql

# 只做一次：建库与账号（密码与 dev.sh 的默认 DSN 一致）
mysql -u root -e "
CREATE DATABASE IF NOT EXISTS avplayer CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER IF NOT EXISTS 'avplayer'@'127.0.0.1' IDENTIFIED BY 'avplayer';
GRANT ALL PRIVILEGES ON avplayer.* TO 'avplayer'@'127.0.0.1';
FLUSH PRIVILEGES;"
```

MySQL 8.4+ 默认 `caching_sha2_password`，PyMySQL 需要 `cryptography`
（已列在 `services/agent/pyproject.toml`）。表结构由 Agent 首次连接时自动创建。

停止：`brew services stop mysql`（数据保留在 `/opt/homebrew/var/mysql`）。

### 3.5 准备模型配置（可选）

本地模式：

```sh
bash scripts/download_local_models.sh
```

配置文件：`resource/local-model.yaml`。

云端模式：

```sh
cp config/secret_key.example.yaml config/secret_key.yaml
# 编辑 doubao_api_key / doubao_model / doubao_speech_api_key
```

模型方式由 Agent 读取 `AVPLAYER_AI_PROVIDER`（默认 `local`），也可在
Qt/Web 的 AI 面板中切换当前会话的方式。

---

## 4. 改动 → 构建 → 按顺序启动

日常开发按这个顺序走：先改 C++/Python/Web 代码，再构建，再按依赖顺序启动
各进程，最后手动验证。

### 4.1 改动后需要重建什么

| 改动位置 | 需要做的事 |
| --- | --- |
| `libs/media-core/`、`services/media/` | 重建 `media_server`，重启媒体服务 |
| `apps/desktop/` | 重建 `AVPlayer`，重启 Qt 客户端 |
| `proto/*.proto` | 重新生成 stub，重建 `AVPlayer` 和 `media_server`，重启 agent/gateway |
| `services/agent/`（Python） | 不需要构建，重启 `avplayer-agent` 与 `avplayer-web-gateway` |
| `apps/web/` | Vite 热更新即可；出包前跑 `npm --prefix apps/web run build` |

### 4.2 构建

```sh
cd /Users/joe/CLionProjects/AVPlayer

# C++：桌面端 + 媒体服务 + 冒烟客户端
cmake --build cmake-build-debug --target AVPlayer media_server streaming_smoke_client -j8

# Python：单元测试（纯逻辑，不需要服务在跑）
PYTHONPATH=services/agent .venv/bin/python -m unittest discover -s services/agent/tests

# Web：类型检查与打包
npm --prefix apps/web run build
```

### 4.3 按顺序启动

依赖顺序是 **agent → gateway → media → web**，前一个端口就绪后再起下一个：

| 步骤 | 进程 | 就绪检查 |
| --- | --- | --- |
| 1 | `avplayer-agent` | `lsof -ti :50061` |
| 2 | `avplayer-web-gateway` | `curl http://127.0.0.1:50065/health` |
| 3 | `media_server` | `lsof -ti :50062` |
| 4 | Vite Web | `https://localhost:5173` 可打开 |

一条命令启动全部（`scripts/dev.sh` 内部就是这个顺序，但**不负责构建**）：

```sh
scripts/dev.sh
```

它会启动：

1. `avplayer-agent` → `127.0.0.1:50061`
2. `avplayer-web-gateway` → `127.0.0.1:50065`
3. `media_server` → `0.0.0.0:50062`
4. Vite Web 开发服务器 → `https://localhost:5173`

需要逐个看日志、或想自己控制顺序时，按第 6 节开多个终端分别启动。

注意：

- 如果 media 显示 skipped，先执行
  `cmake --build cmake-build-debug --target media_server`。
- 首次打开 Vite HTTPS 地址时，浏览器会提示自签名证书，需要手动信任。
- 各进程日志会混合在同一个终端；需要单独排查时用第 6 节的手动启动。

### 4.4 启动 Qt 客户端

```sh
cd /Users/joe/CLionProjects/AVPlayer

# 本地解码播放（默认）
./run_avplayer.sh

# 或：服务端拉流模式
AVPLAYER_STREAMING_MODE=1 ./run_avplayer.sh
```

Qt 默认连接：

- `AVPLAYER_AGENT_SERVER=localhost:50061`
- `AVPLAYER_MEDIA_SERVER=localhost:50062`

连接远程服务时：

```sh
AVPLAYER_AGENT_SERVER=cloud-host:50061 \
AVPLAYER_MEDIA_SERVER=cloud-host:50062 \
AVPLAYER_STREAMING_MODE=1 \
./run_avplayer.sh
```

### 4.5 打开 Web 客户端

浏览器访问 Vite 打印的 HTTPS 地址（通常是 `https://localhost:5173`）：

1. 进入“网络”页，选择视频，点击“上传到服务端”；
2. 上传完成后 `media_id` 自动填入；
3. 点击“连接并播放”；
4. 浏览器可能因自动播放策略暂停，点击一次“播放”；
5. 进入“AI 助手”或“Agent”页使用服务端模型能力。

---

## 5. 只启动部分组件

```sh
scripts/dev.sh --no-web     # 只启动 agent + gateway + media
scripts/dev.sh --no-media   # 不启动 media_server
```

只使用 Qt 本地播放时，也可以只启动 Agent 服务：

```sh
.venv/bin/avplayer-agent
```

---

## 6. 手动启动各组件（按依赖顺序）

需要独立查看日志或自定义端口时，按 agent → gateway → media → web 的顺序
分别开终端。

### Agent 服务

```sh
export AVPLAYER_PROJECT_ROOT="$PWD"
export AVPLAYER_STORAGE_ROOT="$PWD/storage"
export AVPLAYER_AGENT_BIND=127.0.0.1
export AVPLAYER_AGENT_PORT=50061
.venv/bin/avplayer-agent
```

### Web 网关

```sh
export AVPLAYER_AGENT_SERVER=localhost:50061
export AVPLAYER_MEDIA_SERVER=localhost:50062
export AVPLAYER_WEB_GATEWAY_HOST=127.0.0.1
export AVPLAYER_WEB_GATEWAY_PORT=50065
.venv/bin/avplayer-web-gateway
```

### 媒体服务

```sh
export AVPLAYER_MEDIA_SERVER_ADDRESS=0.0.0.0:50062
export AVPLAYER_STORAGE_ROOT="$PWD/storage"
export AVPLAYER_ICE_SERVERS=stun:stun.l.google.com:19302
./cmake-build-debug/services/media/media_server
```

### Web 客户端

```sh
npm --prefix apps/web run dev
```

Vite 会代理 `/signal`、`/control`、`/upload`、`/download`、`/agent`、
`/config` 到 `http://127.0.0.1:50065`。可通过
`AVPLAYER_WEB_GATEWAY_HTTP` 覆盖目标。

---

## 7. Docker Compose

```sh
cp deploy/.env.example deploy/.env
docker compose -f deploy/docker-compose.yml up --build
```

打开 <http://localhost:8080>。

Compose 启动 `agent`、`media`、`web-gateway`、`web` 四个服务，端口和存储卷在
`deploy/.env` 中配置。

已知限制：

- 默认 bridge 网络下，浏览器无法直接访问 media 容器的 ICE host candidate；
  本地串流建议用 `scripts/dev.sh`。Linux 可考虑给 `media` 配 host 网络，
  跨网络部署则需要可达的 TURN。
- `media` 镜像为构建型镜像，保留 C++/Qt 工具链。
- 需要 Docker Desktop/daemon 正在运行。

---

## 8. 配置项

| 变量 | 使用方 | 默认值 |
| --- | --- | --- |
| `AVPLAYER_PROJECT_ROOT` | agent、desktop、media | 当前目录/源码根 |
| `AVPLAYER_STORAGE_ROOT` | agent、media | `<project>/storage` |
| `AVPLAYER_AGENT_BIND` | agent | `[::]`（dev.sh 为 `127.0.0.1`） |
| `AVPLAYER_AGENT_PORT` | agent | `50061` |
| `AVPLAYER_AGENT_SERVER` | gateway、desktop | `localhost:50061` |
| `AVPLAYER_MEDIA_SERVER` | gateway、desktop | `localhost:50062` |
| `AVPLAYER_MEDIA_SERVER_ADDRESS` | media | `0.0.0.0:50062` |
| `AVPLAYER_WEB_GATEWAY_HOST` | gateway | `127.0.0.1` |
| `AVPLAYER_WEB_GATEWAY_PORT` | gateway | `50065` |
| `AVPLAYER_ICE_SERVERS` | media、gateway | Google STUN |
| `AVPLAYER_AI_PROVIDER` | agent | `local` |
| `AVPLAYER_OLLAMA_HOST` | agent | `local-model.yaml` 中的值 |
| `AVPLAYER_STREAMING_MODE` | desktop | `0`（本地播放） |
| `AVPLAYER_REDIS_URL` | agent | `redis://127.0.0.1:6379/0`；置空则禁用缓存 |
| `AVPLAYER_REDIS_TTL` | agent | `3600`（字幕缓存秒数） |
| `AVPLAYER_MYSQL_DSN` | agent | 空（禁用）；Compose 下为 `mysql://avplayer:avplayer@mysql:3306/avplayer` |

数据分层（都可关闭，关闭后回落到磁盘）：

| 层 | 内容 | 说明 |
| --- | --- | --- |
| `storage/` | 原始视频、字幕、剪辑、录制 | 唯一事实来源，`media_server` 直接读 |
| Redis | 字幕 JSON 缓存（`avplayer:transcript:<session>:<media>`） | 加速摘要/搜索/剪辑读取，TTL 可过期 |
| MySQL | `sessions`/`media`/`transcripts`/`segments`/`task_runs` | 跨实例持久化与历史查询；表结构首次连接自动创建 |

存储布局：

```text
storage/<session_id>/<media_id>/
├── original.<ext>
├── transcript.srt / transcript.json
├── edit_plan.json
├── clips/
└── recording-<millis>.mp4
```

---

## 9. 验证

### 9.1 自动检查

```sh
# 网关健康检查
curl http://127.0.0.1:50065/health
# 期望 {"status": "ok"}

# ICE / 模型方式配置
curl http://127.0.0.1:50065/config
# 期望包含 ice_servers 和 provider

# Python 单元测试
PYTHONPATH=services/agent .venv/bin/python -m unittest discover -s services/agent/tests -v

# gRPC 冒烟测试
PYTHONPATH=services/agent .venv/bin/python tests/grpc_smoke_client.py
```

上传一个媒体（网关要求原始文件 body，不是 multipart）：

```sh
curl -s -X POST http://127.0.0.1:50065/upload \
  -H "X-Session-Id: web" \
  -H "X-File-Name: input.mp4" \
  -H "Content-Type: application/octet-stream" \
  --data-binary @resource/input.mp4
# 期望 {"media_id": "...", "size_bytes": ...}
```

媒体服务冒烟测试（会验证 H.264 + Opus、播放控制和服务端录制）：

```sh
./cmake-build-debug/services/media/streaming_smoke_client \
  localhost:50062 web <media_id>
```

期望输出包含 `trackOpen=1`、非零的 `bytesReceived`/`audioBytesReceived`、
`success=1` 的 `status/pause/play/record_start/record_stop`，以及一条
`recording_path=...`。

### 9.2 手动验证清单

服务起来后，按下面顺序人工走一遍：

**Web 端（`https://localhost:5173`）**

1. 网络页选择 `resource/input.mp4` → “上传到服务端”，`media_id` 自动填入；
2. “连接并播放”，画面出现（约 1280×720），进度条开始走；
3. 点击暂停/播放、拖动进度条，画面与进度同步；
4. 开始录制，几秒后停止，服务端生成 `recording-*.mp4`；
5. AI 页转写，出现字幕；再试摘要、搜索、智能剪辑；
6. Agent 页输入目标，看到计划与工具执行日志。

**Qt 端**

1. `./run_avplayer.sh` 打开本地视频，播放/暂停/进度正常；
2. `AVPLAYER_STREAMING_MODE=1 ./run_avplayer.sh` 连接服务端拉流，画面与声音都有；
3. 打开 AI 侧栏做一次转写，字幕回到桌面端。

**服务端产物**

```sh
ls storage/web/<media_id>/
# original.mp4、transcript.srt/json、edit_plan.json、clips/、recording-*.mp4
```

---

## 10. 停止与清理

- 在 `scripts/dev.sh` 终端按 `Ctrl-C`，脚本会清理自己启动的进程。
- 如果确认有残留进程，可按端口停止：

```sh
lsof -ti :50061 | xargs kill
lsof -ti :50062 | xargs kill
lsof -ti :50065 | xargs kill
lsof -ti :5173 | xargs kill
```

- 会话产物在 `storage/`，录制临时文件在 `cache/recordings/`。
- 清空测试数据前请确认目录：

```sh
# 谨慎操作：会删除所有上传媒体、字幕、剪辑和录制
rm -rf storage/*
```

---

## 11. 常见问题

| 现象 | 排查 |
| --- | --- |
| `media skipped` | 先构建 `media_server`，并确认路径 `cmake-build-debug/services/media/media_server` |
| `启动中止：端口已被占用` | `scripts/dev.sh` 启动前预检发现有旧进程，按提示 `lsof -ti :<port> \| xargs kill` 释放后重试 |
| 端口占用 | 用第 10 节命令检查 50061/50062/50065/5173 |
| Web 页面打不开 | 确认 Vite 已启动，并信任自签名 HTTPS 证书 |
| Web 有画面无声音 | 点击一次“播放”解除自动播放限制；确认服务端链接了 Opus |
| Web 黑屏、无视频 | 确认 `AVPLAYER_MEDIA_SERVER` 指向运行中的 media，且 50062 可达 |
| AI/Agent 显示“待配置” | 检查 `resource/local-model.yaml`、Whisper 模型或 `config/secret_key.yaml` |
| AI 请求报“已有任务执行中” | 等待当前任务结束，或切换页签重新提交 |
| 拉流模式没有进度 | 确认使用当前版本的服务端；`ControlAck` 会返回 duration/position |
| Docker 下 WebRTC 失败 | 用 `scripts/dev.sh` 本地串流，或配置 host 网络/TURN |
| 贴纸滤镜在 Web 不可用 | Web 端暂未实现人脸贴纸，Qt 端可用 |

---

## 12. 首次使用流程

1. 准备 C++、Python、Web 依赖；
2. 准备本地或云端模型配置；
3. 终端 A：`scripts/dev.sh`；
4. 终端 B：`./run_avplayer.sh`（或 `AVPLAYER_STREAMING_MODE=1`）；
5. 浏览器打开 Vite HTTPS 地址；
6. Web 上传视频 → 连接 → 播放；
7. AI 页转写字幕 → 摘要/搜索/智能剪辑；
8. Agent 页输入自然语言目标。
