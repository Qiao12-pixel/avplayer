# Deployment

This directory is the single operational entry point. All components share
one storage directory and one set of ports:

| Service | Container port | Host port (default) | Contract |
| --- | --- | --- | --- |
| `agent` | 50061 | 50061 | `agent.proto` MediaService + AgentService |
| `media` | 50062 | 50062 | `agent.proto` StreamingService |
| `web-gateway` | 50065 | 50065 | HTTP `/control`, `/upload`, `/download`, `/config`, `/health`; WebSocket `/signal`, `/agent` |
| `web` | 80 | 8080 | static Web client + reverse proxy |
| `redis` | 6379 | 6379 | optional transcript cache |
| `mysql` | 3306 | 3306 | optional sessions / media / transcripts / task log |

`agent` and `media` mount the same storage root: the agent writes uploaded
media to `<storage>/<session_id>/<media_id>/original.*`, and media_server reads
it from there. The browser never talks to gRPC directly; nginx forwards
`/signal`, `/control`, `/upload`, `/download`, `/agent` and `/config` to the
gateway.

`redis` and `mysql` are optional: the agent degrades to disk when either is
missing, and the tables are created on first connect. Pass an explicit
`AVPLAYER_MYSQL_DSN=` / `AVPLAYER_REDIS_URL=` to disable them deliberately.

## Docker Compose

```sh
cp deploy/.env.example deploy/.env      # adjust ports / storage / ICE
docker compose -f deploy/docker-compose.yml up --build
```

Open <http://localhost:8080>, pick a video in the 网络 tab and press
“上传到服务端”; the returned `media_id` is filled in automatically and
“连接并播放” negotiates WebRTC straight away. The Qt client can reuse the same
`session_id` / `media_id` afterwards.

Local models (Ollama) are optional and use a compose profile:

```sh
docker compose -f deploy/docker-compose.yml --profile local up --build
docker compose -f deploy/docker-compose.yml --profile local \
  exec ollama ollama pull qwen3:4b
```

Set `AVPLAYER_AI_PROVIDER=local` in `deploy/.env` and mount a
`resource/local-model.yaml` that points at `AVPLAYER_OLLAMA_HOST`
(default `http://ollama:11434` in the container network).

Doubao/Ark mode reads `config/secret_key.yaml`, which is mounted read-only at
`/app/config`. Keep it out of version control.

## Native development

Full native setup and troubleshooting live in [STARTUP.md](../STARTUP.md).
`scripts/dev.sh` starts the agent, gateway, media server and Vite client
directly from the checkout:

```sh
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug --target media_server
scripts/dev.sh
```

Use `--no-media` or `--no-web` to skip components. The Web client runs on the
Vite HTTPS dev server and proxies to `127.0.0.1:50065`.

## Configuration

All runtime configuration is environment based and read in
`services/agent/avplayer_agent/settings.py` (agent/gateway) or
`services/media/src/main_media_server.cpp` / `MediaSession.cpp` (media):

| Variable | Used by | Default |
| --- | --- | --- |
| `AVPLAYER_PROJECT_ROOT` | agent | current working directory |
| `AVPLAYER_STORAGE_ROOT` | agent, media | `<project>/storage` |
| `AVPLAYER_AGENT_BIND` / `AVPLAYER_AGENT_PORT` | agent | `[::]` / `50061` |
| `AVPLAYER_AGENT_SERVER` | gateway | `localhost:50061` |
| `AVPLAYER_MEDIA_SERVER` | agent, gateway | `localhost:50062` |
| `AVPLAYER_MEDIA_SERVER_ADDRESS` | media | `0.0.0.0:50062` |
| `AVPLAYER_WEB_GATEWAY_HOST` / `_PORT` | gateway | `127.0.0.1` / `50065` |
| `AVPLAYER_ICE_SERVERS` | media, gateway | `stun:stun.l.google.com:19302` |
| `AVPLAYER_AI_PROVIDER` | agent | `local` |
| `AVPLAYER_OLLAMA_HOST` | agent | value from `local-model.yaml` |
| `AVPLAYER_REDIS_URL` | agent | `redis://redis:6379/0` (compose) |
| `AVPLAYER_MYSQL_DSN` | agent | `mysql://avplayer:avplayer@mysql:3306/avplayer` (compose) |

## Known limits

- WebRTC audio uses Opus; the media image installs `libopus-dev`.
- WebRTC inside the default Compose bridge network advertises container IPs,
  which a browser on the host cannot reach. Use `scripts/dev.sh` for local
  streaming, or run `media` with host networking on Linux / provide a TURN
  server reachable from both sides. The Compose file is primarily an
  agent + gateway + static Web deployment reference.
- The media image is build-oriented (it keeps the C++/Qt toolchain); split a
  slim runtime stage before production packaging.
- `AVPLAYER_ICE_SERVERS` accepts URL-only STUN/TURN entries. TURN
  username/credential pairs are not wired through yet.
- Browsers need a secure context for WebRTC. `localhost` works over HTTP;
  remote deployments must terminate HTTPS (and ideally authenticate the
  gateway) in front of the `web` service.
