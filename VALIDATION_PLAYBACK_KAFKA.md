# 后端Kafka播放事件验证（2026-11-16）

本轮使用本机真实Kafka4.4.0 KRaft，隔离19092；C++客户端librdkafka2.16。

已验证：
- 统一C++后端、playback_event_consumer和接收端构建成功。
- HTTP上传完成、AI结果持久化失败，实际进入后端独立事件主题。
- 真实WebRTC播放的requested/connected/paused/resumed/seeked/closed及recording.started/completed被消费。
- 连接期间active_connections=1，正常结束后为0；连接总时长与分钟桶可查询。
- 新消费组回放到原统计库，event_id去重使计数保持不变。
- 新消费组写入新统计库，可从Kafka历史数据重建播放统计。
- Kafka地址不可用时真实播放和录制仍成功，事件留在本地持久发件箱；重启并恢复broker后补发并形成统计。
- 原上传/Redis/MySQL/Agent WS/媒体收流录制集成测试通过。
- Agent28项单测、负载策略单测、Web构建和Compose配置校验通过。

播放统计主题avplayer.playback.events与MCP任务主题avplayer.media.events分开；RabbitMQ仍只分发耗时任务。
本地SQLite为单机统计投影，不宣称全局多机聚合；分钟统计为消费后更新，活跃数依赖5秒心跳和30秒过期估算。
连接时长含暂停时间，不是观看时长。未验证云端/浏览器QoE/容器运行或生产高可用。

复现：AVPLAYER_KAFKA_BROKERS=127.0.0.1:19092 .venv/bin/python tests/playback_kafka.py。
