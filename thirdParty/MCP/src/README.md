# MCP 源码导航

本目录是仓库内维护的独立 C++ MCP 服务实现，不是 Git 子模块。它通过 JSON-RPC 暴露工具；Agent 负责调用与流程编排，MCP 管理任务并执行媒体工具。MCP 不依赖 AVPlayer 服务进程才能执行其自有媒体工具，但源文件位于同一仓库。

| 子目录 | 职责 |
|---|---|
| `main/` | 程序入口、服务装配和 MCP 内置/AVPlayer 工具注册。 |
| `json_rpc/` | HTTP 与 stdio JSON-RPC 传输、解析和方法分发。 |
| `media/` | 媒体工具后端、原生执行器，以及调用 Python 模型推理的适配。 |
| `messaging/` | RabbitMQ 任务分发、MySQL 任务仓储和 Kafka 事件消费。 |
| `persistence/` | MySQL 连接、schema、事务发件箱、事件投影和轻量语句绑定；AVPlayer events 也复用这些头文件。 |
| `sql/` | MySQL 兼容数据库门面及 MCP 工具调用历史仓储。 |
| `job_agent/` | MCP 自带招聘/求职工具的数据访问。 |
| `redis/` | MCP 工具需要的 Redis 缓存访问。 |
| `zk/`, `load_balancer/` | MCP 实例注册发现与工具服务实例选择。 |
| `config/`, `logger/`, `utils/` | 配置、日志和线程池等基础设施。 |

各目录说明补充具体数据流。项目级配置与启动见 [`thirdParty/MCP/README.md`](../README.md)，MySQL、任务租约及历史 SQLite 导入见 [`deploy/mysql/README.md`](../../../deploy/mysql/README.md)。
