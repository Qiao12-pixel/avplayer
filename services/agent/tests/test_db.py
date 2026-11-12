from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from avplayer_agent import local_tools, session_manager
from avplayer_agent.db import Database
from avplayer_agent.local_tools import get_transcript, save_transcript
from avplayer_agent.session_manager import SessionState, SharedOllama
from avplayer_agent.storage import SessionStorage

SEGMENTS = [
    {"start_ms": 1000, "end_ms": 2500, "text": "第一句"},
    {"start_ms": 3000, "end_ms": 5000, "text": "第二句"},
]


class FakeCache:
    def __init__(self) -> None:
        self.store: dict[tuple[str, str], list[dict]] = {}

    def get(self, session_id: str, media_id: str):
        return self.store.get((session_id, media_id))

    def put(self, session_id: str, media_id: str, segments: list[dict]) -> None:
        self.store[(session_id, media_id)] = segments


class FakeDatabase:
    def __init__(self) -> None:
        self.transcripts: dict[str, list[dict]] = {}
        self.tasks: list[tuple] = []
        self.media: list[tuple] = []

    def save_transcript(self, session_id: str, media_id: str, segments: list[dict]) -> None:
        self.transcripts[media_id] = segments

    def load_transcript(self, media_id: str):
        return self.transcripts.get(media_id)

    def record_task(self, session_id, media_id, task, success, result) -> None:
        self.tasks.append((session_id, media_id, task, success, result))

    def record_media(self, session_id, media_id, file_name, size_bytes) -> None:
        self.media.append((session_id, media_id, file_name, size_bytes))


class PersistenceTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.storage = SessionStorage(Path(self._tmp.name))
        self.fake_cache = FakeCache()
        self.fake_db = FakeDatabase()
        self._cache = local_tools.transcript_cache
        self._db = local_tools.database
        local_tools.transcript_cache = lambda: self.fake_cache  # type: ignore[assignment]
        local_tools.database = lambda: self.fake_db  # type: ignore[assignment]

    def tearDown(self) -> None:
        local_tools.transcript_cache = self._cache  # type: ignore[assignment]
        local_tools.database = self._db  # type: ignore[assignment]
        self._tmp.cleanup()

    def test_transcribe_writes_cache_and_mysql(self) -> None:
        save_transcript(self.storage, "web", "m1", {"segments": SEGMENTS})
        self.assertEqual(self.fake_cache.store[("web", "m1")], SEGMENTS)
        self.assertEqual(self.fake_db.transcripts["m1"], SEGMENTS)

    def test_mysql_is_last_resort_when_cache_and_disk_are_empty(self) -> None:
        save_transcript(self.storage, "web", "m2", {"segments": SEGMENTS})
        self.fake_cache.store.clear()
        self.storage.transcript_path("web", "m2").with_suffix(".json").unlink()

        value = get_transcript(self.storage, "web", "m2", {})
        self.assertIn("第二句", value["text"])
        self.assertTrue(value["has_timestamps"])
        # ...and the cache is repopulated so the next read is hot again.
        self.assertEqual(self.fake_cache.store[("web", "m2")], SEGMENTS)

    def test_nothing_anywhere_is_still_a_miss(self) -> None:
        value = get_transcript(self.storage, "web", "absent", {})
        self.assertEqual(value["text"], "")
        self.assertFalse(value["has_timestamps"])

    def test_task_results_are_recorded(self) -> None:
        original = session_manager.database
        session_manager.database = lambda: self.fake_db  # type: ignore[assignment]
        try:
            state = SessionState("web", self.storage, SharedOllama())
            state.active_media_id = "m3"
            state.publish({"type": "task_result",
                           "payload": {"task": "transcribe_subtitle", "success": True, "result": "ok"}})
        finally:
            session_manager.database = original  # type: ignore[assignment]
        self.assertEqual(self.fake_db.tasks, [("web", "m3", "transcribe_subtitle", True, "ok")])


class DegradeTests(unittest.TestCase):
    def test_disabled_database_is_a_noop(self) -> None:
        db = Database("")
        self.assertFalse(db.enabled)
        self.assertIsNone(db.load_transcript("m1"))
        db.save_transcript("web", "m1", SEGMENTS)
        db.record_task("web", "m1", "summarize_video", True, "ok")
        db.record_media("web", "m1", "a.mp4", 10)
        self.assertEqual(db.list_media("web"), [])

    def test_unreachable_server_degrades(self) -> None:
        db = Database("mysql://user:pass@127.0.0.1:1/avplayer")
        self.assertIsNone(db.load_transcript("m1"))  # must not raise
        db.save_transcript("web", "m1", SEGMENTS)
        self.assertFalse(db.enabled)

    def test_dsn_is_parsed_without_leaking_password(self) -> None:
        from avplayer_agent.db import _parse_dsn

        parsed = _parse_dsn("mysql://avplayer:s3cret@db:3307/media")
        self.assertEqual((parsed["host"], parsed["port"], parsed["database"]), ("db", 3307, "media"))
        self.assertEqual(parsed["user"], "avplayer")
        self.assertNotIn("s3cret", repr(parsed.get("charset")))


if __name__ == "__main__":
    unittest.main()
