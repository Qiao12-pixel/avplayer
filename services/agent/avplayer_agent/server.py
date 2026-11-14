"""gRPC server exposing MediaService + AgentService.

This service is the single owner of agent orchestration: the Qt desktop app
and any future web client talk to it over gRPC instead of spawning a local
Python runtime. Uploaded media and derived artifacts live under the storage
root, so media_server reads the same files.
"""

from __future__ import annotations

import json
import queue
import threading
import time
from concurrent import futures
from pathlib import Path
from typing import Any

import grpc

from . import agent_pb2, agent_pb2_grpc
from .db import database
from .session_manager import SessionManager
from .settings import agent_bind, agent_port, storage_root as default_storage_root


def _decode(value: str) -> dict[str, Any]:
    if not value:
        return {}
    return json.loads(value)


def _encode(value: dict[str, Any]) -> str:
    return json.dumps(value, ensure_ascii=False)


class AgentServicer(agent_pb2_grpc.AgentServiceServicer):
    def RegisterMedia(self, request, context):
        self._sessions.register_upload(request.session_id, request.media_id)
        return agent_pb2.ControlAck(success=True)

    def __init__(self, sessions: SessionManager) -> None:
        self._sessions = sessions

    def Converse(self, request_iterator, context):
        session_id = None
        state = None
        outbox: "queue.Queue[Any] | None" = None
        stop = threading.Event()

        def pump_requests():
            nonlocal session_id, state, outbox
            try:
                for client_message in request_iterator:
                    if state is None:
                        session_id = client_message.session_id
                        state = self._sessions.get_or_create(session_id)
                        outbox = state.attach()
                    data = {
                        "type": client_message.type,
                        "request_id": client_message.request_id,
                        "session_id": client_message.session_id,
                        "payload": _decode(client_message.payload.json),
                    }
                    if client_message.tool:
                        data["tool"] = client_message.tool
                    if client_message.arguments.json:
                        data["arguments"] = _decode(client_message.arguments.json)
                    state.runtime.handle(data)
            except grpc.RpcError:
                # Client went away (browser reload, proxy reset); a cancelled
                # stream is a normal disconnect, not a server error.
                pass
            finally:
                stop.set()

        reader = threading.Thread(target=pump_requests, daemon=True)
        reader.start()

        try:
            while True:
                if outbox is None:
                    if stop.is_set():
                        break
                    time.sleep(0.02)
                    continue
                try:
                    item = outbox.get(timeout=0.2)
                except queue.Empty:
                    if stop.is_set() and outbox.empty():
                        break
                    continue
                yield agent_pb2.ServerMessage(
                    type=item.get("type", ""),
                    request_id=item.get("request_id", ""),
                    session_id=item.get("session_id", ""),
                    tool=item.get("tool", ""),
                    payload=agent_pb2.Struct(json=_encode(item.get("payload", {}))),
                    arguments=agent_pb2.Struct(json=_encode(item.get("arguments", {}))),
                )
        finally:
            stop.set()
            if state is not None and outbox is not None:
                state.detach(outbox)
            reader.join(timeout=5)


def serve(port: int | None = None, bind: str | None = None,
          storage_dir: Path | None = None) -> grpc.Server:
    port = agent_port() if port is None else port
    bind = agent_bind() if bind is None else bind
    sessions = SessionManager(storage_dir or default_storage_root())
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=16))
    agent_pb2_grpc.add_AgentServiceServicer_to_server(AgentServicer(sessions), server)
    server.add_insecure_port(f"{bind}:{port}")
    server.start()
    return server
