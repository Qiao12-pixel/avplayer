"""Minimal stdlib client for the Ark Chat API used by the local Agent Runtime."""
from __future__ import annotations
import json
import urllib.request
from pathlib import Path

def load_flat_config(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.is_file(): return values
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or ":" not in line: continue
        key, value = line.split(":", 1)
        values[key.strip()] = value.strip().split(" #", 1)[0].strip().strip('"\'')
    return values

class ArkChatClient:
    def __init__(self, config_path: Path) -> None:
        config = load_flat_config(config_path)
        self.api_key = config.get("doubao_api_key", "")
        self.model = config.get("doubao_model", "")
        self.endpoint = config.get("doubao_chat_endpoint", "https://ark.cn-beijing.volces.com/api/v3/chat/completions")

    def chat(self, messages: list[dict], tools: list[dict]) -> str:
        if not self.api_key or not self.model: raise RuntimeError("doubao_api_key 或 doubao_model 未配置")
        body = json.dumps({"model": self.model, "messages": messages, "temperature": 0.1, "stream": False}, ensure_ascii=False).encode("utf-8")
        request = urllib.request.Request(self.endpoint, data=body, method="POST", headers={"Authorization": f"Bearer {self.api_key}", "Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=90) as response:
            return json.loads(response.read().decode("utf-8"))["choices"][0]["message"]["content"]
