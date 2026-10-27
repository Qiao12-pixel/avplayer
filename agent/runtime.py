"""Agent orchestration boundary.

This first runtime owns protocol and task lifecycle. Model planning and concrete
tool adapters can be added without changing the Qt process protocol.
"""

from __future__ import annotations

import uuid
import json
import os
from pathlib import Path
from typing import Any, Callable

from protocol import emit, message
from ark_client import ArkChatClient


class AgentRuntime:
    def __init__(self, output: Any) -> None:
        self.output = output
        self.tools: list[dict[str, Any]] = []
        root = Path(os.environ.get("AVPLAYER_PROJECT_ROOT", Path.cwd()))
        self.client = ArkChatClient(root / "config" / "secret_key.yaml")
        self.request_id = ""
        self.session_id = ""
        self.messages: list[dict[str, Any]] = []

    def set_tools(self, tools: list[dict[str, Any]]) -> None:
        self.tools = tools

    def handle(self, payload: dict[str, Any], dispatch: Callable[[dict[str, Any]], None]) -> None:
        message_type = payload.get("type")
        request_id = payload.get("request_id", str(uuid.uuid4()))
        session_id = payload.get("session_id", "")
        if message_type == "goal":
            goal = payload.get("payload", {}).get("goal", "").strip()
            if not goal:
                emit(message("error", request_id, session_id, error="goal is empty"), self.output)
                return
            self.request_id, self.session_id = request_id, session_id
            self.messages = [{"role": "system", "content": "你是 AVPlayer 视频 Agent。只能使用工具完成任务。每次只返回严格 JSON：{\"kind\":\"tool_call\",\"tool\":\"工具名\",\"arguments\":{}}，或 {\"kind\":\"final\",\"text\":\"最终答复\"}。随机导出片段必须调用 export_random_clip，duration_ms 是用户要求的片段长度（毫秒）。只有工具返回 success=true 才能宣称操作完成，最终答复要包含输出文件路径。"}, {"role": "user", "content": goal}]
            emit(message("status", request_id, session_id, status="planning", goal=goal), self.output)
            self.advance()
            return
        if message_type == "tool_catalog":
            self.set_tools(payload.get("payload", {}).get("tools", []))
            emit(message("status", request_id, session_id,
                         status="tool_catalog_updated", tool_count=len(self.tools)), self.output)
            return
        if message_type == "tool_result":
            dispatch(payload)
            self.messages.append({"role": "user", "content": "工具 " + str(payload.get("tool", "")) + " 的结果：" + json.dumps(payload.get("payload", {}), ensure_ascii=False)})
            self.advance()
            return
        emit(message("error", request_id, session_id,
                     error=f"unsupported message type: {message_type}"), self.output)

    def advance(self) -> None:
        try:
            messages = list(self.messages)
            messages[0] = {"role": "system", "content": messages[0]["content"] +
                           "\n可用工具定义：" + json.dumps(self.tools, ensure_ascii=False)}
            raw = self.client.chat(messages, self.tools)
            command = json.loads(raw.strip().removeprefix("```json").removesuffix("```").strip())
            self.messages.append({"role": "assistant", "content": raw})
            if command.get("kind") == "tool_call":
                emit(message("tool_call", str(uuid.uuid4()), self.session_id, tool=command.get("tool", ""), arguments=command.get("arguments", {}), payload={"parent_request_id": self.request_id}), self.output)
            elif command.get("kind") == "final":
                emit(message("final", self.request_id, self.session_id, payload={"text": command.get("text", "")}), self.output)
            else:
                emit(message("error", self.request_id, self.session_id, error="模型返回了未知 Agent 指令。"), self.output)
        except Exception as exc:
            emit(message("error", self.request_id or str(uuid.uuid4()), self.session_id, error=str(exc)), self.output)
