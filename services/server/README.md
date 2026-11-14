# 统一 C++ 后端：代码模块说明

所有模块链接为一个 `avplayer_server` 进程。模块化用于约束代码职责，不拆成额外网络服务。

## 源码结构

```text
services/server/
├── CMakeLists.txt
├── tests/LeastSessionsPolicyTest.cpp
└── src/
    ├── main.cpp                     # 仅创建并运行应用
    ├── app/
    │   └── ServerApplication.*       # 创建服务、注册gRPC、组合模块
    ├── config/
    │   └── Environment.h             # 环境配置读取
    ├── network/
    │   ├── common/
    │   │   ├── NetworkSupport.h      # 网络层别名和gRPC桥接辅助
    │   │   └── Url.cpp               # URL解码
    │   ├── http/
    │   │   ├── HttpServer.*          # Asio监听、连接接入和工作池
    │   │   └── HttpSession.*         # HTTP路由、分块上传与文件下载
    │   └── websocket/
    │       └── WebSocketSession.*    # 信令/Agent双向流、发送队列与取消
    ├── rpc/
    │   ├── UploadService.*           # 上传gRPC、写文件、媒体绑定
    │   ├── StorageService.*          # 数据操作校验及分发，不含SQL或Redis命令
    │   └── MediaRouter.*             # 播放会话路由、固定后端、断连清理
    ├── storage/
    │   ├── redis/RedisCache.*        # Redis连接、TTL字幕缓存和缓存键
    │   └── mysql/MySqlRepository.*   # MySQL连接、建表、查询及字幕事务
    ├── discovery/
    │   └── zookeeper/Registry.*      # 后端注册发现、Watch与恢复
    └── load_balancing/
        └── LeastSessionsPolicy.*     # 纯选择策略：最少会话、同负载轮换
```

后端关联模块：

- `services/media`：`avplayer_media_service`，媒体会话、FFmpeg编解码、WebRTC和录制。
- `libs/media-core`：共享媒体基础代码。
- `src/discovery/zookeeper`：后端内部注册发现模块，构建为server_discovery，不依赖外部共享库。
- `proto`：gRPC接口契约。

## 模块依赖

```text
main → ServerApplication
         ├→ network HTTP/WS → RPC适配
         ├→ UploadService
         ├→ StorageService → RedisCache / MySqlRepository
         ├→ MediaRouter → LeastSessionsPolicy + ZooKeeper Registry
         └→ avplayer_media_service → media-core / FFmpeg / WebRTC
```

CMake同样分成 `server_network`、`server_rpc`、`server_load_balancing`、`server_redis`、`server_mysql` 静态库，最后链接成同一个可执行程序。
数据库驱动类型通过PImpl封装，hiredis/MySQL头文件只在对应实现文件出现，不扩散到网络层。
Redis和MySQL分别持有自己的锁，数据RPC不再用一个全局锁串行化两种存储访问。

## 职责边界

- HTTP/WS层处理协议和连接，不实现SQL、Redis命令或负载策略。
- RPC层将请求映射到对应服务，MediaRouter管理会话生命周期。
- LeastSessionsPolicy只选实例，不访问ZooKeeper，不持有会话，也不发网络请求。调用方在路由锁下完成选择与预留。
- ZooKeeper只发布实例集合；已有会话绑定由MediaRouter管理。
- MySqlRepository保存会话、媒体、字幕和任务数据；RedisCache只负责字幕缓存。
- Agent工具实现属于thirdParty/MCP，不在这些模块里重复实现。

## 构建验证

```sh
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug --target avplayer_server server_policy_test streaming_smoke_client -j4
ctest --test-dir cmake-build-debug -R server_least_sessions_policy --output-on-failure
.venv/bin/python tests/cpp_gateway_integration.py
```

实际进程入口和端口不变，仍用 `scripts/dev.sh` 启动。
同一个进程：HTTP/WS50065，上传/数据/媒体路由gRPC50066，内部媒体gRPC50062。
环境变量及启动步骤见根STARTUP.md，ZooKeeper多实例配置见deploy/ZOOKEEPER.md。

## 保留的实现边界

HTTP/WS到服务仍通过本进程gRPC桥接，单实例MediaRouter直接调用进程内媒体实现。
数据库连接沿用启动时建立、故障降级机制，没有在此次整理中增加连接池、自动重连或全异步SQL。
负载计数仍为当前入口实例本地统计，未增加跨网关共享路由或无缝迁移。
