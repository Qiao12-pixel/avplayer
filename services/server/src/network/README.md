# 网络接入

该模块处理客户端连接与协议边界。`http/` 提供 Boost.Asio HTTP server、路由和文件/接口响应；`websocket/` 管理信令及双向消息连接；`common/` 放 URL 和网络辅助代码。

这里解析请求并调用 `rpc/` 或 MCP integration，不直接操作 MySQL、Redis，也不实现媒体会话。服务装配入口在 `../app/ServerApplication.cpp`。
