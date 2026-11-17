# MCP SQL 仓储

`mysql_database.h` 为 MCP 工具历史和自带招聘工具提供简洁的 MySQL 连接/执行门面；`tool_call_history_repository.*` 保存和查询工具调用记录。它们不创建 SQLite 文件。

任务队列、租约、outbox 与 Kafka 消费状态属于 `../messaging/` 和 `../persistence/`，不要把任务状态逻辑继续塞进工具历史仓储。旧配置中的 `db_path` 只作为兼容标签，不是数据库地址；运行时统一使用 `AVPLAYER_MYSQL_DSN`。数据导入说明见 [`deploy/mysql/README.md`](../../../../deploy/mysql/README.md)。
