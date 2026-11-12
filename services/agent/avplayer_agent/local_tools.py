"""Pure-Python/ffmpeg reimplementation of the C++ Agent tools.

Phase 1 of the client-server migration moves AudioExtractionService and
ClipExportService (former desktop C++ services) server-side. Both were thin
ffmpeg CLI wrappers with no dependency on the local decode pipeline, so they
are reimplemented here as plain subprocess calls operating on server-side
session storage instead of the Qt event loop / QProcess signals.
"""

from __future__ import annotations

import json
import random
import subprocess
import uuid
from pathlib import Path
from typing import Any

from .cache import transcript_cache
from .db import database
from .storage import SessionStorage

MAX_AUDIO_BYTES = 20 * 1024 * 1024
MAX_CLIP_DURATION_MS = 10 * 60 * 1000


def _run_ffmpeg(args: list[str], timeout: int) -> tuple[bool, str]:
    try:
        result = subprocess.run(["ffmpeg", "-nostdin", "-y", *args],
                                 capture_output=True, text=True, timeout=timeout)
    except FileNotFoundError:
        return False, "未找到 ffmpeg 可执行文件。"
    except subprocess.TimeoutExpired:
        return False, "ffmpeg 执行超时。"
    if result.returncode != 0:
        return False, result.stderr.strip()[-800:]
    return True, ""


def _probe_duration_ms(path: Path) -> int:
    result = subprocess.run(
        ["ffprobe", "-v", "error", "-show_entries", "format=duration",
         "-of", "default=noprint_wrappers=1:nokey=1", str(path)],
        capture_output=True, text=True, timeout=30)
    return int(float(result.stdout.strip()) * 1000)


def get_media_context(storage: SessionStorage, session_id: str, media_id: str,
                       arguments: dict[str, Any]) -> dict[str, Any]:
    original = storage.original_path(session_id, media_id)
    return {"success": True, "loaded": original.is_file(), "path": media_id}


def extract_audio(storage: SessionStorage, session_id: str, media_id: str,
                   arguments: dict[str, Any]) -> dict[str, Any]:
    source = storage.original_path(session_id, media_id)
    if not source.is_file():
        return {"success": False, "error": "音轨提取需要已上传的视频。"}
    output_path = source.parent / f"audio-{uuid.uuid4().hex}.mp3"
    ok, error = _run_ffmpeg(
        ["-i", str(source), "-vn", "-ac", "1", "-ar", "16000", "-b:a", "32k", str(output_path)],
        timeout=600)
    if not ok:
        output_path.unlink(missing_ok=True)
        return {"success": False, "error": f"音轨提取失败：{error}"}
    if output_path.stat().st_size > MAX_AUDIO_BYTES:
        output_path.unlink(missing_ok=True)
        return {"success": False, "error": "音轨超过 20 MB；请使用较短视频。"}
    return {"success": True, "path": str(output_path)}


def get_transcript(storage: SessionStorage, session_id: str, media_id: str,
                    arguments: dict[str, Any]) -> dict[str, Any]:
    """Read the transcript, preferring the Redis cache over the JSON on disk.

    The cache is optional: a miss (or an unavailable Redis) falls through to the
    file and repopulates the cache, so behavior is identical either way.
    """
    cache = transcript_cache()
    segments = cache.get(session_id, media_id)
    if segments is None:
        segments_path = storage.transcript_path(session_id, media_id).with_suffix(".json")
        if segments_path.is_file():
            segments = json.loads(segments_path.read_text(encoding="utf-8"))
        else:
            # Redis expired and the file is gone: MySQL is the last resort.
            segments = database().load_transcript(media_id)
        if segments is None:
            return {"success": True, "text": "", "has_timestamps": False, "source_path": media_id}
        cache.put(session_id, media_id, segments)
    text = "\n".join(f"[{item['start_ms']}-{item['end_ms']}] {item['text']}" for item in segments)
    return {"success": True, "text": text, "has_timestamps": bool(segments), "source_path": media_id}


def save_transcript(storage: SessionStorage, session_id: str, media_id: str,
                     arguments: dict[str, Any]) -> dict[str, Any]:
    segments = arguments.get("segments", [])
    segments_path = storage.transcript_path(session_id, media_id).with_suffix(".json")
    segments_path.write_text(json.dumps(segments, ensure_ascii=False), encoding="utf-8")
    srt_path = storage.transcript_path(session_id, media_id)
    srt_path.write_text(_to_srt(segments), encoding="utf-8")
    transcript_cache().put(session_id, media_id, segments)
    database().save_transcript(session_id, media_id, segments)
    return {"success": True, "path": str(srt_path)}


def _to_srt(segments: list[dict[str, Any]]) -> str:
    def timestamp(ms: int) -> str:
        hours, rest = divmod(ms, 3_600_000)
        minutes, rest = divmod(rest, 60_000)
        seconds, millis = divmod(rest, 1000)
        return f"{hours:02d}:{minutes:02d}:{seconds:02d},{millis:03d}"

    lines = []
    for index, item in enumerate(segments, 1):
        lines.append(str(index))
        lines.append(f"{timestamp(item['start_ms'])} --> {timestamp(item['end_ms'])}")
        lines.append(item["text"])
        lines.append("")
    return "\n".join(lines)


def set_edit_plan(storage: SessionStorage, session_id: str, media_id: str,
                   arguments: dict[str, Any]) -> dict[str, Any]:
    plan_path = storage.edit_plan_path(session_id, media_id)
    plan_path.write_text(json.dumps(arguments.get("segments", []), ensure_ascii=False), encoding="utf-8")
    return {"success": True}


def export_video(storage: SessionStorage, session_id: str, media_id: str,
                 arguments: dict[str, Any]) -> dict[str, Any]:
    """Export one clip chosen by the caller.

    Two modes, because "export a clip" means different things:

    * ``start_ms`` + ``end_ms`` -- export exactly that span. This is what a
      request like "把讲价格的那段剪出来" needs; the model reads the subtitle
      timeline (semantic_search / smart_edit) and passes real boundaries.
    * ``duration_ms`` only -- pick a random start with that length. Kept for
      casual requests ("随便剪一段"), never as an implicit default.
    """
    source = storage.original_path(session_id, media_id)
    if not source.is_file():
        return {"success": False, "error": "导出片段只支持已上传的视频。"}

    has_range = "start_ms" in arguments or "end_ms" in arguments
    try:
        start_ms = int(arguments.get("start_ms", 0))
        end_ms = int(arguments.get("end_ms", 0))
        duration_ms = int(arguments.get("duration_ms", 0))
    except (TypeError, ValueError):
        return {"success": False, "error": "start_ms/end_ms/duration_ms 必须是整数毫秒。"}

    try:
        source_duration_ms = _probe_duration_ms(source)
    except (OSError, ValueError, subprocess.SubprocessError):
        return {"success": False, "error": "无法读取视频时长。"}

    if has_range:
        if end_ms <= start_ms:
            return {"success": False, "error": "end_ms 必须大于 start_ms。"}
        if start_ms < 0 or end_ms > source_duration_ms:
            return {"success": False,
                    "error": f"片段超出视频范围（0–{source_duration_ms} 毫秒）。"}
        start_ms, duration_ms = start_ms, end_ms - start_ms
    elif duration_ms > 0:
        if source_duration_ms < duration_ms:
            return {"success": False, "error": "视频时长不足，无法导出指定长度的片段。"}
        last_start = source_duration_ms - duration_ms
        start_ms = 0 if last_start == 0 else random.randint(0, last_start)
    else:
        return {"success": False,
                "error": "请提供 start_ms/end_ms（按内容截取）或 duration_ms（随机截取）。"}

    if duration_ms <= 0 or duration_ms > MAX_CLIP_DURATION_MS:
        return {"success": False, "error": "片段时长必须在 1 毫秒到 10 分钟之间。"}

    output_path = storage.clip_dir(session_id, media_id) / f"clip-{uuid.uuid4().hex[:8]}.mp4"
    ok, error = _run_ffmpeg(
        ["-ss", f"{start_ms / 1000:.3f}", "-i", str(source), "-t", f"{duration_ms / 1000:.3f}",
         "-map", "0:v:0", "-map", "0:a:0?", "-c:v", "libx264", "-c:a", "aac",
         "-movflags", "+faststart", str(output_path)],
        timeout=600)
    if not ok:
        output_path.unlink(missing_ok=True)
        return {"success": False, "error": f"FFmpeg 导出失败：{error}"}
    return {"success": True, "path": str(output_path), "start_ms": start_ms,
            "end_ms": start_ms + duration_ms, "duration_ms": duration_ms}


def export_plan(storage: SessionStorage, session_id: str, media_id: str,
                arguments: dict[str, Any]) -> dict[str, Any]:
    """Export the segments returned by smart_edit (Web/Agent-side export)."""
    source = storage.original_path(session_id, media_id)
    if not source.is_file():
        return {"success": False, "error": "导出剪辑需要已上传的视频。"}
    segments = arguments.get("segments", [])
    if not isinstance(segments, list) or not segments or len(segments) > 5:
        return {"success": False, "error": "剪辑片段数量必须在 1 到 5 之间。"}
    try:
        source_duration_ms = _probe_duration_ms(source)
    except (OSError, ValueError, subprocess.SubprocessError):
        return {"success": False, "error": "无法读取视频时长。"}
    directory = storage.clip_dir(session_id, media_id)
    paths = []
    for index, item in enumerate(segments, 1):
        start_ms = int(item.get("start_ms", -1))
        end_ms = int(item.get("end_ms", -1))
        duration_ms = end_ms - start_ms
        if start_ms < 0 or duration_ms <= 0 or duration_ms > MAX_CLIP_DURATION_MS:
            return {"success": False, "error": f"第 {index} 段的时间范围无效。"}
        if end_ms > source_duration_ms:
            return {"success": False, "error": f"第 {index} 段超出视频时长。"}
        output_path = directory / f"plan-{index:02d}.mp4"
        ok, error = _run_ffmpeg(
            ["-ss", f"{start_ms / 1000:.3f}", "-i", str(source),
             "-t", f"{duration_ms / 1000:.3f}", "-map", "0:v:0", "-map", "0:a:0?",
             "-c:v", "libx264", "-c:a", "aac", "-movflags", "+faststart", str(output_path)],
            timeout=600)
        if not ok:
            output_path.unlink(missing_ok=True)
            return {"success": False, "error": f"第 {index} 段导出失败：{error}"}
        paths.append(str(output_path))
    return {"success": True, "paths": paths, "count": len(paths)}


TOOLS = {
    "get_media_context": get_media_context,
    "extract_audio": extract_audio,
    "get_transcript": get_transcript,
    "save_transcript": save_transcript,
    "set_edit_plan": set_edit_plan,
    "export_video": export_video,
    # Older clients still advertise the previous name.
    "export_random_clip": export_video,
    "export_plan": export_plan,
}
