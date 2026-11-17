# MCP 入口与工具注册

`mcp_server_main.cpp` 是可执行程序入口，`server_app.*` 负责读取配置、构造 HTTP/JSON-RPC 服务并组装工具与后台发布/消费组件。

`mcp_builtin_tools.*` 实现通用工具，`mcp_avplayer_tools.*` 注册 AVPlayer 媒体工具，`mcp_job_agent_tools.*` 和 `mcp_redis_tools.*` 提供对应扩展。工具定义在此层，耗时媒体执行由 `../media/` 负责，任务状态由 `../messaging/` 负责。
