#!/usr/bin/env bash
# Single local entry point for the agent service, Web gateway, media service
# and Vite Web client. Docker/Compose is available in deploy/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PYTHON="${PYTHON:-$ROOT/.venv/bin/python}"
BUILD_DIR="${AVPLAYER_BUILD_DIR:-$ROOT/cmake-build-debug}"

export AVPLAYER_PROJECT_ROOT="$ROOT"
export AVPLAYER_STORAGE_ROOT="${AVPLAYER_STORAGE_ROOT:-$ROOT/storage}"
export AVPLAYER_AGENT_PORT="${AVPLAYER_AGENT_PORT:-50061}"
export AVPLAYER_AGENT_BIND="${AVPLAYER_AGENT_BIND:-127.0.0.1}"
export AVPLAYER_MEDIA_SERVER="${AVPLAYER_MEDIA_SERVER:-localhost:50062}"
export AVPLAYER_MEDIA_SERVER_ADDRESS="${AVPLAYER_MEDIA_SERVER_ADDRESS:-0.0.0.0:50062}"
export AVPLAYER_WEB_GATEWAY_HOST="${AVPLAYER_WEB_GATEWAY_HOST:-127.0.0.1}"
export AVPLAYER_WEB_GATEWAY_PORT="${AVPLAYER_WEB_GATEWAY_PORT:-50065}"
export AVPLAYER_ICE_SERVERS="${AVPLAYER_ICE_SERVERS:-stun:stun.l.google.com:19302}"
# Optional transcript cache; agent degrades to disk when Redis is absent.
export AVPLAYER_REDIS_URL="${AVPLAYER_REDIS_URL:-redis://127.0.0.1:6379/0}"
# Optional durable store (see STARTUP.md 3.5); empty disables MySQL.
export AVPLAYER_MYSQL_DSN="${AVPLAYER_MYSQL_DSN:-mysql://avplayer:avplayer@127.0.0.1:3306/avplayer}"
export AVPLAYER_ZK_HOSTS="${AVPLAYER_ZK_HOSTS:-}"
export AVPLAYER_ZK_MEDIA_PATH="${AVPLAYER_ZK_MEDIA_PATH:-/avplayer/media}"
export AVPLAYER_MEDIA_ADVERTISE="${AVPLAYER_MEDIA_ADVERTISE:-127.0.0.1:${AVPLAYER_MEDIA_SERVER_ADDRESS##*:}}"
export AVPLAYER_DATA_SERVER="${AVPLAYER_DATA_SERVER:-localhost:50066}"
export AVPLAYER_DATA_SERVER_ADDRESS="${AVPLAYER_DATA_SERVER_ADDRESS:-127.0.0.1:50066}"
export AVPLAYER_WORKER_PYTHON="${AVPLAYER_WORKER_PYTHON:-$PYTHON}"
export AVPLAYER_MCP_ROOT="${AVPLAYER_MCP_ROOT:-$ROOT/thirdParty/MCP}"
export AVPLAYER_MCP_BUILD_DIR="${AVPLAYER_MCP_BUILD_DIR:-$ROOT/build/mcp}"
export AVPLAYER_MCP_BIN="${AVPLAYER_MCP_BIN:-$AVPLAYER_MCP_BUILD_DIR/MCP_Server}"
export MCP_MEDIA_ROOT="${MCP_MEDIA_ROOT:-$AVPLAYER_MCP_ROOT}"
export AVPLAYER_MCP_PORT="${AVPLAYER_MCP_PORT:-8081}"
export AVPLAYER_MCP_URL="${AVPLAYER_MCP_URL:-http://127.0.0.1:$AVPLAYER_MCP_PORT/jsonrpc}"
export PYTHONPATH="$ROOT/services/agent${PYTHONPATH:+:$PYTHONPATH}"

WITH_MCP=1
WITH_MEDIA=1
WITH_WEB=1
for arg in "$@"; do
  case "$arg" in
    --no-mcp) WITH_MCP=0 ;;
    --no-media) WITH_MEDIA=0 ;;
    --no-web) WITH_WEB=0 ;;
    -h|--help)
      echo "Usage: $0 [--no-media] [--no-web] [--no-mcp]"
      exit 0
      ;;
    *)
      echo "Unknown argument: $arg" >&2
      exit 2
      ;;
  esac
done

if [[ ! -x "$PYTHON" ]]; then
  echo "Python virtualenv not found at $PYTHON" >&2
  echo "Create it with: python3 -m venv .venv && .venv/bin/python -m pip install -e services/agent" >&2
  exit 1
fi

# Fail fast instead of starting half the stack on a busy port.
port_listener() {
  lsof -nP -tiTCP:"$1" -sTCP:LISTEN 2>/dev/null | head -1 || true
}

check_ports() {
  local busy=""
  local port label pid
  while read -r port label; do
    [[ -n "$port" ]] || continue
    pid="$(port_listener "$port")"
    if [[ -n "$pid" ]]; then
      busy+="  ${label} 端口 ${port} 已被占用 (pid ${pid})"$'\n'
    fi
  done <<EOF
$AVPLAYER_AGENT_PORT agent
$([[ "$WITH_MCP" == "1" ]] && echo "$AVPLAYER_MCP_PORT mcp")
$AVPLAYER_WEB_GATEWAY_PORT gateway
${AVPLAYER_DATA_SERVER_ADDRESS##*:} data
$([[ "$WITH_MEDIA" == "1" ]] && echo "${AVPLAYER_MEDIA_SERVER_ADDRESS##*:} media")
$([[ "$WITH_WEB" == "1" ]] && echo "5173 web")
EOF
  if [[ -n "$busy" ]]; then
    echo "启动中止：以下端口已被占用，未启动任何服务。" >&2
    printf '%s' "$busy" >&2
    echo "释放后重试，例如：" >&2
    echo "  lsof -ti :50065 | xargs kill" >&2
    exit 1
  fi
}

GATEWAY_BIN="${AVPLAYER_GATEWAY_BIN:-$BUILD_DIR/services/gateway/web_gateway}"
if [[ ! -x "$GATEWAY_BIN" ]]; then
  echo "C++ gateway not built: $GATEWAY_BIN" >&2
  echo "cmake --build $BUILD_DIR --target web_gateway" >&2
  exit 1
fi
if [[ "$WITH_MCP" == "1" && ! -x "$AVPLAYER_MCP_BIN" ]]; then
  echo "MCP binary not built; run scripts/build_mcp.sh or use --no-mcp for an external server" >&2
  exit 1
fi
check_ports

PIDS=""
cleanup() {
  for pid in $PIDS; do
    kill "$pid" 2>/dev/null || true
  done
  wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

if [[ "$WITH_MCP" == "1" ]]; then
  mkdir -p "$ROOT/cache/mcp"
  MCP_CONFIG="${AVPLAYER_MCP_CONFIG:-$AVPLAYER_MCP_ROOT/config/server.json}"
  if [[ ! -f "$MCP_CONFIG" ]]; then
    echo "MCP configuration not found: $MCP_CONFIG" >&2
    exit 1
  fi
  (cd "$ROOT/cache/mcp" && exec "$AVPLAYER_MCP_BIN" --config "$MCP_CONFIG" --host 127.0.0.1 --port "$AVPLAYER_MCP_PORT") &
  PIDS="$PIDS $!"
fi

echo "agent    127.0.0.1:${AVPLAYER_AGENT_PORT}"
"$PYTHON" -m avplayer_agent &
PIDS="$PIDS $!"

echo "gateway  127.0.0.1:${AVPLAYER_WEB_GATEWAY_PORT} -> ${AVPLAYER_MEDIA_SERVER}"
"$GATEWAY_BIN" &
PIDS="$PIDS $!"


if [[ "$WITH_MEDIA" == "1" ]]; then
  MEDIA_BIN="$BUILD_DIR/services/media/media_server"
  if [[ -x "$MEDIA_BIN" ]]; then
    echo "media    ${AVPLAYER_MEDIA_SERVER_ADDRESS}"
    "$MEDIA_BIN" &
    PIDS="$PIDS $!"
  else
    echo "media    skipped: $MEDIA_BIN not built" >&2
    echo "         cmake -S . -B cmake-build-debug && cmake --build cmake-build-debug --target media_server" >&2
  fi
fi

if [[ "$WITH_WEB" == "1" ]]; then
  if [[ ! -d "$ROOT/apps/web/node_modules" ]]; then
    echo "Installing Web client dependencies..."
    npm --prefix "$ROOT/apps/web" install
  fi
  echo "web      Vite dev server (open the HTTPS URL it prints)"
  npm --prefix "$ROOT/apps/web" run dev &
  PIDS="$PIDS $!"
fi

wait
