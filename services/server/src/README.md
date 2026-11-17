# C++ 后端源码导航

这里是统一 `avplayer_server` 的内部模块。`../main.cpp` 只负责创建并运行 `app/ServerApplication`；各模块分别编译成静态库，最终仍运行在同一个服务进程内。

| 目录 | 职责 |
|---|---|
| `app/` | 装配 HTTP、WebSocket、gRPC、媒体、存储和发现组件并启动监听。 |
| `config/` | 从环境变量读取服务地址、存储连接和集群配置。 |
| `network/` | HTTP 路由、WebSocket 会话、URL 和网络通用辅助；负责连接生命周期，不直接写数据库。 |
| `rpc/` | gRPC 上传/数据服务，以及媒体实例选择和内部调用。 |
| `storage/mysql/` | 媒体、会话、转写和结果等业务数据的 MySQL 仓储。 |
| `storage/redis/` | Redis 缓存访问；缓存失效时由上层仓储回源。 |
| `discovery/zookeeper/` | 注册当前服务实例、发现同类实例并处理成员变化。 |
| `load_balancing/` | 对已发现实例按活跃会话数选取目标；不访问 ZooKeeper。 |
| `events/` | 写播放/服务事件 MySQL 发件箱、发布 Kafka，以及独立消费端更新播放统计投影。 |
| `integrations/mcp/` | C++ 后端对 MCP 天气工具的客户端集成。 |

HTTP 与 WebSocket 网关、存储和媒体能力由一个 `avplayer_server` 进程提供。需要理解启动顺序时从 `app/ServerApplication.cpp` 阅读；需要理解跨实例媒体路由时从 `rpc/MediaRouter.cpp` 开始。

MySQL/Kafka 共享持久化适配器在 `thirdParty/MCP/src/persistence/`，本目录的事件代码复用它；具体依赖和构建目标见本目录 `CMakeLists.txt` 及 [服务端说明](../README.md)。
