# 统一 MySQL 数据库与租约执行

AVPlayer后端、MCP任务仓储、工具调用历史、MCP自带job-agent数据库、Kafka发件箱和消费者投影，现统一使用 `AVPLAYER_MYSQL_DSN`。
业务代码不再创建SQLite数据库；媒体文件、字幕文件和模型文件仍放文件系统，这是产物存储而非数据库。
`MCP_JOB_DB/MCP_EVENT_DB/MCP_JOB_LOCK_DIR/AVPLAYER_EVENT_OUTBOX/AVPLAYER_STATS_DB`旧变量已移除，不能用于切回SQLite。

## 启动

```sh
export AVPLAYER_MYSQL_DSN='mysql://avplayer:你的密码@127.0.0.1:3306/avplayer'
scripts/build_mcp.sh
cmake --build cmake-build-debug --target avplayer_server playback_event_consumer -j4
scripts/dev.sh
```

MySQL须预先创建数据库和账户，建议MySQL8.0+，实际本机验证版本见验证记录。账号需要建表及事务权限；表使用InnoDB。
C++共享适配位于thirdParty/MCP/src/persistence；主后端以头文件依赖复用，不依赖MCP进程执行数据库读写。
DDL在启动时幂等执行，002_shared_jobs.sql为可手动执行的对应表结构。此前草案若已执行且缺status_json等字段，请先备份并人工对齐，不会自动丢弃已有表。
MCP旧配置里的本地DB路径现在仅为兼容标签，不创建文件。DSN解析暂不支持密码中未编码的@、IPv6或TLS选项，部署时使用当前支持的格式。

## 任务安全

- 主键job_id；请求内容不相同的重复提交拒绝，相同请求返回原状态。
- 事务行锁原子领取，租约使用MySQL NOW(6)，与客户端机器时钟无关。
- 每次领取增加execution_version；owner、version、未取消、未过期是续租和完成的共同条件。
- 默认15秒租约，约每1/3租期续约；MCP_JOB_LEASE_SECONDS可设3..300。
- 数据库故障或续租失败视为租约丢失，终止子进程组，不确认成功。未确认Rabbit消息可重新投递。
- 发布循环扫描本队列过期任务，事务内重新排队、更新版本并写Rabbit发件箱；没有文件锁或同机SQLite依赖。
- Rabbit消息在租约被其他人持有时重新排队，不抢占该执行者。
- 任务最多尝试3次，耗尽转为失败/死信。取消立即撤销租约、递增版本，旧执行者不能再完成。
- 每次执行在jobs/job_id/attempt-version下隔离；产物名含job_id-v版本号。旧执行可能留下孤立文件，但不能覆盖新版本或修改已接受的数据库结果。
- 全局待完成任务上限通过mcp_admission行锁串行准入，MCP_PENDING_LIMIT默认1024。

状态变更、事件发件箱和Rabbit任务发布发件箱同一事务提交。发布器领取有租约的发件箱条目，确认broker交付后以owner/version/未过期为条件标记已发送。
Kafka生产仍至少一次，重复事件由消费端处理；不承诺exactly-once或外部模型API不重复收费。
媒体/事件大payload通过MySQL JSON保存，消息只引用任务或发送事件；跨机器部署仍需要可共享的媒体/产物存储与一致配置。

## 统计与审计

consumed_events用 `(projection_name,event_id)` 唯一键去重。
播放消费者将去重记录、事件计数、分钟统计、会话和实例心跳在同一MySQL事务更新后，才提交Kafka偏移。
- AVPLAYER_STATS_PROJECTION：默认playback-v1，后端查询和消费者必须一致。
- MCP_EVENT_PROJECTION：默认mcp-audit-v1。
- 同projection换消费组：去重回放，不重复统计。
- 新projection+新消费组：独立重建，不污染旧统计。
不同实例可以共享MySQL，不再将多个消费者的本地库误认为全局聚合。
当前单projection以数据库行锁串行更新，保证正确性优先，不宣称高吞吐分析引擎。

表：mcp_jobs、event_outbox、consumed_events、playback_event_counts、playback_minute_counts、playback_sessions、playback_instances、mcp_admission。
原业务表media_results、transcripts/segments、task_runs等保留。MCP工具调用记录使用tool_call_history；自带招聘工具表也已改为MySQL，非本平台业务链路所必需。

## 历史SQLite导入

不删除旧数据库，不在程序启动时盲目导入。先停止旧/新任务执行、发布与消费进程，备份MySQL与SQLite。
新程序先成功启动一次建表，再停止；使用同一个DSN离线导入：

```sh
.venv/bin/python scripts/migrate_sqlite_to_mysql.py --kind jobs --sqlite cache/mcp/media_jobs.sqlite3 --queue avplayer.media.tasks --topic avplayer.media.events --stopped
.venv/bin/python scripts/migrate_sqlite_to_mysql.py --kind playback --sqlite cache/events/playback_outbox.sqlite3 --topic avplayer.playback.events --stopped
.venv/bin/python scripts/migrate_sqlite_to_mysql.py --kind playback --sqlite cache/events/playback_stats.sqlite3 --topic avplayer.playback.events --stopped
.venv/bin/python scripts/migrate_sqlite_to_mysql.py --kind audit --sqlite cache/mcp/media_events.sqlite3 --projection mcp-audit-v1 --stopped
```

jobs导入会将旧running任务重新排队，已取消/已完成保持终态；原始请求ID保持不变。存在ID且输入不同会中止，不覆盖目标数据。
playback将旧raw事件及未发送发件箱重新发布至Kafka，由新的MySQL消费者projection重建统计。
同一输入重复导入的发件箱ID稳定，不会不断生成新ID。历史Kafka事件保留期不足时，SQLite raw事件可补充历史来源。
--kind history / job-agent可导入旧工具历史/招聘工具关系数据，ID冲突直接回滚，不自动REPLACE覆盖。
脚本需要pymysql，仅迁移工具用Python；运行数据库逻辑仍为C++。

## 故障验证

```sh
cmake --build build/mcp --target mysql_lease_test
build/mcp/mysql_lease_test
.venv/bin/python tests/mysql_shared_integration.py
AVPLAYER_KAFKA_BROKERS=127.0.0.1:19092 .venv/bin/python tests/playback_kafka.py
```

租约测试覆盖活跃竞争、续期、过期、旧owner完成拒绝、取消和发件箱发布版本校验。
集成测试使用真实MySQL/Rabbit/Kafka，SIGSTOP旧执行进程直到接管，恢复后检查旧执行者不能覆盖新产物；也验证重启状态、共享审计和未创建SQLite文件。
测试会新增mysql-*会话数据，不清空现有业务表。
