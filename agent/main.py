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
    emit(message("ready", "runtime", capabilities=["goal", "tool_result"], tools=[]))
    for payload, error in read_messages():
        if error:
            emit(message("error", "protocol", error=error))
            continue
        runtime.handle(payload, lambda result: emit(message(
            "status", result.get("request_id", ""), result.get("session_id", ""),
            status="tool_result_received", tool=result.get("tool", ""))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
