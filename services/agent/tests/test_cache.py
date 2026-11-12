from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from avplayer_agent import local_tools
from avplayer_agent.cache import TranscriptCache
from avplayer_agent.local_tools import get_transcript, save_transcript
from avplayer_agent.storage import SessionStorage

SEGMENTS = [
    {"start_ms": 1000, "end_ms": 2500, "text": "第一句"},
    {"start_ms": 3000, "end_ms": 5000, "text": "第二句"},
]


class FakeCache:
    """In-memory stand-in for TranscriptCache."""

    def __init__(self) -> None:
        self.store: dict[tuple[str, str], list[dict]] = {}

    def get(self, session_id: str, media_id: str):
        return self.store.get((session_id, media_id))

    def put(self, session_id: str, media_id: str, segments: list[dict]) -> None:
        self.store[(session_id, media_id)] = segments


class TranscriptCacheTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.storage = SessionStorage(Path(self._tmp.name))
        self.fake = FakeCache()
        self._original = local_tools.transcript_cache
        local_tools.transcript_cache = lambda: self.fake  # type: ignore[assignment]

    def tearDown(self) -> None:
        local_tools.transcript_cache = self._original  # type: ignore[assignment]
        self._tmp.cleanup()

    def test_save_then_read_uses_cache(self) -> None:
        save_transcript(self.storage, "web", "m1", {"segments": SEGMENTS})
        self.assertEqual(self.fake.store[("web", "m1")], SEGMENTS)

        # Remove the JSON on disk: a cache hit must still return the transcript.
        json_path = self.storage.transcript_path("web", "m1").with_suffix(".json")
        json_path.unlink()
        value = get_transcript(self.storage, "web", "m1", {})
        self.assertTrue(value["has_timestamps"])
        self.assertIn("第一句", value["text"])
        self.assertIn("[1000-2500]", value["text"])

    def test_cache_miss_repopulates_from_disk(self) -> None:
        save_transcript(self.storage, "web", "m2", {"segments": SEGMENTS})
        self.fake.store.clear()

        value = get_transcript(self.storage, "web", "m2", {})
        self.assertIn("第二句", value["text"])
        self.assertEqual(self.fake.store[("web", "m2")], SEGMENTS)

    def test_missing_transcript_is_not_an_error(self) -> None:
        value = get_transcript(self.storage, "web", "absent", {})
        self.assertEqual(value["text"], "")
        self.assertFalse(value["has_timestamps"])

    def test_sessions_are_isolated(self) -> None:
        save_transcript(self.storage, "web", "m3", {"segments": SEGMENTS})
        self.assertIsNotNone(self.fake.store.get(("web", "m3")))
        self.assertIsNone(self.fake.store.get(("other", "m3")))


class DegradeTests(unittest.TestCase):
    def test_disabled_cache_reads_and_writes_without_error(self) -> None:
        cache = TranscriptCache(url="")
        self.assertFalse(cache.enabled)
        self.assertIsNone(cache.get("web", "m1"))
        cache.put("web", "m1", SEGMENTS)  # must be a no-op, not a crash
        cache.drop("web", "m1")

    def test_unreachable_redis_degrades_to_miss(self) -> None:
        # Port 1 is closed: connect fails fast and the cache disables itself.
        cache = TranscriptCache(url="redis://127.0.0.1:1/0", ttl=60)
        self.assertIsNone(cache.get("web", "m1"))
        cache.put("web", "m1", SEGMENTS)
        self.assertIsNone(cache.get("web", "m1"))

    def test_disk_path_still_works_when_redis_is_down(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            storage = SessionStorage(Path(tmp))
            original = local_tools.transcript_cache
            local_tools.transcript_cache = lambda: TranscriptCache(url="redis://127.0.0.1:1/0")
            try:
                save_transcript(storage, "web", "m9", {"segments": SEGMENTS})
                value = get_transcript(storage, "web", "m9", {})
            finally:
                local_tools.transcript_cache = original  # type: ignore[assignment]
            self.assertIn("第一句", value["text"])
            payload = json.loads(
                storage.transcript_path("web", "m9").with_suffix(".json").read_text(encoding="utf-8"))
            self.assertEqual(payload, SEGMENTS)


if __name__ == "__main__":
    unittest.main()
