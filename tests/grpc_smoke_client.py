"""Standalone smoke test for the server-side Agent gRPC service.

Usage:
    .venv/bin/python -m pip install -e services/agent
    .venv/bin/python tests/grpc_smoke_client.py
"""

from __future__ import annotations

import json
import os
import tempfile
import time
from pathlib import Path

import grpc

from avplayer_agent import agent_pb2, agent_pb2_grpc
from avplayer_agent.server import serve


def upload(channel, session_id: str, path: Path) -> str:
    stub = agent_pb2_grpc.MediaServiceStub(channel)

    def chunks():
        yield agent_pb2.MediaChunk(session_id=session_id, file_name=path.name, data=b"")
        with open(path, "rb") as handle:
            while True:
                data = handle.read(1 << 20)
                if not data:
                    break
                yield agent_pb2.MediaChunk(data=data)

    handle = stub.UploadMedia(chunks())
    return handle.media_id


def main() -> None:
    server = serve(port=50061, bind="127.0.0.1", storage_dir=Path(os.environ.get("AVPLAYER_STORAGE_ROOT", "storage")))
    try:
        channel = grpc.insecure_channel("localhost:50061")
        session_id = "smoke-session"

        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as fake_video:
            fake_video.write(b"not a real video, just exercising the upload path")
            fake_path = Path(fake_video.name)
        media_id = upload(grpc.insecure_channel(os.environ.get("AVPLAYER_DATA_SERVER", "localhost:50066")), session_id, fake_path)
        print("uploaded media_id =", media_id)

        stub = agent_pb2_grpc.AgentServiceStub(channel)

        def requests():
            yield agent_pb2.ClientMessage(
                type="tool_catalog", request_id="r1", session_id=session_id,
                payload=agent_pb2.Struct(json=json.dumps({"tools": []})))
            yield agent_pb2.ClientMessage(
                type="task", request_id="r2", session_id=session_id,
                payload=agent_pb2.Struct(json=json.dumps({"task": "transcribe_subtitle"})))
            time.sleep(5)

        for message in stub.Converse(requests()):
            print(message.type, message.payload.json or message.arguments.json)
            if message.type == "task_result":
                break
    finally:
        server.stop(grace=1)


if __name__ == "__main__":
    main()
