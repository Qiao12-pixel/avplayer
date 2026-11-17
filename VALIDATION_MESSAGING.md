# RabbitMQ/Kafka验证（2026-11-16）

环境：macOS Apple Silicon，本机真实RabbitMQ4.4.0（隔离5673）、Kafka4.4.0 KRaft（隔离19092），C++客户端rabbitmq-c0.18.1/librdkafka2.16。
不使用模拟broker。测试MCP独立启动，不依赖AVPlayer媒体服务执行工具。

通过tests/messaging_integration.py：
- 两个broker地址不可达时，任务和事件发件箱事务持久保存，不回退内存任务队列。
- 重启并接通broker后，待发布任务恢复执行。
- 两个MCP进程竞争消费，共用SQLite任务库/锁目录；重复Rabbit投递未触发并发重复执行。
- SIGKILL执行中的MCP，重启后Rabbit重新投递，任务结果可继续查询。
- 取消状态持久化；无效剪辑失败后进入专用死信队列。
- Kafka状态事件交付后清理发件箱，审计消费者保存事件后提交偏移。
- 新消费组实际重新读取全部事件，审计按event_id去重，记录数保持不变。

额外通过：
- 真实MCP独立抽音轨、导出、转码、任务ID、取消与会话隔离测试。
- Agent→MCP→Rabbit分发→原生音轨提取/Whisper→字幕保存，17条segments。
- MCP93项CTest、Agent28项单测。
- scripts/dev.sh在隔离服务端口启动成功，Kafka主题尚未存在时消费者继续等待。
- scripts/build_mcp.sh、脚本语法及Compose配置校验。

范围限制：任务仓储/执行锁目前为同机SQLite/文件锁，不是多机Worker集群。至少一次语义，不宣称exactly-once。
本轮使用本机broker，Docker仅提供并校验配置；没有验收容器部署、消息积压压力或生产HA集群。
开发依赖已安装，但默认开发端口5672/9092需要用户启动对应broker，隔离测试服务会停止。
