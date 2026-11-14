# 统一C++后端验证（2026-11-14）

网关与media_server合并为avplayer_server，内部媒体静态库不再有独立main。
本轮构建avplayer_server、Web、streaming_smoke_client；未启动Qt客户端。

已通过：
- 单进程HTTP上传下载、Redis/MySQL、Agent WebSocket、真实WebRTC H.264/Opus接收、播放控制、录制。
- scripts/dev.sh --no-web启动一个C++后端、Agent、MCP；独立Vite经50965代理完整流程。
- 转写17条字幕、摘要、搜索、智能剪辑及产物下载、360p转码下载、自然语言Agent真实调用MCP导出片段。
- 两个完整后端实例承接4路真实播放，分配2/2；粘性控制、重复拒绝、断连回收、实例SIGKILL和重启。
- ZooKeeper故障期间既有播放继续，恢复后发现和重新注册。
- Python 25项回归测试；Web生产构建；Compose discovery配置校验。

端口50065/50066/50062均由同一个C++进程监听，保留旧端口与环境变量以兼容调用方。
单实例路由直接调用进程内StreamingService，Web适配仍使用本机gRPC桥接；集群模式可转发到其他完整实例。

未覆盖：浏览器渲染/音频UI、Qt实机启动、容器运行和公网长稳压测。
旧build目录可能仍有历史web_gateway/media_server产物，启动时不要使用它们；新脚本只启动avplayer_server。

可复现脚本：tests/cpp_gateway_integration.py、tests/full_stack_no_qt.py、tests/zookeeper_load_balancing.py、tests/zookeeper_recovery.py。
完整流程原始结果见VALIDATION_UNIFIED_SERVER.json。
