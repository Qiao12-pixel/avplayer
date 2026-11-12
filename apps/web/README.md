# AVPlayer Web Stream

An independent browser client for `services/media`. The browser receives the
H.264 track over WebRTC and sends play, pause and seek controls through the
same `StreamingService` used by the native client.

Browsers cannot call the bidirectional gRPC signaling stream directly, so
`services/agent`'s `web_gateway.py` bridges a WebSocket/HTTP endpoint to
`media_server`. Decoding, storage and WebRTC stay in the C++ service; agent
orchestration and uploads stay in `services/agent`.

## Run locally

The simplest option is the repository dev script, which starts the agent,
gateway, media service and Vite together:

```sh
scripts/dev.sh
```

Manual equivalent:

```sh
.venv/bin/python -m pip install -e 'services/agent[dev]'
.venv/bin/avplayer-agent &
.venv/bin/avplayer-web-gateway &
./cmake-build-debug/services/media/media_server &
cd apps/web && npm ci && npm run dev
```

Open the HTTPS URL Vite prints, choose a video and press **Upload to server**;
the returned `media_id` is filled in automatically. Connect to start playback
(H.264 video + Opus audio).

The UI mirrors the Qt client: a player stage, network/AI/Agent tabs, progress
and time, play/pause, server-side recording, screenshot download and
flip/gray/invert filters. The AI tab runs transcription, summary, semantic
search, smart edit and plan export through `services/agent`; the Agent tab
runs natural-language goals and shows the plan/tool/output. Dynamic stickers
are not available in the browser yet.

## Container deployment

`deploy/docker-compose.yml` builds this client into an nginx image and proxies
`/signal`, `/control`, `/upload` and `/config` to the gateway:

```sh
docker compose -f deploy/docker-compose.yml up --build
# http://localhost:8080
```

## Configuration

- Vite proxies `/signal`, `/control`, `/upload`, `/download`, `/config` and
  `/agent` to `AVPLAYER_WEB_GATEWAY_HTTP` (default `http://127.0.0.1:50065`).
- The gateway proxies uploads to `AVPLAYER_AGENT_SERVER` (default
  `localhost:50061`); nginx has no body-size limit for `/upload`.
- The gateway returns `{"ice_servers": [...]}` from
  `AVPLAYER_ICE_SERVERS`; the client loads it at startup and falls back to
  Google STUN for local development.
- `npm run build` type-checks and emits `dist/`.

For playback across networks, configure reachable STUN/TURN servers and open
the media ports; the current env plumbing supports URL-only TURN entries.
