"""Manual end-to-end check: real upload + transcribe_subtitle against a real
video, using the server-side Agent gRPC service directly — no Qt involved.
"""

from __future__ import annotations

import json
import os
import sys
import time
from pathlib import Path

import grpc

from avplayer_agent import agent_pb2, agent_pb2_grpc


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
    video_path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("resource/input.mp4")
    channel = grpc.insecure_channel("localhost:50061")
    session_id = "manual-check"

    t0 = time.monotonic()
    media_id = upload(grpc.insecure_channel(os.environ.get("AVPLAYER_DATA_SERVER", "localhost:50066")), session_id, video_path)
    print(f"uploaded media_id={media_id} in {time.monotonic() - t0:.1f}s")

    stub = agent_pb2_grpc.AgentServiceStub(channel)

    def requests():
        yield agent_pb2.ClientMessage(
            type="tool_catalog", request_id="r1", session_id=session_id,
            payload=agent_pb2.Struct(json=json.dumps({"tools": []})))
        yield agent_pb2.ClientMessage(
            type="task", request_id="r2", session_id=session_id,
            payload=agent_pb2.Struct(json=json.dumps({"task": "transcribe_subtitle"})))
        time.sleep(600)

    t0 = time.monotonic()
    for message in stub.Converse(requests()):
        elapsed = time.monotonic() - t0
        print(f"[{elapsed:6.1f}s] {message.type} {message.payload.json or message.arguments.json}")
        if message.type == "task_result":
            break


if __name__ == "__main__":
    main()
