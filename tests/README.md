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
