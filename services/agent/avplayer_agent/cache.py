"""Optional Redis cache for artifacts that are expensive to recompute.

Design rules:

* Disk stays the source of truth. Every artifact is written to
  ``storage/<session>/<media>/`` first; Redis only short-circuits the read.
* Redis is optional. If the package is missing, the server is unreachable, or
  a command fails, we silently fall back to disk and never break a task.
* Keys are namespaced by session + media so two sessions can never see each
  other's transcript.

The transcript is the first (and currently only) cached artifact: it is written
once by ``transcribe_subtitle`` and read by summary / semantic search / smart
edit, so a hit removes a JSON read + re-format of a potentially long document.
"""

from __future__ import annotations

import json
from typing import Any

from .settings import redis_url, redis_ttl_seconds

try:  # pragma: no cover - exercised only when the optional package is present
    import redis as _redis
except ImportError:  # pragma: no cover
    _redis = None

_TRANSCRIPT_KEY = "avplayer:transcript:{session}:{media}"


class TranscriptCache:
    """Best-effort Redis cache; every failure degrades to "cache miss"."""

    def __init__(self, url: str = "", ttl: int = 3600) -> None:
        self._url = url
        self._ttl = ttl
        self._client: Any = None
        self._broken = False

    @property
    def enabled(self) -> bool:
        return bool(self._url) and _redis is not None

    def _connection(self) -> Any:
        if not self.enabled or self._broken:
            return None
        if self._client is None:
            try:
                self._client = _redis.Redis.from_url(
                    self._url,
                    socket_connect_timeout=0.3,
                    socket_timeout=0.3,
                    decode_responses=True,
                )
            except Exception:
                self._broken = True
                return None
        return self._client

    def get(self, session_id: str, media_id: str) -> list[dict[str, Any]] | None:
        client = self._connection()
        if client is None:
            return None
        try:
            raw = client.get(_TRANSCRIPT_KEY.format(session=session_id, media=media_id))
            if not raw:
                return None
            value = json.loads(raw)
        except Exception:
            # A cache must never be able to fail a task.
            self._broken = True
            return None
        return value if isinstance(value, list) else None

    def put(self, session_id: str, media_id: str, segments: list[dict[str, Any]]) -> None:
        client = self._connection()
        if client is None:
            return
        try:
            client.set(
                _TRANSCRIPT_KEY.format(session=session_id, media=media_id),
                json.dumps(segments, ensure_ascii=False),
                ex=self._ttl,
            )
        except Exception:
            self._broken = True

    def drop(self, session_id: str, media_id: str) -> None:
        client = self._connection()
        if client is None:
            return
        try:
            client.delete(_TRANSCRIPT_KEY.format(session=session_id, media=media_id))
        except Exception:
            self._broken = True


_TRANSCRIPTS: TranscriptCache | None = None


def transcript_cache() -> TranscriptCache:
    """Process-wide cache instance (settings are read once on first use)."""
    global _TRANSCRIPTS
    if _TRANSCRIPTS is None:
        _TRANSCRIPTS = TranscriptCache(redis_url(), redis_ttl_seconds())
    return _TRANSCRIPTS
