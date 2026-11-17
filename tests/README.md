# 测试入口

- services/agent/tests：Agent编排、MCP调用、身份隔离和数据RPC适配。
- cpp_gateway_integration.py：上传下载、数据库、WebSocket、真实WebRTC播放录制。
- mcp_independent_agent.py：只启动Agent+MCP验证真实转写和字幕保存。
- zookeeper_load_balancing.py / zookeeper_recovery.py：媒体会话多实例及故障恢复。
- MCP仓库tests/media_standalone.py：原生媒体工具和本地任务队列，无需AVPlayer服务。

AVPlayer专属Worker和旧工具实现测试已随重复实现删除。

网关与媒体已合并：cpp_gateway_integration只启动一个avplayer_server。ZooKeeper分流测试的每个候选均为完整avplayer_server，而非独立media_server。

`two_web_sessions.py`：两个独立页面身份同时上传不同视频，经HTTP/WS与真实WebRTC收流，验证暂停/Seek互不影响、Agent身份隔离及两路录制。使用51061/51062/51065/51066及临时storage，不启动Qt。
协议集成测试不代替浏览器画面和声音的人工验收。

result_persistence.py验证真实Redis/MySQL结果快照、跨重启回填、同媒体复用以及真实转写/摘要自动保存，使用独立results-*会话。

`node tests/weather_widget.mjs`验证天气组件10秒轮询、不重叠请求、隐藏页面暂停/恢复和销毁清理。
本轮真实MCP get_weather→Open-Meteo调用已返回天气数据；外部天气服务可用性不作为稳定CI断言。

messaging_integration.py：真实RabbitMQ/Kafka，验证持久任务、broker中断恢复、重复投递、消费者退出重投、死信、Kafka审计与回放。
媒体工具/Agent集成测试现在需要broker；生产实现是C++，pika只用于测试。详见MESSAGING.md。

playback_kafka.py验证真实WebRTC事件→后端Kafka→统计投影、活跃连接、Seek、回放去重、新库重建及Kafka离线时播放继续与发件箱恢复。
需外部Kafka，可设置AVPLAYER_KAFKA_BROKERS=127.0.0.1:19092；脚本使用51762/51765/51766、临时storage/outbox/stats和独立主题。

MySQL迁移后，mysql_shared_integration.py替代旧SQLite消息测试；messaging_integration.py为兼容入口。所有启动后端/MCP的集成测试需要同一AVPLAYER_MYSQL_DSN，playback_kafka.py用独立projection验证回放。mysql_lease_test验证数据库时间租约和版本栅栏。
