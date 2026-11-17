# 部署

统一C++程序avplayer_server使用server.Dockerfile，一个server容器包含HTTP/WS、数据和媒体能力。
agent.Dockerfile仅运行Python编排；外部MCP通过AVPLAYER_MCP_URL连接，并共享媒体存储。
nginx将HTTP/WS请求反向代理至server:50065。

```sh
cp deploy/.env.example deploy/.env
docker compose -f deploy/docker-compose.yml config --quiet
docker compose -f deploy/docker-compose.yml up --build
```

已校验Compose结构，镜像运行及跨主机WebRTC未做本轮实机验证。
ZooKeeper完整实例配置见ZOOKEEPER.md。

RabbitMQ/Kafka开发broker可单独启动：`docker compose -f deploy/docker-compose.yml up -d rabbitmq kafka`。
宿主机MCP运行时Rabbit用户/密码需与.env一致；Kafka advertised地址必须是客户端可达地址。
broker持久卷已配置，单节点不是生产高可用集群；本轮真实验证使用本机安装的隔离broker，未运行Docker镜像。
