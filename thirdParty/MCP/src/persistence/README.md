# 共享 MySQL 持久化适配

这些头文件集中封装 MCP 与 AVPlayer 后端事件模块共用的 MySQL 基础能力，不是单独运行的服务。

- `MySql.h`：读取共享 DSN、连接、重连和事务边界；事务中断线会失败并回滚，不在半途自动换连接。
- `Schema.h`：幂等建表和兼容所需的 schema 检查。
- `Outbox.h`：同一事务内保存待发事件，并以数据库租约/version 并发领取。
- `EventStore.h`：事件去重、播放统计投影及审计记录。
- `Statement.h`：供 MCP 旧仓储代码迁移使用的轻量参数绑定/结果读取接口，底层仍是 MySQL。

应用层分别在 `services/server/src/events/` 和 `thirdParty/MCP/src/messaging/`。该目录不管理媒体文件；跨主机部署的媒体/产物共享需单独配置。数据库 schema 与测试见 [`deploy/mysql/README.md`](../../../../deploy/mysql/README.md)。
