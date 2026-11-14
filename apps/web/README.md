# Web 客户端

Vite + TypeScript，浏览器经 C++ Asio/Beast 网关访问后端：
`/upload` 上传、`/signal` 协商、`/control` 控制、`/agent` 智能体消息、`/download` 产物下载。
实际 H.264/Opus 音视频直接通过 WebRTC 在浏览器与 media_server 之间传输。

```sh
npm --prefix apps/web install
npm --prefix apps/web run dev
npm --prefix apps/web run build
```

后端启动和端口见根目录 STARTUP.md；网关见 services/gateway/README.md。
前端 API 路径不因 Python→C++ 迁移改变，Vite/nginx 的反向代理仍指向 50065。
