# 部署

```sh
cp deploy/.env.example deploy/.env
docker compose -f deploy/docker-compose.yml config
docker compose -f deploy/docker-compose.yml up --build
```

agent.Dockerfile 只启动 Python Agent；gateway.Dockerfile 提供 C++ Asio/Beast HTTP/WS、上传和 Redis/MySQL；media.Dockerfile 提供 WebRTC 音视频；web.Dockerfile 提供 nginx 静态站点和反向代理。
内部数据服务端口 50066；Agent 50061；media 50062；HTTP 网关 50065；Web 8080。
gateway、Agent、media 挂载同一 storage；MySQL/Redis 由 gateway 访问。数据库连接当前在启动时建立，数据库未就绪会进入降级状态，需要重启 gateway 后恢复。
镜像配置已迁移，本地集成测试通过；容器构建与跨主机 WebRTC 尚需在目标环境验证。

ZooKeeper 可选 profile 与本机双实例验证见 [ZOOKEEPER.md](ZOOKEEPER.md)。

Agent 的模型工具现在需要外部自建 C++ MCP 服务。Compose 通过 AVPLAYER_MCP_URL 指向它（默认宿主机8081）；本仓库不包含相邻 MCP 仓库的镜像。
MCP 服务的 AVPLAYER_DATA_SERVER 须设为它可达的网关50066，宿主机运行时默认 localhost:50066 即可。
未启动 MCP 时上传与播放仍可使用，但 AI 媒体工具会明确失败。完整容器链路尚未验证。
