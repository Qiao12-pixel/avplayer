# C++ 网络与数据服务

唯一的浏览器接入、媒体上传及 Redis/MySQL 实现。Python Agent 不再提供 HTTP、WebSocket、上传服务或直接访问数据库。

- `:50065`：Boost.Asio + Beast HTTP/WebSocket，兼容 `/signal`、`/agent`、`/control`、`/upload`、`/download`、`/config`、`/health`。
- `:50066`：内部 gRPC `MediaService.UploadMedia` 与 `StorageService.Execute`，供 Qt、网关和 Python Agent 使用。
- Python `:50061` 只提供 `AgentService`，包括上传完成后的媒体绑定 RPC。
- C++ 媒体服务 `:50062` 继续负责 WebRTC 音视频与播放控制。

HTTP 以 64 KiB 缓冲转发上传，下载使用 Beast file_body。WebSocket 在 strand 上异步收发，每个连接仅一个写操作在途，发送队列上限 4 MiB；gRPC 阻塞写操作运行于工作池，连接断开时取消对应调用。当前每条双向流使用一个专用 gRPC 读取线程，不能将此实现宣称为全异步 gRPC。

字幕读取仍为 Redis→文件→MySQL；Python 仅保留 RPC 适配器，直接数据库驱动仅在 C++。MySQL 字幕和片段替换在事务内完成；数据库与 Redis 不可用时允许落盘流程继续，不保证跨存储原子一致性。文件与字幕工具也由 C++ 执行，Agent 只发起 RPC。

## 构建

依赖 Boost（Asio/Beast）、nlohmann-json、gRPC、Protobuf、hiredis、MySQL/MariaDB C 客户端。

```sh
cmake -S services/gateway -B build/gateway -DCMAKE_BUILD_TYPE=Debug
cmake --build build/gateway -j4
# 或使用根 CMake：cmake --build cmake-build-debug --target web_gateway
```

```sh
AVPLAYER_AGENT_SERVER=localhost:50061 \
AVPLAYER_MEDIA_SERVER=localhost:50062 \
AVPLAYER_DATA_SERVER=localhost:50066 \
AVPLAYER_DATA_SERVER_ADDRESS=127.0.0.1:50066 \
AVPLAYER_STORAGE_ROOT="$PWD/storage" \
./build/gateway/web_gateway
```

Redis 设置 `AVPLAYER_REDIS_URL=redis://127.0.0.1:6379/0`；MySQL 设置 `AVPLAYER_MYSQL_DSN=mysql://user:password@host:3306/database`，数据库须预先创建。当前 DSN 使用简单 user/password/host 形式；Redis URL 暂不支持认证/TLS。服务启动时尝试连接，恢复数据库连接需要重启服务。

`AVPLAYER_WEB_GATEWAY_HOST/PORT` 默认 `127.0.0.1/50065`，`AVPLAYER_DATA_SERVER_ADDRESS` 默认 `127.0.0.1:50066`。Qt 上传和 Python 内部存储 RPC 均通过 `AVPLAYER_DATA_SERVER` 连接，不再指向 Agent 端口。内部 gRPC 没有鉴权，应仅在可信内部网络使用；HTTPS 仍由前置代理终止。

## 验证

`tests/cpp_gateway_integration.py` 启动隔离端口上的 Agent、C++ 网关及媒体服务，验证真实 Redis/MySQL、上传下载、Agent WebSocket 和经 WebSocket 网关的真实 WebRTC 收流。需要已构建的媒体测试客户端和本机数据库，测试会写入 `cpp-migration` 会话。

## ZooKeeper 与会话负载均衡

设置 `AVPLAYER_ZK_HOSTS=127.0.0.1:2181` 后启用发现；默认路径 `AVPLAYER_ZK_MEDIA_PATH=/avplayer/media`。
每个媒体实例必须设置网关可达的 `AVPLAYER_MEDIA_ADVERTISE=host:port`。注册使用临时顺序节点，节点数据为实例 gRPC 地址。

网关的内部 `:50066` 现在也实现 StreamingService：

- 首条 offer 按本网关活跃/协商中会话数最少分配后端，相同负载轮换选择。
- 会话由 `(session_id, media_id)` 唯一标识，后续 ICE 和 Control 始终发往原后端。
- 同名活跃会话返回 ALREADY_EXISTS，断开或后端失效后清理路由。
- ZooKeeper Watch 更新实例列表，并每约 2 秒核对；请求线程不访问 ZooKeeper。
- 实例强制退出后，临时节点在 ZooKeeper 会话过期时摘除，不保证瞬时故障检测。
- ZooKeeper 断连时拒绝新会话，已有路由仍可控制；会话过期后重新连接并注册。
- 开启发现但列表为空时返回 UNAVAILABLE，不回退到单实例。未设置 ZK_HOSTS 时沿用 AVPLAYER_MEDIA_SERVER 静态目标。
- `/health` 的 routing 字段提供发现状态、后端列表及各实例本地会话计数；gRPC 初始 metadata `x-avplayer-media-backend` 可验证路由。

Web 无需改接口，网关内部会将 `/signal`、`/control` 转到本进程的路由服务。
Qt 拉流默认连接 `localhost:50066`；连接远程集群时设置 `AVPLAYER_MEDIA_SERVER=gateway-host:50066`。
直接连接媒体实例的 50062 仍可用于调试，但会绕过负载均衡，不能经网关控制该直连会话。

这是一层网关内的媒体会话调度，不是跨网关全局调度：路由表及计数驻留单网关内存。
多网关部署须将同一播放会话的信令和控制请求固定到同一个网关；网关重启需要重新协商。
Agent Runtime 与 Redis/MySQL 不由 ZooKeeper 均衡。共享解码、多网关路由同步、实例业务探活、自动迁移、CPU 权重和容量配额尚未实现。
注册采用开放 ACL，仅供可信内网开发；生产使用前需补充 ZooKeeper ACL/认证。当前发现注册不是健康探针，端口监听不代表业务无阻塞。



## Agent工具边界

Agent媒体工具已独立到MCP，本服务不再提供CallTool或WorkerService；保留上传、数据接口及StreamingService媒体会话均衡。
