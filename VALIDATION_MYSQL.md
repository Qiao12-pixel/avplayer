# 统一 MySQL 验证记录

本轮使用本机真实 MySQL、RabbitMQ（5673）、Kafka（19092），未使用容器模拟结果。

- MCP：93/93 CTest 通过。
- mysql_lease_test：原子领取、续租、过期禁止完成、版本 fencing、取消与发布者租约通过。
- tests/mysql_shared_integration.py：两个独立 MCP 进程竞争，SIGSTOP 旧执行者直到接管，恢复后无法覆盖新产物；重启恢复、共享审计、无新 SQLite 文件通过。
- tests/playback_kafka.py：真实上传、WebRTC 播放控制与录制事件、AI 结果事件、MySQL 统计、同 projection 回放去重、新 projection 重建、Kafka 故障期间继续播放及恢复发件箱通过。
- tests/mcp_independent_agent.py：Agent→MCP→原生提取→Whisper→字幕保存通过，生成17条字幕。
- 离线导入使用临时 SQLite 样本验证：终态结果保留、重复导入幂等、源文件保留。
- avplayer_server、playback_event_consumer、MCP_Server、mcp_event_consumer 构建通过；未构建 Qt 客户端。

未自动导入用户已有旧数据库；需按照 deploy/mysql/README.md 停机、备份并显式执行迁移脚本。测试向共享 MySQL 新增隔离 ID 数据，不清空已有业务数据。

边界：至少一次消息投递；失效执行可能遗留版本化孤立产物；多机仍需共享媒体文件存储。Docker Compose 配置已更新，本轮未实测 Docker 部署。
