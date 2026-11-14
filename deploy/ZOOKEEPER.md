# 统一C++后端的ZooKeeper路由

每个avplayer_server同时提供网络和媒体能力，注册的是完整后端中的媒体端点，不再需要独立media_server。
每个实例须使用独立HTTP、公共gRPC、内部媒体gRPC端口，并共享storage。

实例A：
```sh
AVPLAYER_ZK_HOSTS=127.0.0.1:2181 \
AVPLAYER_MEDIA_ADVERTISE=127.0.0.1:50062 \
AVPLAYER_MEDIA_SERVER_ADDRESS=127.0.0.1:50062 \
AVPLAYER_DATA_SERVER_ADDRESS=127.0.0.1:50066 AVPLAYER_DATA_SERVER=127.0.0.1:50066 \
AVPLAYER_WEB_GATEWAY_PORT=50065 AVPLAYER_STORAGE_ROOT="$PWD/storage" \
./cmake-build-debug/services/server/avplayer_server
```
实例B使用50072/50076/50075对应内部媒体/公共gRPC/HTTP，advertise也改为50072。
Web请求可进入任一实例，新会话按该入口的活跃会话数分配；既有控制必须回到原入口，路由表不是跨实例共享。

`AVPLAYER_SERVER_REGISTER=0`为测试/专用入口选项，默认1；正常完整实例应保持注册。
ZooKeeper故障时停止新分配，已有路由继续工作；过期后重新注册。实例退出需要重新协商，不是无缝迁移。

```sh
AVPLAYER_TEST_ZK=127.0.0.1:52181 .venv/bin/python tests/zookeeper_load_balancing.py
ZK_SERVER=/path/to/zkServer.sh .venv/bin/python tests/zookeeper_recovery.py
```

分流测试启动一个不注册的统一路由入口和两个完整后端，验证4路分流2/2、粘性控制、退出清理与重启。
恢复测试仅启动一个完整后端并中断临时ZooKeeper，验证现有播放与重新发现。
Compose discovery profile配置保留，镜像运行与跨主机WebRTC未在本轮验证。
