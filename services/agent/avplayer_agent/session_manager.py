"""Per-session AgentRuntime instances for the gRPC server.

The former stdio Runtime assumed one process == one user: a
single worker thread/lock, a single cached LangGraph, a single "active
session" attribute. This module gives each session_id its own AgentRuntime
(and its own outbound message queue) so concurrent clients don't block each
other, while the Ollama subprocess stays shared across sessions to avoid
spawning one local-model server per connection.
"""

from __future__ import annotations

import queue
import threading
from pathlib import Path
from typing import Any

from .db import database
from .local_tools import TOOLS
from .runtime import AgentRuntime
from .storage import SessionStorage


class SessionState:
    def __init__(self, session_id: str, storage: SessionStorage, shared_ollama: "SharedOllama") -> None:
        self.session_id = session_id
        self.storage = storage
        self.active_media_id: str | None = None
        self._subscribers: "list[queue.Queue[Any]]" = []
        self._subscriber_lock = threading.Lock()
        self.runtime = AgentRuntime(output=None, model=shared_ollama.model_for(session_id),
                                     tool_executor=self._execute_tool, sink=self.publish)

    def attach(self) -> "queue.Queue[Any]":
        """Register this connection as a reader of the session stream.

        Every connection gets its own queue and messages are broadcast, so a
        stale socket (HMR reload, second tab, flaky reconnect) can never steal
        a result from the connection that is actually driving the UI. Ejecting
        the oldest reader instead would make two live clients kick each other
        in a reconnect loop.
        """
        box: "queue.Queue[Any]" = queue.Queue()
        with self._subscriber_lock:
            self._subscribers.append(box)
        return box

    def detach(self, box: "queue.Queue[Any]") -> None:
        with self._subscriber_lock:
            if box in self._subscribers:
                self._subscribers.remove(box)

    def publish(self, message: dict[str, Any]) -> None:
        if message.get("type") == "task_result":
            payload = message.get("payload") or {}
            database().record_task(
                self.session_id,
                self.active_media_id or "",
                str(payload.get("task", "")),
                bool(payload.get("success")),
                str(payload.get("result", "") or payload.get("error", "")),
            )
        with self._subscriber_lock:
            targets = list(self._subscribers)
        for target in targets:
            target.put(message)

    def _execute_tool(self, tool: str, arguments: dict[str, Any], session_id: str) -> dict[str, Any]:
        handler = TOOLS.get(tool)
        if handler is None:
            return {"success": False, "error": f"未知工具：{tool}"}
        if self.active_media_id is None:
            return {"success": False, "error": "尚未上传媒体文件；请先调用 UploadMedia。"}
        return handler(self.storage, self.session_id, self.active_media_id, arguments)


class SharedOllama:
    """Placeholder seam for sharing one local-model backend across sessions.

    AgentRuntime currently owns its own `_ensure_ollama`/model lazily; for
    phase 1 we keep that per-session (simplicity over sharing), but route
    construction through here so a future change can hand every session the
    same already-running Ollama client instead of each session spawning its
    own `ollama serve` check.
    """

    def model_for(self, session_id: str) -> Any | None:
        return None


class SessionManager:
    def __init__(self, storage_root: Path) -> None:
        self.storage = SessionStorage(storage_root)
        self._sessions: dict[str, SessionState] = {}
        self._lock = threading.Lock()
        self._ollama = SharedOllama()

    def get_or_create(self, session_id: str) -> SessionState:
        with self._lock:
            state = self._sessions.get(session_id)
            if state is None:
                state = SessionState(session_id, self.storage, self._ollama)
                self._sessions[session_id] = state
            return state

    def register_upload(self, session_id: str, media_id: str) -> None:
        self.get_or_create(session_id).active_media_id = media_id
