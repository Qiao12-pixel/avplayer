# AVPlayer 构建与启动

本文用于在本机已有编译工具、Python 虚拟环境、Node 模块和项目配置的情况下，构建并启动**除 Qt 客户端外**的完整项目。所有命令从仓库根目录执行。每个前台服务命令分别在一个终端运行。

项目由统一 C++ 后端、MCP、Python Agent、两个 Kafka 消费者和 Web 页面组成。C++ 后端进程同时提供 HTTP/WebSocket 网关、gRPC 数据接口和媒体/WebRTC 服务。

## 构建

### 1. 恢复已删除构建目录所需的 Conan 配置

`cmake-build-debug` 和 `build` 被删除后，先重新生成 Conan 的 CMake 包配置。Conan 会优先复用本机缓存；缺失的包才会获取或构建。

```sh
.tools/conan/bin/conan install . -of cmake-build-debug -s build_type=Debug \
  -s compiler.cppstd=17 --build=missing
.tools/conan/bin/conan install thirdParty/MCP -of build/mcp -s build_type=Debug \
  --build=missing
```

### 2. 配置并构建 C++ 后端

```sh
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$PWD/cmake-build-debug/build/Debug/generators:/opt/homebrew"
cmake --build cmake-build-debug \
  --target avplayer_server playback_event_consumer -j4
```

若构建目录中没有 libdatachannel 源码，CMake 会在首次配置时获取项目固定使用的 `v0.23.1`；若源码存在但 Git 子模块不全，配置时会递归补齐；完整后续配置会复用已有源码。

### 3. 配置并构建 MCP

```sh
cmake -S thirdParty/MCP -B build/mcp \
  -DCMAKE_BUILD_TYPE=Debug -DMCP_ENABLE_AVPLAYER=ON
cmake --build build/mcp \
  --target MCP_Server mcp_media_executor mcp_event_consumer -j4
```

### 4. 构建 Web

```sh
npm --prefix apps/web run build
```

本地联调时仍使用 Vite 开发服务器，见后文。上面的构建会生成 `apps/web/dist`。

## 启动中间件

MySQL、RabbitMQ 和 Kafka 是后端/MCP 的必需服务，Redis 提供缓存。若这些 Homebrew 服务已安装，直接在仓库根目录启动：

```sh
brew services start mysql
brew services start redis
brew services start rabbitmq
brew services start kafka
brew services start zookeeper
brew services list
```

确认四项服务状态为 `started` 后再启动应用。默认本机端口为 MySQL `3306`、Redis `6379`、RabbitMQ `5672`、Kafka `9092`。可用以下命令确认端口监听：

```sh
lsof -nP -iTCP:3306 -iTCP:6379 -iTCP:5672 -iTCP:9092 -sTCP:LISTEN
rabbitmq-diagnostics -q ping
kafka-topics --bootstrap-server 127.0.0.1:9092 --list
```

本机 MySQL 必须已有 `avplayer` 数据库和 `avplayer` 用户；下面命令按 `avplayer/avplayer` 示例填写，若实际凭据不同请替换 DSN。Homebrew RabbitMQ 默认使用 `guest/guest`，因此若尚未配置应用账号，将 MCP 命令中的 `MCP_RABBIT_USER`、`MCP_RABBIT_PASSWORD` 改为 `guest`。如果已为 RabbitMQ 创建 `avplayer` 用户，则使用该用户凭据。

也可选择仓库的 Docker Compose 中间件（需要 Docker daemon 和可访问镜像仓库）：

```sh
docker compose -f deploy/docker-compose.yml up -d mysql redis rabbitmq kafka
docker compose -f deploy/docker-compose.yml ps
```

Compose 默认 MySQL 为 `avplayer/avplayer`，RabbitMQ 为 `avplayer/avplayer-dev`。使用 Compose 时 MCP 命令保持 `avplayer/avplayer-dev`；不要与 Homebrew RabbitMQ 的默认凭据混用。

启动 MCP 前需要已有配置文件 `thirdParty/MCP/config/server.json`。上传文件、MCP 工具和 Agent 共用仓库下的 `storage/`。

## 启动应用

每个代码块单独在一个终端执行，保持进程运行。

### 1. MCP 工具服务

```sh
AVPLAYER_PROJECT_ROOT="$PWD" \
AVPLAYER_STORAGE_ROOT="$PWD/storage" \
MCP_MEDIA_ROOT="$PWD/thirdParty/MCP" \
AVPLAYER_WORKER_PYTHON="$PWD/.venv/bin/python" \
AVPLAYER_MYSQL_DSN='mysql://avplayer:avplayer@127.0.0.1:3306/avplayer' \
MCP_RABBIT_HOST=127.0.0.1 MCP_RABBIT_PORT=5672 \
MCP_RABBIT_USER=guest MCP_RABBIT_PASSWORD=guest \
MCP_KAFKA_BROKERS=127.0.0.1:9092 \
./build/mcp/MCP_Server \
  --config "$PWD/thirdParty/MCP/config/server.json" \
  --host 127.0.0.1 --port 8081
```

MCP 会自动启动 `build/mcp/mcp_media_executor`。`AVPLAYER_WORKER_PYTHON` 使用已有的 Agent 虚拟环境，供需要 Python 推理的工具调用。

### 2. MCP 事件消费者

```sh
AVPLAYER_MYSQL_DSN='mysql://avplayer:avplayer@127.0.0.1:3306/avplayer' \
MCP_KAFKA_BROKERS=127.0.0.1:9092 \
MCP_EVENT_PROJECTION=mcp-audit-v1 \
./build/mcp/mcp_event_consumer
```

### 3. C++ 后端

```sh
AVPLAYER_STORAGE_ROOT="$PWD/storage" \
AVPLAYER_MYSQL_DSN='mysql://avplayer:avplayer@127.0.0.1:3306/avplayer' \
AVPLAYER_KAFKA_BROKERS=127.0.0.1:9092 \
AVPLAYER_AGENT_SERVER=127.0.0.1:50061 \
AVPLAYER_DATA_SERVER_ADDRESS=127.0.0.1:50066 \
AVPLAYER_MEDIA_SERVER_ADDRESS=127.0.0.1:50062 \
./cmake-build-debug/services/server/avplayer_server
```

该进程同时监听 HTTP/WebSocket `50065`、数据与上传 gRPC `50066`、媒体/WebRTC gRPC `50062`。

### 4. 播放统计消费者

```sh
AVPLAYER_MYSQL_DSN='mysql://avplayer:avplayer@127.0.0.1:3306/avplayer' \
AVPLAYER_KAFKA_BROKERS=127.0.0.1:9092 \
AVPLAYER_STATS_PROJECTION=playback-v1 \
./cmake-build-debug/services/server/playback_event_consumer
```

### 5. Python Agent

```sh
AVPLAYER_PROJECT_ROOT="$PWD" \
AVPLAYER_STORAGE_ROOT="$PWD/storage" \
AVPLAYER_DATA_SERVER=127.0.0.1:50066 \
AVPLAYER_MCP_URL=http://127.0.0.1:8081/jsonrpc \
AVPLAYER_AI_PROVIDER=local \
PYTHONPATH="$PWD/services/agent" \
.venv/bin/python -m avplayer_agent
```

AI 功能使用已有的本地模型配置；若项目配置的是云端模型，将 `AVPLAYER_AI_PROVIDER=local` 改为 `cloud`。模型服务和凭据需已可用。

### 6. Web 页面

```sh
npm --prefix apps/web run dev
```

在终端输出的地址打开页面，默认通常为 `https://localhost:5173`。仅本机 HTTP 调试可运行：

```sh
AVPLAYER_WEB_HTTP=1 npm --prefix apps/web run dev
```

Vite 将上传、控制、信令、天气等请求代理到本机 C++ 网关 `50065`。

## 可选：四个 MCP Worker 与 ZooKeeper 负载均衡

该模式替代上面的单实例 MCP 启动方式。Agent 将工具请求发送到 Proxy，Proxy 根据 ZooKeeper 注册列表按平滑加权轮询选择 Worker；耗时媒体任务再由 RabbitMQ 分发，实际执行者可能不是接收请求的实例。网页播放仍由 C++ 后端处理。

### 1. 启动 ZooKeeper

```sh
brew services start zookeeper
lsof -nP -iTCP:2181 -sTCP:LISTEN
```

### 2. 启动四个 Worker

打开四个终端，各自进入仓库根目录。分别将第一行的 `MCP_PORT` 设为 `8081`、`8082`、`8083`、`8084`，然后执行完整命令：

```sh
MCP_PORT=8081
AVPLAYER_PROJECT_ROOT="$PWD" \
AVPLAYER_STORAGE_ROOT="$PWD/storage" \
MCP_MEDIA_ROOT="$PWD/thirdParty/MCP" \
AVPLAYER_WORKER_PYTHON="$PWD/.venv/bin/python" \
AVPLAYER_MYSQL_DSN='mysql://avplayer:avplayer@127.0.0.1:3306/avplayer' \
MCP_RABBIT_HOST=127.0.0.1 MCP_RABBIT_PORT=5672 \
MCP_RABBIT_USER=guest MCP_RABBIT_PASSWORD=guest \
MCP_RABBIT_QUEUE=avplayer.media.tasks \
MCP_KAFKA_BROKERS=127.0.0.1:9092 \
MCP_KAFKA_TOPIC=avplayer.media.events \
MCP_MEDIA_SLOTS=2 \
./build/mcp/MCP_Server --mode worker \
  --config "$PWD/thirdParty/MCP/config/server.json" \
  --host 127.0.0.1 --port "$MCP_PORT" \
  --zk-hosts 127.0.0.1:2181
```

四个进程共享 MySQL、Rabbit 队列、Kafka 主题和媒体目录，默认合计八个后台执行槽。配置中的日志路径也可能共用，排查单实例问题时应为各实例使用独立日志配置。这里的 `127.0.0.1` 适用于同机运行；跨机器必须使用 Proxy 可访问的 Worker 地址和共享媒体存储。

### 3. 启动 MCP Proxy

另开一个终端，在仓库根目录运行：

```sh
./build/mcp/MCP_Server --mode proxy \
  --proxy-port 8090 --zk-hosts 127.0.0.1:2181
```

Proxy 自身不执行媒体任务，也不占用后台媒体执行槽。四个 Worker 注册成功后，它会发现并转发请求；默认权重相同时轮流选择实例。

### 4. Agent 接入 Proxy

将上面 Agent 启动命令替换为以下命令；已有 Agent 进程先按 `Ctrl+C` 停止：

```sh
AVPLAYER_PROJECT_ROOT="$PWD" \
AVPLAYER_STORAGE_ROOT="$PWD/storage" \
AVPLAYER_DATA_SERVER=127.0.0.1:50066 \
AVPLAYER_MCP_URL=http://127.0.0.1:8090/jsonrpc \
AVPLAYER_AI_PROVIDER=local \
PYTHONPATH="$PWD/services/agent" \
.venv/bin/python -m avplayer_agent
```

C++ 后端、Web 和两个事件消费者继续使用前文命令。MCP 事件消费者通常只需启动一个；增加消费者副本时使用相同消费组和 projection，不需要每个 Worker 配一个独立消费组。

若也希望天气组件通过 Proxy 调用 MCP，在前文 C++ 后端启动命令中加入 `AVPLAYER_MCP_URL=http://127.0.0.1:8090/jsonrpc`。

### 5. 检查 Proxy 与 Worker

```sh
lsof -nP -iTCP:8081 -iTCP:8082 -iTCP:8083 -iTCP:8084 -iTCP:8090 -sTCP:LISTEN
curl -fsS http://127.0.0.1:8090/health
curl -fsS http://127.0.0.1:8090/jsonrpc \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}}'
```

`/health` 只表示 Proxy 存活；`tools/list` 成功才说明至少一个 Worker 能被代理访问。Proxy 日志中的实例列表用于确认是否发现四个 Worker。请求按调用分配，不按网页固定绑定。

停止时在四个 Worker、Proxy、Agent 等业务终端按 `Ctrl+C`；需要停止 ZooKeeper 时执行 `brew services stop zookeeper`。

## 检查与停止

```sh
curl -fsS http://127.0.0.1:50065/health
curl -fsS http://127.0.0.1:50065/config
curl -fsS http://127.0.0.1:8081/jsonrpc \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"startup-check","version":"1"}}}'
```

主要端口：Web `5173`、MCP `8081`、Agent gRPC `50061`、HTTP/WebSocket `50065`、数据/上传 gRPC `50066`、媒体 gRPC `50062`。

在各业务进程终端按 `Ctrl+C` 停止。Homebrew 中间件停止命令：

```sh
brew services stop kafka
brew services stop rabbitmq
brew services stop redis
brew services stop mysql
```

若使用 Compose 启动的中间件，停止服务并保留数据卷：

```sh
docker compose -f deploy/docker-compose.yml stop mysql redis rabbitmq kafka
```

不要使用 `docker compose down -v`，该命令会删除 Compose 数据卷。
