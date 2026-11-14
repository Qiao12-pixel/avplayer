# 服务发现模块

`Registry.*` 封装ZooKeeper多线程C客户端，作为后端内部静态库server_discovery链接到统一进程。
只负责临时节点注册、Watch与列表核对、断连/会话过期后的重连注册，不负责负载均衡或业务会话分配。
对外不暴露ZooKeeper原生句柄；网络I/O线程不直接调用ZooKeeper API。
具体负载选择位于services/server/src/load_balancing，会话绑定位于services/server/src/rpc/MediaRouter。
变量保持AVPLAYER_ZK_HOSTS、AVPLAYER_ZK_MEDIA_PATH、AVPLAYER_MEDIA_ADVERTISE兼容。
