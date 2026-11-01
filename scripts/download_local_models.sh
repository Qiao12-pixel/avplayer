#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OLLAMA_MODELS="$PROJECT_ROOT/resource/ollama"
WHISPER_MODELS="$PROJECT_ROOT/resource/whisper-small"
OLLAMA_HOST="127.0.0.1:11435"
mkdir -p "$OLLAMA_MODELS" "$WHISPER_MODELS"

if ! command -v ollama >/dev/null 2>&1; then
  echo "Install Ollama first: https://ollama.com/download" >&2
  exit 1
fi
if [[ ! -x "$PROJECT_ROOT/.venv/bin/python" ]]; then
  python3 -m venv "$PROJECT_ROOT/.venv"
fi
"$PROJECT_ROOT/.venv/bin/python" -m pip install -r "$PROJECT_ROOT/agent/requirements.txt"

if ! OLLAMA_HOST="http://$OLLAMA_HOST" curl -fsS "http://$OLLAMA_HOST/api/tags" >/dev/null 2>&1; then
  OLLAMA_HOST="$OLLAMA_HOST" OLLAMA_MODELS="$OLLAMA_MODELS" ollama serve \
    >"$PROJECT_ROOT/resource/ollama/ollama.log" 2>&1 &
  OLLAMA_PID=$!
  trap 'kill "$OLLAMA_PID" 2>/dev/null || true' EXIT
  for _ in $(seq 1 50); do
    if curl -fsS "http://$OLLAMA_HOST/api/tags" >/dev/null 2>&1; then break; fi
    sleep 0.2
  done
fi

echo "Downloading qwen3:4b into $OLLAMA_MODELS"
if OLLAMA_HOST="http://$OLLAMA_HOST" OLLAMA_MODELS="$OLLAMA_MODELS" ollama show qwen3:4b >/dev/null 2>&1; then
  echo "Qwen3:4b is already installed."
elif ! OLLAMA_HOST="http://$OLLAMA_HOST" OLLAMA_MODELS="$OLLAMA_MODELS" ollama pull qwen3:4b; then
  echo "Ollama registry download failed; trying the official Qwen GGUF repository."
  MODEL_FILE="$OLLAMA_MODELS/Qwen3-4B-Q4_K_M.gguf"
  EXPECTED_SHA='7485fe6f11af29433bc51cab58009521f205840f5b4ae3a32fa7f92e8534fdf5'
  ACTUAL_SHA="$(shasum -a 256 "$MODEL_FILE" 2>/dev/null | awk '{print $1}')"
  if [[ "$ACTUAL_SHA" != "$EXPECTED_SHA" ]]; then
    curl -L --fail --retry 3 --continue-at - \
      -o "$MODEL_FILE" \
      'https://huggingface.co/Qwen/Qwen3-4B-GGUF/resolve/main/Qwen3-4B-Q4_K_M.gguf'
    ACTUAL_SHA="$(shasum -a 256 "$MODEL_FILE" | awk '{print $1}')"
  fi
  if [[ "$ACTUAL_SHA" != "$EXPECTED_SHA" ]]; then
    echo "Model checksum mismatch; expected $EXPECTED_SHA, got $ACTUAL_SHA" >&2
    exit 1
  fi
  printf 'FROM %s\n' "$MODEL_FILE" > "$OLLAMA_MODELS/Qwen3-4B.Modelfile"
  OLLAMA_HOST="http://$OLLAMA_HOST" OLLAMA_MODELS="$OLLAMA_MODELS" \
    ollama create qwen3:4b -f "$OLLAMA_MODELS/Qwen3-4B.Modelfile"
fi

echo "Downloading faster-whisper small into $WHISPER_MODELS"
PROJECT_ROOT="$PROJECT_ROOT" "$PROJECT_ROOT/.venv/bin/python" - <<'PY'
from pathlib import Path
import os
from faster_whisper import WhisperModel

root = Path(os.environ["PROJECT_ROOT"])
WhisperModel("small", download_root=str(root / "resource" / "whisper-small"),
             device="cpu", compute_type="int8")
print("Whisper model is ready")
PY

echo "Local models are ready under resource/."
