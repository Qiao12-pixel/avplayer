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
