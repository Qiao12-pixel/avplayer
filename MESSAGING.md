# RabbitMQ、Kafka 与共享 MySQL

AVPlayer 与 MCP 使用同一个 `AVPLAYER_MYSQL_DSN`。任务、租约、发件箱、消费去重、播放统计、工具调用历史都由 C++ 读写 MySQL；Python 负责 Agent 编排与推理接入。运行时不使用 SQLite。

## 耗时任务

MCP 接收任务后，在同一 MySQL 事务保存任务状态与 RabbitMQ 发布发件箱。发布器确认 broker 收到消息后才标记已发送，消息仅携带任务 ID。

Worker 通过数据库事务行锁领取任务，取得 owner、execution_version 与数据库时间计算的租约。执行期间续租；成功、失败回写都必须匹配当前 owner/version，且租约有效、任务未取消。

租约到期由发布循环撤销旧版本、重新排队，并事务性创建新消息。旧 Worker 恢复后不能回写状态。每次执行使用独立目录与带版本号的产物名，避免旧进程覆盖新结果。取消会立即撤销租约；执行器发现续租失败时终止子进程组。

RabbitMQ 是至少一次投递，数据库领取与版本校验负责并发去重；不承诺外部操作恰好执行一次。媒体、模型与产物仍存放文件系统，跨机器需要共享产物存储。

## 事件与统计

MCP 任务状态事件进入 Kafka；AVPlayer 上传、播放、暂停、Seek、录制和 AI 结果持久化事件进入播放事件主题。

生产端先写 MySQL 发件箱，再异步投递 Kafka。消费者在同一事务完成事件 ID 去重和统计更新，提交成功后才提交 Kafka 偏移。重放同一 projection 不重复计数，新的 projection 与消费组可独立重建统计。

默认主题为 `avplayer.media.events` 与 `avplayer.playback.events`，默认任务队列为 `avplayer.media.tasks`。消费者可以独立启动，不要求与服务端同进程。

## 构建、配置和验证

完整环境变量、表结构、旧库离线导入和测试命令见 [MySQL 迁移说明](deploy/mysql/README.md)，整个项目的启动见 [STARTUP.md](STARTUP.md)。

旧 `MCP_JOB_DB`、`MCP_EVENT_DB`、`MCP_JOB_LOCK_DIR`、`AVPLAYER_EVENT_OUTBOX`、`AVPLAYER_STATS_DB` 均已移除。不要使用旧 SQLite 文件路径配置新服务。
