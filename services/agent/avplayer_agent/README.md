# Agent 包导航

这是 Python Agent 服务的实现包。它负责会话管理、模型调用与 LangGraph 编排；媒体文件处理和耗时媒体工具由 MCP/C++ 执行，数据库及媒体存储接口也由后端服务提供。

| 文件 | 职责 |
|---|---|
| `__main__.py`, `server.py` | 进程入口、gRPC 服务注册和生命周期。 |
| `runtime.py`, `protocol.py` | 解析 Agent 请求、组织流程并将结果编码为客户端消息。 |
| `session_manager.py` | 按会话管理运行上下文和并发请求。 |
| `mcp_client.py` | 发现并调用 MCP 工具。 |
| `ark_client.py` | 模型服务客户端。 |
| `data_client.py`, `storage.py` | 访问 C++ 数据/Worker 服务和文件能力。 |
| `db.py`, `cache.py`, `result_store.py` | Agent 侧结果读取、缓存和持久结果复用。 |
| `settings.py` | 环境变量与运行配置。 |

单元测试位于 `services/agent/tests/`。接口定义来自仓库根目录 `proto/`，服务启动、环境变量和完整请求链路见 [Agent 说明](../README.md) 与 [STARTUP.md](../../../STARTUP.md)。
