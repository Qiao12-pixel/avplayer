# C++后端模块整理验证（2026-11-14）

统一进程、监听端口、客户端API保持不变。入口缩减为main→ServerApplication。

代码拆分：network/http、network/websocket、rpc、storage/redis、storage/mysql、load_balancing、config/app。
ZooKeeper实现位于services/server/src/discovery/zookeeper，媒体实现仍为services/media内部静态库。
Redis/MySQL驱动使用PImpl封装，各自持锁；网络和RPC实现不包含驱动头文件或命令。

通过：
- avplayer_server与server_policy_test构建。
- 最少会话策略测试：2/2分配、较低负载选择、成员变化、空列表拒绝。
- cpp_gateway_integration：真实Redis/MySQL、HTTP上传下载、Agent WS、H.264/Opus WebRTC和录制。
- zookeeper_load_balancing：4路分2/2、粘性控制、重复拒绝、退出清理、SIGKILL摘除和重启。
- zookeeper_recovery：中断期间既有播放、重连与过期恢复。
- Python25项单测、Web构建、Compose配置校验。

多实例与ZooKeeper恢复测试有重叠端口，需要顺序运行；首次并行发生端口冲突，顺序重跑通过。
本次没有重新验证浏览器UI、云端模型或容器运行；不增加连接池、全异步gRPC或新故障恢复承诺。
