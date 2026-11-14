#!/usr/bin/env python3
"""Measure the current (distributed) AVPlayer architecture end to end.

Everything here runs against the real services started by scripts/dev.sh:

  1. upload throughput            browser/gRPC -> agent -> storage
  2. WebRTC playback              media_server -> H.264/Opus client
  3. control-plane latency        HTTP -> gateway -> gRPC -> media_server
  4. transcription throughput     agent -> ffmpeg -> faster-whisper
  5. clip export throughput       agent -> ffmpeg re-encode
  6. memory footprint             media_server RSS while streaming

Usage (services must be running):

    .venv/bin/python scripts/bench_metrics.py \
        --media-server localhost:50062 --agent localhost:50061 \
        --gateway http://127.0.0.1:50065 --clip resource/bench-1080p60.mp4
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
import subprocess
import sys
import threading
import time
import urllib.request
from pathlib import Path

import grpc

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "services" / "agent"))

from avplayer_agent import agent_pb2 as pb  # noqa: E402
from avplayer_agent import agent_pb2_grpc as pbg  # noqa: E402
from avplayer_agent.mcp_client import run_task  # noqa: E402

SESSION = "bench"


def _fmt(value: float, unit: str = "") -> str:
    return f"{value:.2f}{unit}"


def upload(path: Path, target: str, session: str) -> tuple[str, dict]:
    channel = grpc.insecure_channel(target)
    stub = pbg.MediaServiceStub(channel)
    data = path.read_bytes()
    size_mb = len(data) / 1e6

    def chunks():
        yield pb.MediaChunk(session_id=session, file_name=path.name, data=b"")
        for offset in range(0, len(data), 1 << 20):
            yield pb.MediaChunk(data=data[offset:offset + (1 << 20)])

    started = time.perf_counter()
    handle = stub.UploadMedia(chunks())
    elapsed = time.perf_counter() - started
    return handle.media_id, {
        "size_mb": size_mb,
        "upload_s": elapsed,
        "upload_mb_s": size_mb / elapsed if elapsed else 0.0,
    }


def stream(binary: Path, server: str, session: str, media_id: str, seconds: int) -> dict:
    process = subprocess.run(
        [str(binary), server, session, media_id, str(seconds)],
        capture_output=True, text=True, timeout=seconds + 120)
    metrics = {}
    for line in process.stdout.splitlines():
        match = re.match(r"\[bench\] (\w+)=(.*)", line.strip())
        if match:
            key, raw = match.group(1), match.group(2)
            try:
                metrics[key] = float(raw)
            except ValueError:
                metrics[key] = raw
    metrics["exit_code"] = process.returncode
    metrics["stderr_tail"] = process.stderr.strip().splitlines()[-5:]
    return metrics


def control_latency(gateway: str, session: str, media_id: str, count: int = 120) -> dict:
    """Round trip through the Python gateway, as the browser does it."""
    import json as _json

    body = _json.dumps({"session_id": session, "media_id": media_id, "action": "status"}).encode()
    times = []
    for _ in range(count):
        request = urllib.request.Request(f"{gateway}/control", data=body,
                                         headers={"Content-Type": "application/json"})
        started = time.perf_counter()
        try:
            with urllib.request.urlopen(request, timeout=5) as response:
                response.read()
        except Exception:
            continue
        times.append((time.perf_counter() - started) * 1000)
    times.sort()
    return {
        "count": len(times),
        "p50_ms": statistics.median(times) if times else 0.0,
        "p95_ms": times[int(len(times) * 0.95)] if times else 0.0,
    }


def transcribe(target: str, session: str) -> dict:
    channel = grpc.insecure_channel(target)
    stub = pbg.AgentServiceStub(channel)
    stop = threading.Event()

    def requests():
        yield pb.ClientMessage(type="task", request_id="bench-transcribe", session_id=session,
                               payload=pb.Struct(json=json.dumps({"task": "transcribe_subtitle"})))
        stop.wait(600)

    started = time.perf_counter()
    result = {}
    try:
        for message in stub.Converse(requests()):
            payload = json.loads(message.payload.json or "{}")
            if message.type == "task_result":
                result = payload
                break
    finally:
        stop.set()
    elapsed = time.perf_counter() - started
    segments = result.get("segments") or []
    return {
        "wall_s": elapsed,
        "segments": len(segments),
        "success": bool(result.get("success")),
        "error": result.get("error", ""),
    }


def media_duration(path: Path) -> float:
    out = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration",
                          "-of", "default=noprint_wrappers=1:nokey=1", str(path)],
                         capture_output=True, text=True, timeout=60).stdout.strip()
    return float(out)


def clip_export(media_id: str, duration_ms: int) -> dict:
    started = time.perf_counter()
    result = run_task("export_video", SESSION, media_id,
                          {"start_ms": 0, "end_ms": duration_ms})
    elapsed = time.perf_counter() - started
    return {
        "wall_s": elapsed,
        "success": bool(result.get("success")),
        "speed_x": (duration_ms / 1000.0) / elapsed if elapsed else 0.0,
        "path": result.get("path", ""),
        "error": result.get("error", ""),
    }


def rss_mb(pid: int) -> float:
    out = subprocess.run(["ps", "-o", "rss=", "-p", str(pid)], capture_output=True, text=True).stdout.strip()
    return int(out) / 1024.0 if out else 0.0


def listening_pid(port: int) -> int:
    out = subprocess.run(["lsof", "-nP", "-tiTCP:%d" % port, "-sTCP:LISTEN"],
                         capture_output=True, text=True).stdout.split()
    return int(out[0]) if out else 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--media-server", default="localhost:50062")
    parser.add_argument("--data-server", default="localhost:50066")
    parser.add_argument("--agent", default="localhost:50061")
    parser.add_argument("--gateway", default="http://127.0.0.1:50065")
    parser.add_argument("--clip", default=str(ROOT / "resource" / "bench-1080p60.mp4"))
    parser.add_argument("--stream-seconds", type=int, default=10)
    parser.add_argument("--transcribe", action="store_true", help="also run whisper (slow)")
    parser.add_argument("--transcribe-clip", default="",
                        help="speech clip used for the transcription benchmark "
                             "(defaults to --clip)")
    parser.add_argument("--json-out", default="")
    args = parser.parse_args()

    clip = Path(args.clip)
    if not clip.is_file():
        print(f"missing clip: {clip}", file=sys.stderr)
        return 2

    results: dict = {"clip": {"path": str(clip), "duration_s": media_duration(clip)}}

    media_id, upload_stats = upload(clip, args.data_server, SESSION)
    results["upload"] = {**upload_stats, "media_id": media_id}
    print(f"[upload] {upload_stats['size_mb']:.1f} MB in {upload_stats['upload_s']:.2f}s "
          f"= {upload_stats['upload_mb_s']:.1f} MB/s", flush=True)

    binary = ROOT / "cmake-build-debug" / "services" / "media" / "streaming_smoke_client"
    media_pid = listening_pid(int(args.media_server.rsplit(":", 1)[1]))
    rss_before = rss_mb(media_pid) if media_pid else 0.0

    # Control latency needs a live session; sample it while the client streams.
    samples: dict = {}

    def sample_control() -> None:
        time.sleep(4.0)
        samples.update(control_latency(args.gateway, SESSION, media_id))

    sampler = threading.Thread(target=sample_control, daemon=True)
    sampler.start()
    stream_stats = stream(binary, args.media_server, SESSION, media_id, args.stream_seconds)
    sampler.join(timeout=30)
    rss_after = rss_mb(media_pid) if media_pid else 0.0

    results["stream"] = stream_stats
    results["control"] = samples
    results["media_server_rss_mb"] = {"before": rss_before, "after": rss_after}
    print(f"[stream] connect={stream_stats.get('connect_ms', 0):.0f} ms "
          f"first_frame={stream_stats.get('first_video_ms', 0):.0f} ms "
          f"{stream_stats.get('video_mbps', 0):.2f} Mbps "
          f"{stream_stats.get('video_pkt_per_s', 0):.0f} pkt/s "
          f"audio={stream_stats.get('audio_kbps', 0):.0f} kbps", flush=True)
    print(f"[control] p50={samples.get('p50_ms', 0):.2f} ms p95={samples.get('p95_ms', 0):.2f} ms", flush=True)

    results["export"] = clip_export(media_id, int(results["clip"]["duration_s"] * 1000 / 2))
    print(f"[export] {results['export']['wall_s']:.2f}s "
          f"({results['export']['speed_x']:.1f}x realtime)", flush=True)

    if args.transcribe:
        speech = Path(args.transcribe_clip) if args.transcribe_clip else clip
        speech_id, _ = upload(speech, args.data_server, "bench-audio")
        audio_s = media_duration(speech)
        results["transcribe"] = transcribe(args.agent, "bench-audio")
        results["transcribe"].update({"audio_s": audio_s, "media_id": speech_id,
                                      "rtf": results["transcribe"]["wall_s"] / audio_s})
        print(f"[transcribe] {results['transcribe']['wall_s']:.1f}s for {audio_s:.0f}s audio "
              f"(RTF={results['transcribe']['rtf']:.2f}), "
              f"{results['transcribe']['segments']} segments", flush=True)

    if args.json_out:
        Path(args.json_out).write_text(json.dumps(results, ensure_ascii=False, indent=2))
        print(f"[done] wrote {args.json_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
