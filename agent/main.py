"""Local Agent Runtime process for AVPlayer.

Run with: python3 agent/main.py
Communication uses one JSON object per line on stdin/stdout.
"""

from __future__ import annotations

import sys

from protocol import emit, message, read_messages
from runtime import AgentRuntime


def main() -> int:
    runtime = AgentRuntime(sys.stdout)
    emit(message("ready", "runtime", capabilities=["goal", "task", "tool_result"],
                 model_configured=bool(runtime.config.get("doubao_api_key") and runtime.config.get("doubao_model")),
                 speech_configured=bool(runtime.config.get("doubao_speech_api_key")),
                 config_status="Python Runtime 已读取本地配置（密钥尚未验证）。"))
    for payload, error in read_messages():
        if error:
            emit(message("error", "protocol", error=error))
            continue
        runtime.handle(payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
