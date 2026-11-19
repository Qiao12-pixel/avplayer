# gRPC 服务与媒体路由

这里实现服务端的 gRPC 接口适配。`UploadService` 暴露原生 gRPC 分块上传，并提供 HTTP 网关复用的共享上传事务；两种入口都统一执行文件落盘、媒体登记和完成事件。`StorageService` 对 Agent/MCP 暴露媒体、字幕和结果数据能力；`MediaRouter` 将播放请求绑定到本机媒体会话或 ZooKeeper 发现的其他媒体实例，并使用负载策略选目标。

本目录负责将 RPC 请求转换为领域调用，不直接实现 HTTP/WebSocket，也不包含 MySQL SQL 细节。接口契约来自根目录 `proto/`。
