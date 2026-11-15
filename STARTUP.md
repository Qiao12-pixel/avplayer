> 当前启动单元：统一 C++ 后端 avplayer_server、Python Agent、thirdParty/MCP 和 Web。ZooKeeper代码位于services/server/src/discovery/zookeeper，媒体只是内部库，不单独启动。

# AVPlayer：从 C++ 构建到项目启动

本文以当前 macOS / Apple Silicon 开发环境为准。推荐先启动 **Web + 后端 + MCP + Agent**，Qt 客户端按需启动。
所有命令均在项目根目录执行，另有注明的除外。

```sh
cd /Users/joe/CLionProjects/AVPlayer
```

## 1. 先明确需要构建和启动的程序

| 程序 | 源码 | 构建产物 | 作用 |
|---|---|---|---|
| 统一 C++ 后端 | services/server（入口）＋services/media（内部库） | cmake-build-debug/services/server/avplayer_server | 网络接入、数据管理、媒体编解码、WebRTC与录制 |
| C++ MCP | thirdParty/MCP | build/mcp/MCP_Server | Agent 媒体工具与任务管理 |
| MCP 工具执行器 | thirdParty/MCP/src/media | build/mcp/mcp_media_executor | MCP 自动启动，执行 FFmpeg 媒体任务 |
| Python Agent | services/agent | .venv/bin/python -m avplayer_agent | 模型调用和任务编排 |
| Web | apps/web | 开发时 Vite；生产产物 apps/web/dist | 浏览器界面 |
| Qt（可选） | apps/desktop | cmake-build-debug/apps/desktop/AVPlayer | 桌面播放器 |

MCP 是普通源码目录，随主仓库一起检出，不需要 git submodule 操作。
Agent 的媒体工具经 MCP 执行，不需要启动旧版 task_worker，项目已删除该实现。
实时播放由统一 avplayer_server 的媒体模块提供。MCP和统一后端必须访问同一份上传文件。

## 2. 首次准备环境

### 2.1 工具链和系统依赖

需要 Xcode Command Line Tools、CMake 3.24+、C++17 编译器、Conan 2、Python 3.12 和 Node.js 20+。
当前工程使用 Homebrew `/opt/homebrew` 路径及 Conan 包；Linux 部署说明另见 deploy/README.md。

```sh
xcode-select -p
cmake --version
python3 --version
node --version
npm --version
conan --version
```

Homebrew 依赖分工：

| 依赖 | 用途 |
|---|---|
| grpc、protobuf | C++ 服务接口及代码生成 |
| boost、nlohmann-json | Asio/Beast 网络服务及 JSON |
| hiredis、mysql | C++ Redis/MySQL 客户端 |
| zookeeper | C 客户端库；运行 ZooKeeper 服务为可选项 |
| qtbase、qtmultimedia、qtdeclarative | 媒体核心及可选 Qt UI |
| opencv、glew、glfw、opus | 根工程依赖及媒体能力 |
| ffmpeg | MCP 执行器使用的 ffmpeg / ffprobe 命令 |

可按缺失项安装；示例：

```sh
brew install cmake ninja grpc protobuf boost nlohmann-json hiredis mysql zookeeper \
  qtbase qtmultimedia qtdeclarative opencv glew glfw opus ffmpeg
```

FFmpeg **C++ 链接库**由根目录 conanfile.txt 提供，与命令行 ffmpeg 的来源可不同。
当前根 CMake 显式使用 `/opt/homebrew/opt/openssl@4`：确认这个目录存在，或按本机 OpenSSL 路径调整 CMake；不要只创建一个错误的软链接。
Qt 各模块须处于相同 major.minor 版本。
即使本次不构建 Qt 客户端，根 CMake 仍会发现 Qt Widgets/OpenGL/OpenCV 等依赖，这是当前构建入口的限制。

### 2.2 Python 环境

```sh
python3 -m venv .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install -e 'services/agent[dev]'
```

Agent 使用 gRPC、LangChain/LangGraph；MCP 语音推理使用此环境中的 faster-whisper/PyYAML。
Conan 未安装时可用单独工具环境安装，避免污染业务依赖：

```sh
python3 -m venv .tools/conan
.tools/conan/bin/python -m pip install 'conan>=2,<3'
export PATH="$PWD/.tools/conan/bin:$PATH"
conan profile detect --exist-ok
```

`.tools/` 仅存本地工具，已排除版本控制。

## 3. 构建 C++ 主工程

### 3.1 安装 Conan 依赖

```sh
conan install . -of cmake-build-debug -s build_type=Debug \
  -s compiler.cppstd=17 --build=missing
```

当前使用 Debug，生成配置应位于：

```text
cmake-build-debug/build/Debug/generators/
```

其中应包含 ffmpeg-config.cmake、stb-config.cmake、glm-config.cmake 等。

### 3.2 准备 libdatachannel

当前根 CMake 使用预先存在的源码目录，不会自动克隆。新环境首次执行：

```sh
git clone --branch v0.23.1 --depth 1 --recurse-submodules \
  https://github.com/paullouisageneau/libdatachannel.git \
  cmake-build-debug/_deps/libdatachannel-src
```

已有该目录时跳过。若子模块缺失：

```sh
git -C cmake-build-debug/_deps/libdatachannel-src submodule update --init --recursive
```

### 3.3 配置 CMake

```sh
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$PWD/cmake-build-debug/build/Debug/generators;/opt/homebrew"
```

已有可用构建目录可直接重配，不要切换该目录原有的生成器。
如需换成 Ninja，用新的构建目录，并同步设置 AVPLAYER_BUILD_DIR；新目录也要准备 libdatachannel 源码。

### 3.4 编译后端，不编译 Qt 可执行文件

```sh
cmake --build cmake-build-debug \
  --target avplayer_server streaming_smoke_client -j4
```

`streaming_smoke_client` 是验证 WebRTC 接收的命令行程序，不是 Qt 客户端。
确认结果：

```sh
test -x cmake-build-debug/services/server/avplayer_server
test -x cmake-build-debug/services/server/avplayer_server
test -x cmake-build-debug/services/media/streaming_smoke_client
```

可选编译 Qt：

```sh
cmake --build cmake-build-debug --target AVPlayer -j4
```

C++ Protobuf/gRPC 代码由 CMake 自动生成。修改 proto 后，还需更新 Python 代码：

```sh
PYTHON=.venv/bin/python bash scripts/gen_proto.sh
```

## 4. 独立构建 thirdParty/MCP

首次安装 MCP 的 Conan 依赖并构建：

```sh
scripts/build_mcp.sh --install-deps
```

如果 Conan 在自定义路径，可显式指定：

```sh
CONAN="$PWD/.tools/conan/bin/conan" scripts/build_mcp.sh --install-deps
```

以后修改 MCP 源码只需：

```sh
scripts/build_mcp.sh
```

这个脚本配置 thirdParty/MCP，并将产物放到 build/mcp；不会使用原仓库外的 `/Users/joe/CLionProjects/MCP`。

```sh
test -x build/mcp/MCP_Server
test -x build/mcp/mcp_media_executor
```

两个程序保持在同一目录；MCP 会自动启动工具执行器，无需手动运行它。
本机已有 Conan 缓存不代表干净机器已安装依赖：如果提示缺少 spdlog、httplib、GTest 等包，先执行带 --install-deps 的命令。

## 5. 安装 Web 依赖和生成前端产物

```sh
npm --prefix apps/web ci
npm --prefix apps/web run build
```

开发启动脚本使用 Vite，不要求提前生成 dist，但建议启动前跑一次 build 检查 TypeScript。
没有 package-lock.json 或有意更新依赖时使用 npm install。

## 6. 准备运行配置和模型

### 6.1 MCP 配置

当前本机使用原样复制的：

```text
thirdParty/MCP/config/server.json
```

此目录被 `thirdParty/MCP/.gitignore` 忽略，**新克隆仓库不会自动获得本地配置**。
请从自己的配置备份恢复，或指定外部配置文件：

```sh
export AVPLAYER_MCP_CONFIG=/absolute/path/to/server.json
```

dev.sh 会通过 --config 读取它，通过命令行指定绑定地址127.0.0.1和端口8081，不修改配置内容。
配置内相对日志/数据库路径按 MCP 的工作目录 `cache/mcp` 解析。
MCP 图片工具的 API key 与 Agent 文本/语音配置不是同一个配置来源。

### 6.2 本地 AI 模型

先安装 Ollama，然后下载模型：

```sh
bash scripts/download_local_models.sh
```

默认模型配置位于 resource/local-model.yaml：

- 文本：Ollama qwen3:4b，地址 http://127.0.0.1:11435。
- 语音：resource/whisper-small 中的 faster-whisper 模型。

模型文件不提交 Git。只有播放需求时可不准备模型；AI 功能会报告模型未就绪。
本地模型服务可由 Agent 按需启动；排障时也可独立运行：

```sh
OLLAMA_HOST=127.0.0.1:11435 OLLAMA_MODELS="$PWD/resource/ollama" ollama serve
```

### 6.3 云端模型（可选）

```sh
cp config/secret_key.example.yaml config/secret_key.yaml
```

填写文本模型及语音凭据，启动时设置 AVPLAYER_AI_PROVIDER=cloud。
本机完整链路验证使用 local，不代表云端 API key 已验证。

### 6.4 Redis / MySQL

Redis/MySQL 用于 C++ 数据服务；MCP独立工具保存文件；Agent生成转写/摘要后经C++后端同步MySQL和Redis，同媒体请求可复用结果。
运行本机数据库，例如在单独终端运行 Redis、通过自己的数据库管理方式启动 MySQL。
MySQL 须预先创建数据库和用户；网关连接成功后自动创建业务表。
可在 MySQL 管理终端执行以下开发环境示例（已有库/用户时按实际权限调整）：

```sql
CREATE DATABASE IF NOT EXISTS avplayer CHARACTER SET utf8mb4;
CREATE USER IF NOT EXISTS 'avplayer'@'127.0.0.1' IDENTIFIED BY 'avplayer';
GRANT ALL PRIVILEGES ON avplayer.* TO 'avplayer'@'127.0.0.1';
```

默认脚本读取：

```text
AVPLAYER_REDIS_URL=redis://127.0.0.1:6379/0
AVPLAYER_MYSQL_DSN=mysql://avplayer:avplayer@127.0.0.1:3306/avplayer
```

账户/密码不同可通过环境变量覆盖。示例密码仅用于本机开发。
网关启动时尝试连接；如果数据库当时未就绪，会进入文件流程降级状态，数据库恢复后需重启网关。
播放/工具不依赖数据库才能运行，但验证真实数据库的集成测试要求数据库可用。

## 7. 一条命令启动项目

先确认构建完成、MCP 配置存在、所需端口空闲，然后运行：

```sh
AVPLAYER_AI_PROVIDER=local scripts/dev.sh
```

脚本启动以下程序；表中50065/50066/50062均属于同一个C++进程，不会自动编译C++：

| 进程 | 默认监听 |
|---|---|
| MCP_Server | 127.0.0.1:8081，/jsonrpc |
| Python Agent | 127.0.0.1:50061，gRPC |
| avplayer_server HTTP | 127.0.0.1:50065，HTTP/WebSocket |
| 网关内部接口 | 127.0.0.1:50066，上传/数据/媒体会话路由 gRPC |
| 同进程媒体模块 | 0.0.0.0:50062，内部信令/控制及 WebRTC |
| Vite | https://localhost:5173 |

打开 Vite 打印的地址。开发证书可能需要在浏览器中手动信任。
如果仅在 localhost 开发、不想使用自签名 HTTPS：

```sh
AVPLAYER_WEB_HTTP=1 AVPLAYER_AI_PROVIDER=local scripts/dev.sh
```

浏览器使用 http://localhost:5173。这个选项适用于本机开发；远程 WebRTC 页面仍需安全上下文。

### 启动部分组件

```sh
scripts/dev.sh --no-web       # 不启动 Vite
scripts/dev.sh --no-mcp       # MCP 已在外部运行
```

外部 MCP 配置 AVPLAYER_MCP_URL；不再单独启动媒体服务。
脚本会检查端口冲突，但不逐个等待所有服务就绪，也不做长期子进程健康监控。
统一后端未构建时脚本会直接报错，不会再出现网关启动但遗漏媒体进程的情况。

### 停止

在 dev.sh 所在终端按 Ctrl+C。脚本清理自己启动的服务；不要用全局 pkill 清理其他任务的服务。
独立启动的 Redis、MySQL、Ollama 或外部 MCP 按它们自己的方式停止。

## 8. 页面上的正常使用流程

1. 打开 Web“网络”页，选择视频并上传，等待生成 media_id。
2. 点击连接并播放。音视频由 C++ 媒体服务通过 WebRTC 发送。
3. 验证暂停、播放和进度跳转；开启录制后停止，下载录制文件。
4. AI 页选择模型方式，先转写字幕，再执行摘要、字幕搜索和智能剪辑。
5. 导出剪辑建议并下载产物。
6. Agent 工作台输入自然语言目标，如“将当前视频0到1000毫秒导出为片段”，查看工具状态和结果。

工具路径：Python Agent→MCP→本地原生工具/模型推理。
播放路径：Web→avplayer_server网络/媒体模块→WebRTC→Web。
上传方和 MCP 必须使用相同 AVPLAYER_STORAGE_ROOT，否则会出现“文件已上传但工具找不到媒体”。

## 9. 分终端手动启动（排障用）

先在每个终端进入项目根目录，并设置以下相同路径：

```sh
export AVPLAYER_PROJECT_ROOT="$PWD"
export AVPLAYER_STORAGE_ROOT="$PWD/storage"
export PYTHONPATH="$PWD/services/agent"
export MCP_MEDIA_ROOT="$PWD/thirdParty/MCP"
export AVPLAYER_WORKER_PYTHON="$PWD/.venv/bin/python"
```

终端 A：MCP。

```sh
./build/mcp/MCP_Server --config "$PWD/thirdParty/MCP/config/server.json" \
  --host 127.0.0.1 --port 8081
```

终端 B：Python Agent。

```sh
AVPLAYER_DATA_SERVER=localhost:50066 \
AVPLAYER_MCP_URL=http://127.0.0.1:8081/jsonrpc \
AVPLAYER_AI_PROVIDER=local .venv/bin/python -m avplayer_agent
```

终端 C：统一 C++ 后端（包括网关和媒体能力）。

```sh
AVPLAYER_AGENT_SERVER=localhost:50061 \
AVPLAYER_MEDIA_SERVER_ADDRESS=127.0.0.1:50062 \
AVPLAYER_DATA_SERVER=localhost:50066 \
AVPLAYER_REDIS_URL=redis://127.0.0.1:6379/0 \
AVPLAYER_MYSQL_DSN='mysql://avplayer:avplayer@127.0.0.1:3306/avplayer' \
./cmake-build-debug/services/server/avplayer_server
```

终端 D：Web。

```sh
AVPLAYER_WEB_HTTP=1 npm --prefix apps/web run dev
```

这些程序启动顺序不是严格依赖链，但应等端口就绪后再上传和发任务。

## 10. 启动后检查与自动验证

基础检查：

```sh
curl -fsS http://127.0.0.1:50065/health
curl -fsS http://127.0.0.1:50065/config
curl -fsS http://127.0.0.1:8081/health
lsof -nP -iTCP:50061 -sTCP:LISTEN
lsof -nP -iTCP:50062 -sTCP:LISTEN
```

MCP 若未提供 /health，用协议握手检查：

```sh
curl -fsS http://127.0.0.1:8081/jsonrpc \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"startup-check","version":"1"}}}'
```

不依赖已运行开发服务的回归检查：

```sh
PYTHONPATH=services/agent .venv/bin/python -m unittest discover -s services/agent/tests -v
cmake --build build/mcp -j4
ctest --test-dir build/mcp --output-on-failure
.venv/bin/python thirdParty/MCP/tests/media_standalone.py
.venv/bin/python tests/mcp_independent_agent.py
```

后两项会启动自己的隔离 MCP/Agent 进程；转写测试需要本地模型及 resource/input.mp4。
真实数据库、WebRTC 网关集成检查：

```sh
.venv/bin/python tests/cpp_gateway_integration.py
```

该测试使用隔离端口，但会写入本地数据库及 storage/cpp-migration。

### 完整非 Qt 流程

保持第一个终端运行：

```sh
AVPLAYER_WEB_HTTP=1 AVPLAYER_AI_PROVIDER=local scripts/dev.sh
```

另一个终端：

```sh
.venv/bin/python tests/full_stack_no_qt.py \
  --report /tmp/avplayer-full-stack-report.json
```

覆盖上传、同源 HTTP/WS、真实 WebRTC 接收、播放控制、录制、字幕转写、摘要、搜索、智能剪辑、转码和 Agent 工具导出。
接收端是 C++ 测量客户端，不代表浏览器实际渲染/音频输出、截图和滤镜已验证。
本轮验证记录见 VALIDATION_NO_QT.md。

## 11. 可选：ZooKeeper 与媒体服务负载均衡

编译始终需要 ZooKeeper C 客户端库，但单实例运行不要求 ZooKeeper 服务。
启用时设置 AVPLAYER_ZK_HOSTS，并确保每个媒体实例的 AVPLAYER_MEDIA_ADVERTISE 是网关可达的不同地址。

```sh
AVPLAYER_ZK_HOSTS=127.0.0.1:2181 scripts/dev.sh
```

脚本不会自动启动 ZooKeeper。双实例启动、故障摘除和恢复测试见 [deploy/ZOOKEEPER.md](deploy/ZOOKEEPER.md)。
当前均衡的是媒体播放会话，不是 MCP 本地任务队列；不提供跨网关共享路由或播放无缝迁移。

## 12. 可选：Qt 客户端

```sh
cmake --build cmake-build-debug --target AVPlayer -j4
./run_avplayer.sh
AVPLAYER_STREAMING_MODE=1 ./run_avplayer.sh
```

Qt 远程播放默认经网关50066。注意当前 run_avplayer.sh 中 Qt 路径仍写为6.10.2；本机安装版本不符时应先修正脚本或使用构建产物直接启动，不要将旧路径问题误认为后端失败。

## 13. 常见问题

| 现象 | 检查与处理 |
|---|---|
| ffmpeg CMake 包找不到 | 执行Conan安装；CMAKE_PREFIX_PATH指向本次生成的Debug/generators |
| libdatachannel目录不存在 | 按3.2节克隆指定版本并初始化Git子模块 |
| OpenSSL头文件找不到 | 检查根CMake硬编码的openssl@4路径是否对应本机安装 |
| Qt链接/ABI错误 | 对齐qtbase、qtmultimedia、qtdeclarative的major.minor |
| MCP缺少依赖包 | scripts/build_mcp.sh --install-deps，确认Conan可执行路径 |
| MCP配置不存在 | 恢复本地thirdParty/MCP/config/server.json，或设置AVPLAYER_MCP_CONFIG绝对路径 |
| MCP编译成功但任务无法启动 | MCP_Server和mcp_media_executor放在同一目录，检查执行权限 |
| 端口被占用 | 用lsof确认占用进程，在其终端正常停止，再启动；不要重复运行dev.sh |
| 上传成功但MCP找不到文件 | 比较网关与MCP的storage根、session_id和media_id |
| AI服务连接但工具失败 | 检查8081/jsonrpc握手、tools/list和MCP日志；不应启动已删除的AVPlayer Worker |
| 转写模型不可用 | 检查模型目录中config.json/model.bin及推理Python依赖 |
| 摘要/搜索不可用 | 先转写；检查Ollama模型列表及11435端口 |
| 数据库恢复但仍未写入 | 当前网关没有自动重连保证，数据库就绪后重启网关 |
| Web无法连接WebSocket | 对齐Vite代理目标AVPLAYER_WEB_GATEWAY_HTTP；检查网关50065 |
| 同机可播放、远程无法收流 | 检查ICE、NAT、防火墙、HTTPS/TURN；HTTP端口可达不等于WebRTC媒体可达 |

## 14. Docker 方式的状态

```sh
cp deploy/.env.example deploy/.env
docker compose -f deploy/docker-compose.yml config --quiet
docker compose -f deploy/docker-compose.yml up --build
```

Compose当前仍需要可达的MCP服务，AVPLAYER_MCP_URL必须按容器网络设置；MCP与容器上传服务需挂载同一媒体文件。
已完成Compose配置校验，未完成镜像构建、容器运行及跨主机WebRTC验证；本地启动成功不等于容器部署已验收。

## 后端模块单测

```sh
cmake --build cmake-build-debug --target server_policy_test
ctest --test-dir cmake-build-debug -R server_least_sessions_policy --output-on-failure
```

模块目录与职责见services/server/README.md。目录整理不改变统一后端进程、端口或客户端接口。


## 文档导航与日常最短路径

整体目录和职责说明见 [ARCHITECTURE.md](ARCHITECTURE.md)。
已有依赖、配置与模型的本机日常操作：

```sh
cd /Users/joe/CLionProjects/AVPlayer
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug --target avplayer_server streaming_smoke_client -j4
scripts/build_mcp.sh
npm --prefix apps/web run build
AVPLAYER_AI_PROVIDER=local scripts/dev.sh
```

该命令不构建或启动Qt客户端。首次环境请按前文章节安装Conan/系统依赖、预置libdatachannel、恢复本地MCP配置和模型。
修改ZooKeeper或负载均衡代码后只需重建avplayer_server，不需要单独构建/启动ZooKeeper适配程序。
可选ZooKeeper服务由外部启动，`AVPLAYER_ZK_HOSTS=127.0.0.1:2181 scripts/dev.sh`启用注册发现。

## 同时打开两个 Web 页面

每个页面自动生成独立的web-随机ID，上传、播放控制与Agent共用此身份，无需手动填写会话ID。
可同时打开两个标签页，各自选择视频、上传、连接并播放；暂停/Seek其中一个不会改变另一个播放会话。
复制标签页也生成新身份；刷新页面会开启新会话，需要重新选择和上传视频，暂不恢复旧页面状态。
会话隔离是功能边界，不等同于用户鉴权。

验证脚本：`.venv/bin/python tests/two_web_sessions.py`，同时上传两份媒体，经WebSocket协商真实音视频并验证控制隔离。

## 转写与摘要持久化

更新后端后会自动创建media_results表，保存同媒体的转写/摘要结果，Redis缓存未命中时从MySQL回填。详见[RESULT_PERSISTENCE.md](RESULT_PERSISTENCE.md)。数据库需在后端启动前可用。

## Web 天气组件

顶部栏提供紧凑天气展示，当前默认北京，仅显示图标、城市、天气和温度。页面可见时每10秒轮询一次，请求未结束时不会叠加；隐藏标签页暂停，恢复后立即查询。
链路：Web `/weather?city=...`→统一C++后端→现有MCP `get_weather`→Open-Meteo。
不使用Agent会话、不调用大模型，也不请求浏览器定位权限；后台静默刷新，失败时保留上次数据，不显示刷新日志。
后端读取AVPLAYER_MCP_URL（默认http://127.0.0.1:8081/jsonrpc），当前天气代理只支持内部HTTP地址；公网HTTPS由外部代理处理。
修改后重建avplayer_server并重启后端；Vite修改配置后需重启，生产Web需重新构建。

```sh
cmake --build cmake-build-debug --target avplayer_server -j4
npm --prefix apps/web run build
node tests/weather_widget.mjs
```

天气自动定位：浏览器授权后获取粗略位置，坐标降至0.1度后发送本机/location，由后端调用OpenStreetMap Nominatim反查城市，再通过MCP天气工具查询。
拒绝或失败时保留北京/上次城市，点击城市可重试。只申请一次，不持续跟踪；不把城市反查加入10秒轮询。
反查结果按粗略坐标缓存，后端串行限速；服务地址可用AVPLAYER_GEOCODER_URL更换。公共Nominatim使用限制见其Usage Policy，规模化部署应换为自建或获授权地理服务。
定位服务需要浏览器安全上下文及权限；本次测试仅用公开城市中心坐标和模拟授权回调，没有读取用户真实位置。
