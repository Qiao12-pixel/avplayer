# 测试入口

- services/agent/tests：Agent编排、MCP调用、身份隔离和数据RPC适配。
- cpp_gateway_integration.py：上传下载、数据库、WebSocket、真实WebRTC播放录制。
- mcp_independent_agent.py：只启动Agent+MCP验证真实转写和字幕保存。
- zookeeper_load_balancing.py / zookeeper_recovery.py：媒体会话多实例及故障恢复。
- MCP仓库tests/media_standalone.py：原生媒体工具和本地任务队列，无需AVPlayer服务。

AVPlayer专属Worker和旧工具实现测试已随重复实现删除。
