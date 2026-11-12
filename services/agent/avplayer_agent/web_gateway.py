"""HTTP/WebSocket bridge for browsers to the native StreamingService gRPC API."""

from __future__ import annotations

import asyncio
import json
from pathlib import Path
from urllib.parse import unquote

from aiohttp import web
import grpc

from . import agent_pb2, agent_pb2_grpc

from .settings import (
    agent_server_target,
    ai_provider,
    ice_servers,
    media_server_target,
    storage_root,
    web_gateway_host,
    web_gateway_port,
)


def _signal(message: dict) -> agent_pb2.SignalMessage:
    return agent_pb2.SignalMessage(
        session_id=message.get("session_id", ""),
        media_id=message.get("media_id", ""),
        kind=message.get("kind", ""),
        sdp=message.get("sdp", ""),
        candidate=message.get("candidate", ""),
        mid=message.get("mid", ""),
    )


async def signal(request: web.Request) -> web.WebSocketResponse:
    ws = web.WebSocketResponse(heartbeat=25, max_msg_size=1_000_000)
    await ws.prepare(request)
    channel = grpc.aio.insecure_channel(media_server_target())
    stub = agent_pb2_grpc.StreamingServiceStub(channel)
    outgoing: asyncio.Queue[agent_pb2.SignalMessage | None] = asyncio.Queue()
    session = {"id": "", "media": ""}

    async def requests():
        while True:
            item = await outgoing.get()
            if item is None:
                return
            yield item

    call = stub.Signal(requests())

    async def read_responses():
        try:
            async for item in call:
                await ws.send_json({"kind": item.kind, "sdp": item.sdp,
                                    "candidate": item.candidate, "mid": item.mid})
        except (grpc.aio.AioRpcError, asyncio.CancelledError) as exc:
            if isinstance(exc, grpc.aio.AioRpcError) and not ws.closed:
                await ws.send_json({"kind": "error", "error": exc.details() or str(exc)})
        finally:
            if not ws.closed:
                await ws.close()

    reader = asyncio.create_task(read_responses())
    try:
        async for frame in ws:
            if frame.type == web.WSMsgType.TEXT:
                try:
                    data = json.loads(frame.data)
                    if data.get("kind") == "offer":
                        session["id"], session["media"] = data.get("session_id", ""), data.get("media_id", "")
                        if not session["id"] or not session["media"]:
                            raise ValueError("session_id and media_id are required")
                    if not session["id"] or not session["media"]:
                        raise ValueError("send an offer before ICE candidates")
                    data["session_id"], data["media_id"] = session["id"], session["media"]
                    await outgoing.put(_signal(data))
                except (ValueError, json.JSONDecodeError) as exc:
                    await ws.send_json({"kind": "error", "error": str(exc)})
            elif frame.type in (web.WSMsgType.ERROR, web.WSMsgType.CLOSE):
                break
    finally:
        await outgoing.put(None)
        call.cancel()
        reader.cancel()
        await asyncio.gather(reader, return_exceptions=True)
        await channel.close()
    return ws


async def control(request: web.Request) -> web.Response:
    try:
        data = await request.json()
        async with grpc.aio.insecure_channel(media_server_target()) as channel:
            stub = agent_pb2_grpc.StreamingServiceStub(channel)
            result = await stub.Control(agent_pb2.ControlRequest(
                session_id=data["session_id"], media_id=data["media_id"],
                action=data["action"], position=float(data.get("position", 0))))
        return web.json_response({
            "success": result.success,
            "error": result.error,
            "duration_ms": result.duration_ms,
            "position_ms": result.position_ms,
            "playing": result.playing,
            "recording": result.recording,
            "output_path": result.output_path,
        }, status=200 if result.success else 400)
    except (KeyError, TypeError, ValueError, json.JSONDecodeError) as exc:
        return web.json_response({"success": False, "error": str(exc)}, status=400)
    except grpc.aio.AioRpcError as exc:
        return web.json_response({"success": False, "error": exc.details()}, status=502)


async def health(_: web.Request) -> web.Response:
    return web.json_response({"status": "ok"})


def _struct(value: dict | None) -> agent_pb2.Struct:
    return agent_pb2.Struct(json=json.dumps(value or {}, ensure_ascii=False))


async def agent(request: web.Request) -> web.WebSocketResponse:
    """Browser <-> AgentService.Converse bridge (JSON over WebSocket)."""
    ws = web.WebSocketResponse(heartbeat=25, max_msg_size=4 * 1024 * 1024)
    await ws.prepare(request)
    channel = grpc.aio.insecure_channel(agent_server_target())
    stub = agent_pb2_grpc.AgentServiceStub(channel)
    outgoing: asyncio.Queue[agent_pb2.ClientMessage | None] = asyncio.Queue()

    async def requests():
        while True:
            item = await outgoing.get()
            if item is None:
                return
            yield item

    call = stub.Converse(requests())

    async def read_responses():
        try:
            async for item in call:
                payload = json.loads(item.payload.json) if item.payload.json else {}
                arguments = json.loads(item.arguments.json) if item.arguments.json else {}
                await ws.send_json({
                    "type": item.type,
                    "request_id": item.request_id,
                    "session_id": item.session_id,
                    "tool": item.tool,
                    "payload": payload,
                    "arguments": arguments,
                })
        except (grpc.aio.AioRpcError, asyncio.CancelledError) as exc:
            if isinstance(exc, grpc.aio.AioRpcError) and not ws.closed:
                await ws.send_json({"type": "error", "error": exc.details() or str(exc)})
        finally:
            # The gRPC stream ended (server replaced this reader or the call
            # failed). Leaving the WebSocket open would strand the browser on
            # a dead connection that never receives results again.
            if not ws.closed:
                await ws.close()

    reader = asyncio.create_task(read_responses())
    try:
        async for frame in ws:
            if frame.type != web.WSMsgType.TEXT:
                continue
            try:
                data = json.loads(frame.data)
                message = agent_pb2.ClientMessage(
                    type=str(data.get("type", "")),
                    request_id=str(data.get("request_id", "")),
                    session_id=str(data.get("session_id", "web")),
                    tool=str(data.get("tool", "")),
                    payload=_struct(data.get("payload")),
                    arguments=_struct(data.get("arguments")),
                )
                await outgoing.put(message)
            except (ValueError, json.JSONDecodeError) as exc:
                await ws.send_json({"type": "error", "error": str(exc)})
    finally:
        await outgoing.put(None)
        call.cancel()
        reader.cancel()
        await asyncio.gather(reader, return_exceptions=True)
        await channel.close()
    return ws


async def upload(request: web.Request) -> web.Response:
    """Browser-friendly proxy for MediaService.UploadMedia.

    The browser cannot speak gRPC, so it POSTs the raw file body here and the
    gateway streams it to the agent service. ``X-Session-Id`` and
    ``X-File-Name`` are required (file name may be percent-encoded).
    """
    session_id = request.headers.get("X-Session-Id", "").strip()
    file_name = unquote(request.headers.get("X-File-Name", "upload.bin"))
    if not session_id:
        return web.json_response({"error": "X-Session-Id is required"}, status=400)

    channel = grpc.aio.insecure_channel(agent_server_target())
    stub = agent_pb2_grpc.MediaServiceStub(channel)

    async def chunks():
        yield agent_pb2.MediaChunk(session_id=session_id, file_name=file_name, data=b"")
        async for chunk in request.content.iter_chunked(1 << 20):
            if chunk:
                yield agent_pb2.MediaChunk(data=chunk)

    try:
        handle = await stub.UploadMedia(chunks())
        return web.json_response({"media_id": handle.media_id,
                                  "size_bytes": handle.size_bytes})
    except grpc.aio.AioRpcError as exc:
        return web.json_response({"error": exc.details() or str(exc)}, status=502)
    finally:
        await channel.close()


async def download(request: web.Request) -> web.StreamResponse:
    """Download a server-side artifact (transcript, clip, recording)."""
    session_id = request.query.get("session_id", "")
    media_id = request.query.get("media_id", "")
    file_name = request.query.get("file", "")
    if not session_id or not media_id or not file_name:
        raise web.HTTPBadRequest(text="session_id, media_id and file are required")
    if any(part in ("", ".", "..") for part in Path(file_name).parts):
        raise web.HTTPBadRequest(text="invalid file name")
    root = storage_root().resolve()
    target = (root / session_id / media_id / file_name).resolve()
    if root not in target.parents or not target.is_file():
        raise web.HTTPNotFound(text="file not found")
    return web.FileResponse(target)


async def client_config(_: web.Request) -> web.Response:
    """Browser-side WebRTC configuration; server-side ICE stays in media_server."""
    return web.json_response({"ice_servers": ice_servers(), "provider": ai_provider()})


def create_app() -> web.Application:
    app = web.Application(client_max_size=0)
    app.router.add_get("/signal", signal)
    app.router.add_post("/control", control)
    app.router.add_post("/upload", upload)
    app.router.add_get("/health", health)
    app.router.add_get("/agent", agent)
    app.router.add_get("/config", client_config)
    app.router.add_get("/download", download)
    return app


def main() -> int:
    web.run_app(create_app(), host=web_gateway_host(), port=web_gateway_port())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
