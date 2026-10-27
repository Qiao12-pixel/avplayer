"""JSON Lines protocol shared by the Python Agent Runtime and Qt client."""

from __future__ import annotations

import json
import sys
from typing import Any, TextIO

PROTOCOL = "avplayer.agent.v1"


def message(message_type: str, request_id: str, session_id: str = "", **fields: Any) -> dict[str, Any]:
    payload = dict(fields.pop("payload", {}))
    result = {
        "protocol": PROTOCOL,
        "type": message_type,
        "request_id": request_id,
        "session_id": session_id,
    }
    for key in ("tool", "arguments"):
        if key in fields:
            result[key] = fields.pop(key)
    payload.update(fields)
    result["payload"] = payload
    return result


def emit(payload: dict[str, Any], output: TextIO = sys.stdout) -> None:
    output.write(json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n")
    output.flush()


def read_messages(input_stream: TextIO = sys.stdin):
    for line in input_stream:
        line = line.strip()
        if not line:
            continue
        try:
            payload = json.loads(line)
        except json.JSONDecodeError as exc:
            yield None, f"invalid JSON: {exc}"
            continue
        if payload.get("protocol") != PROTOCOL:
            yield None, "unsupported protocol"
            continue
        yield payload, None
