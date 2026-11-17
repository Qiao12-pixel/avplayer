# MCP 消息与任务

`JobStore` 是任务状态的权威仓储。它在 MySQL 事务内完成提交、原子领取、租约续期、取消、过期重派和结果回写；每次领取递增 `execution_version`，旧 owner/version 不能续租或完成新一轮任务。普通执行失败会在同一事务中更新为待重试并写入 Rabbit outbox；第三次失败后转为终态并进入死信队列。

RabbitMQ 仅分发任务 ID。MySQL outbox 与任务状态在同一事务提交，发布器使用带版本的发布租约避免并发重复发布造成状态丢失。任务状态事件通过 outbox 发布 Kafka；`EventConsumer` 在 MySQL `consumed_events` 中按 projection 和 event ID 去重，事务提交后才提交 Kafka 偏移。

媒体执行适配位于相邻 `../media/`：它领取任务后按 execution version 隔离工作目录和产物。配置、故障边界、至少一次投递语义和端到端验证见 [消息与 MySQL 文档](../../../../MESSAGING.md) 及 [迁移文档](../../../../deploy/mysql/README.md)。
