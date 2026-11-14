# ZooKeeper + 媒体会话负载均衡

C++ gateway 与媒体服务依赖 ZooKeeper 多线程 C 客户端（macOS `zookeeper`，Debian/Ubuntu `libzookeeper-mt-dev`）。

## 本机验证两实例

先启动本地 ZooKeeper（默认 2181），编译 gateway/media/smoke client，然后在不同终端执行：

```sh
AVPLAYER_ZK_HOSTS=127.0.0.1:2181 \
AVPLAYER_MEDIA_ADVERTISE=127.0.0.1:50062 \
AVPLAYER_MEDIA_SERVER_ADDRESS=127.0.0.1:50062 \
AVPLAYER_STORAGE_ROOT="$PWD/storage" \
./cmake-build-debug/services/media/media_server
```

```sh
AVPLAYER_ZK_HOSTS=127.0.0.1:2181 \
AVPLAYER_MEDIA_ADVERTISE=127.0.0.1:50072 \
AVPLAYER_MEDIA_SERVER_ADDRESS=127.0.0.1:50072 \
AVPLAYER_STORAGE_ROOT="$PWD/storage" \
./cmake-build-debug/services/media/media_server
```

```sh
AVPLAYER_ZK_HOSTS=127.0.0.1:2181 \
AVPLAYER_DATA_SERVER=localhost:50066 \
AVPLAYER_STORAGE_ROOT="$PWD/storage" \
./cmake-build-debug/services/gateway/web_gateway
```

Agent 仍按 STARTUP.md 启动在 50061，Web 的现有端口不变。
用不同 session/media 打开多个播放会话；`curl http://127.0.0.1:50065/health` 查看分配计数。
同一 session/media 不支持两个独立播放会话同时占用；多个浏览器测试请使用不同 session。

## 自动验证

```sh
# 使用专用 ZooKeeper 端口，避免修改开发服务；此脚本不启动 ZooKeeper。
AVPLAYER_TEST_ZK=127.0.0.1:52181 .venv/bin/python tests/zookeeper_load_balancing.py
# 自行启动临时 ZooKeeper，停止并重启它，验证会话超时恢复。
ZK_SERVER=/path/to/zkServer.sh .venv/bin/python tests/zookeeper_recovery.py
```

负载测试启动隔离 gateway 50465/50466、media 50462/50463，使用临时 storage 和真实 WebRTC 接收端。
验证四路分配为 2/2、控制粘性、重复拒绝、断连回收、SIGKILL 后摘除、全部后端离线和重新上线。
恢复测试使用独立 ZooKeeper 52182 和服务端口 50562/50565/50566；不会停止现有 ZooKeeper。

## Compose

```sh
AVPLAYER_ZK_HOSTS=zookeeper:2181 \
docker compose -f deploy/docker-compose.yml --profile discovery up --build
```

Compose 默认仍是一个媒体实例；profile 启动一个开发用单节点 ZooKeeper。
增加实例时必须分配不同的可达 advertised 地址、避免宿主机端口冲突，并共享 storage。
不能直接复制同一个 AVPLAYER_MEDIA_ADVERTISE 后宣称有多个可路由实例。
容器镜像与跨主机 WebRTC 未在本轮执行验证；此处仅提供配置。

ZooKeeper 不转发音视频。网关仅均衡新会话及转发信令/控制，WebRTC 仍直连被选中的媒体实例。
参见 services/gateway/README.md 中的单网关范围和生产限制。
