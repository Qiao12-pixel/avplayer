> 数据库实现已更新：任务仓储、租约、事件发件箱、统计投影和MCP历史统一使用MySQL；旧SQLite说明仅为历史。当前权威配置、迁移步骤和执行隔离语义见 [deploy/mysql/README.md](deploy/mysql/README.md)。

# AVPlayer 项目结构与运行架构

## 1. 系统组成

项目采用单仓库管理，运行时有三个后端进程：**统一 C++ 后端、Python Agent、自建 MCP**。
Web 与 Qt 是客户端；C++ 内部按模块组织，不再把网关和媒体能力拆成独立进程。

| 运行单元 | 入口 | 职责 |
|---|---|---|
| 统一 C++ 后端 | avplayer_server | 网络接入、上传下载、数据库、媒体会话、编解码、WebRTC、录制 |
| Python Agent | python -m avplayer_agent | 模型调用、LangGraph 编排、MCP 工具调用 |
| C++ MCP | MCP_Server | 独立媒体工具、异步任务管理；自动调用原生执行器 |
| Web | Vite / nginx | 上传、远程播放、AI 与 Agent 界面 |
| Qt（可选） | AVPlayer | 本地播放或远程拉流、桌面交互 |

## 2. 项目目录

```text
AVPlayer/
├── apps/
│   ├── desktop/                     # Qt 界面与客户端专属代码
│   └── web/                         # TypeScript/Vite Web 客户端
├── services/
│   ├── server/                      # 统一 C++ 后端入口与内部模块
│   │   ├── CMakeLists.txt
│   │   ├── tests/                   # 负载策略测试
│   │   └── src/
│   │       ├── main.cpp             # 极简入口
│   │       ├── app/                 # 模块装配与服务启动
│   │       ├── config/              # 环境配置
│   │       ├── network/
│   │       │   ├── common/          # 网络辅助、URL 编码处理
│   │       │   ├── http/            # Asio 监听、HTTP 路由、文件传输
│   │       │   └── websocket/       # 信令/Agent 双向消息、发送队列
│   │       ├── rpc/                 # 上传、数据、播放路由接口
│   │       ├── storage/
│   │       │   ├── redis/           # Redis 连接与缓存
│   │       │   └── mysql/           # MySQL 连接、表、查询与事务
│   │       ├── discovery/
│   │       │   └── zookeeper/       # 后端实例注册、发现、恢复
│   │       └── load_balancing/      # 最少活跃会话选择策略
│   ├── media/                       # 服务端媒体内部库，不是独立进程
│   │   └── src/                     # 媒体会话、H.264/Opus 输出、录制
│   └── agent/                       # 可安装的 Python 编排包及测试
├── libs/
│   └── media-core/                  # Qt 与后端共享的解复用/解码/同步/编码
├── thirdParty/
│   └── MCP/                         # 普通源码目录，不是 Git 子模块
│       ├── src/media/               # MCP 独立 C++ 媒体工具与推理适配
│       ├── config/                  # 本地配置，Git 忽略
│       └── tests/                   # MCP 单测与独立媒体测试
├── proto/                           # 项目内部 gRPC 契约
├── scripts/                         # 构建 MCP、开发启动、生成协议与测量
├── tests/                           # 跨进程与完整业务验证
├── deploy/                          # Dockerfile、Compose、nginx、环境样例
├── config/                          # Agent 模型凭据及样例
├── resource/                        # 本地模型、示例媒体等，不提交
├── storage/                         # 上传、字幕、剪辑与录制产物，不提交
├── cache/                           # 本地运行缓存，不提交
├── build/mcp/                       # MCP 独立构建产物，不提交
└── cmake-build-debug/                # 主工程构建产物，不提交
```

ZooKeeper 已从 libs/service-discovery 迁入 services/server/src/discovery/zookeeper。
它只属于后端注册发现，媒体内部库与共享 media-core 不再依赖 ZooKeeper。

## 3. C++ 模块职责与依赖

| 模块 | 做什么 | 不负责什么 |
|---|---|---|
| app | 创建对象、注册 gRPC、启动 HTTP | 不写数据库或媒体处理逻辑 |
| network | 连接生命周期、HTTP/WS、分块传输与取消 | 不直接访问 MySQL/hiredis |
| rpc | 请求转换、上传、会话绑定与路由 | 不实现负载算法或数据库命令 |
| storage/redis | 缓存键、TTL、连接与缓存操作 | 不决定播放实例 |
| storage/mysql | 元数据、字幕、任务记录与事务 | 不管理 WebSocket |
| discovery/zookeeper | 临时注册、Watch、实例列表与过期恢复 | 不分配业务会话 |
| load_balancing | 从候选实例中选择最少会话者，同负载轮换 | 不访问 ZooKeeper 或发送 RPC |
| services/media | 媒体会话、编解码、WebRTC、录制 | 不承担 HTTP 路由 |

模块分别构建为 server_network、server_rpc、server_mysql、server_redis、server_discovery、server_load_balancing 静态库，最终链接为同一个 avplayer_server。
Redis/MySQL 的驱动头文件通过 PImpl 隔离，各自维护访问锁。

## 4. 运行时链路

### 播放与控制

```text
Web → HTTP/WS :50065 → avplayer_server
                              ├→ HTTP 分块上传 → UploadTransaction → 文件存储
                              ├→ 数据与播放路由 gRPC :50066
                              └→ 媒体模块/集群内部 gRPC :50062
Web/Qt ←──────── WebRTC H.264 + Opus ───────┘
```

50065、50066、50062 都是同一 C++ 进程的监听端口，不代表三个服务。
Web 上传由 HTTP 网关直接调用共享上传事务并写文件；gRPC `UploadMedia` 仍供原生 gRPC 客户端使用。播放控制和远程媒体路由使用 gRPC；单实例 `MediaRouter` 直接调用进程内媒体实现。
Qt 上传/远程播放默认连50066，Agent通信连50061；本地播放直接使用media-core。

### Agent 工具

```text
Web/Qt → Agent :50061 → MCP /jsonrpc :8081
                           ├→ 查询媒体、读写字幕与剪辑计划
                           └→ 本地 C++ 执行器 → FFmpeg / 模型推理
```

工具实现只在 thirdParty/MCP，AVPlayer 中没有重复工具执行器或专属 Worker。
Agent 发现 schema、绑定 session/media、提交任务并查询状态；MCP 本地队列不参与 AVPlayer 媒体实例均衡。
MCP 的任务管理驻留进程内，不承诺持久恢复或 exactly-once。

## 5. 数据与文件归属

C++ 后端接收上传，写 storage/session/media/original.*，并通知 Agent 绑定媒体。
MCP 必须访问同一 storage，独立保存 JSON/SRT、剪辑计划和导出文件。
C++ MySQL/Redis 接口用于元数据、缓存和任务记录；MCP 文件工具自身不直连数据库；Agent生成转写/摘要成功后，经C++结果存储接口同步MySQL和Redis。
Redis/MySQL 连接维持已有启动连接和故障降级机制，此次整理不新增连接池或自动重连承诺。

## 6. ZooKeeper 与多实例

完整 avplayer_server 实例注册自身内部媒体地址。入口按本地会话数选择实例，后续信令和控制固定到原实例。
已有播放状态保存在被选实例内；故障后需重新协商，不是无缝迁移。
多个入口尚未共享路由表，必须保持入口会话粘性；计数不是全局负载统计。
单实例可不启动 ZooKeeper，但编译后端仍需 ZooKeeper C 客户端库。

## 7. 开发入口

完整构建与启动步骤见 [STARTUP.md](STARTUP.md)。
后端模块导航见 [services/server/README.md](services/server/README.md)。
多实例与故障测试见 [deploy/ZOOKEEPER.md](deploy/ZOOKEEPER.md)。
源码目录导航：[`services/server/src`](services/server/src/README.md)、[`services/agent/avplayer_agent`](services/agent/avplayer_agent/README.md)、[`thirdParty/MCP/src`](thirdParty/MCP/src/README.md)、[`libs/media-core`](libs/media-core/README.md)、[`proto`](proto/README.md) 和 [`apps/desktop`](apps/desktop/README.md)。

内部 gRPC/ZooKeeper 当前用于可信开发网络，未实现完整生产鉴权；容器运行和公网 WebRTC 需要单独验收。

转写与摘要按媒体ID持久化、缓存回填及复用规则见[RESULT_PERSISTENCE.md](RESULT_PERSISTENCE.md)。

Web天气组件独立于媒体/Agent会话：浏览器每10秒请求统一后端weather接口，后端integrations/mcp模块调用MCP get_weather。
隐藏页面暂停，不获取用户定位；依赖MCP与Open-Meteo外网可达。

## 消息系统更新

MCP耗时任务不再使用进程内业务队列：RabbitMQ负责分发，Kafka承载任务状态事件，MySQL任务仓储和事务发件箱负责重启恢复与可靠发布。AVPlayer与MCP共享MySQL，任务租约及执行版本用于拒绝过期Worker回写。
源码在thirdParty/MCP/src/messaging，媒体工具API保持不变。
新增mcp_event_consumer进程记录审计事件；模型编排、实时播放链路不变。
配置、同机部署边界和验证见[MESSAGING.md](MESSAGING.md)。

## 后端Kafka播放事件与统计

统一C++后端的events模块收集上传、播放生命周期、控制、录制和AI结果保存事件，通过MySQL持久发件箱发送Kafka。
媒体模块仅提供事件回调，不直接依赖broker；不逐包上报，也不改变WebRTC传输路径。
独立playback_event_consumer消费avplayer.playback.events，完成事件去重、分钟计数和会话统计，/playback-stats读取MySQL投影。
MCP继续使用RabbitMQ分发耗时任务；其任务事件主题与播放事件主题分开。Kafka回放可重建统计，具体指标语义和单机边界见MESSAGING.md。
