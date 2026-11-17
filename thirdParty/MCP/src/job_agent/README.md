# MCP 求职工具数据访问

`job_agent_database.*` 为 MCP 自带的简历、职位、匹配、面试与申请工具提供 MySQL 表结构初始化和数据读写。工具接口注册在相邻 `../main/mcp_job_agent_tools.*`。

这是 MCP 的示例/扩展领域数据，不是 AVPlayer 媒体任务仓储；共享 DSN 和迁移说明见 [`deploy/mysql/README.md`](../../../../deploy/mysql/README.md)。
