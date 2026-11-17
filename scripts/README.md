# 开发脚本

- `dev.sh`：加载本地开发配置并启动 C++ 服务端、MCP 和 Agent 等进程；所需中间件与环境变量见 `STARTUP.md`。
- `build_mcp.sh`：配置并构建 vendored MCP C++ 服务及事件消费者。
- `gen_proto.sh`：从 `proto/` 生成 C++ 和 Python gRPC/Protobuf 绑定。
- `migrate_sqlite_to_mysql.py`：停机后显式迁移旧版本数据；不会删除或修改源 SQLite 文件。
- `bench_metrics.py`：汇总性能测试输出，不启动业务服务。
- `download_local_models.sh`：获取本地模型资源。

脚本会读取当前仓库路径和环境变量；避免在生产数据库上运行测试或迁移前先核对目标 DSN。MySQL 数据迁移步骤见 `deploy/mysql/README.md`。
