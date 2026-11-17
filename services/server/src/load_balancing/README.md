# 实例选择策略

`LeastSessionsPolicy` 只从调用方给出的健康候选实例中选择活跃会话数最少者；并列时轮换选择，避免固定偏向列表首项。

ZooKeeper 的连接、Watch 和实例列表由 `../discovery/zookeeper/` 负责。此策略不连接 ZooKeeper，也不发起 RPC；实际路由由 `../rpc/MediaRouter` 完成。单元测试在 `services/server/tests/`。
