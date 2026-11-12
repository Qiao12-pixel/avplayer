from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from avplayer_agent import local_tools
from avplayer_agent.local_tools import export_video
from avplayer_agent.storage import SessionStorage


class ExportVideoTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.storage = SessionStorage(Path(self._tmp.name))
        media_id = "m1"
        self.media_id = media_id
        original = self.storage.original_path("web", media_id, "video.mp4")
        original.write_bytes(b"fake")
        self.calls: list[list[str]] = []
        self._run_ffmpeg = local_tools._run_ffmpeg
        self._probe = local_tools._probe_duration_ms
        local_tools._run_ffmpeg = self._fake_ffmpeg
        local_tools._probe_duration_ms = lambda path: 120_000  # 2 minutes

    def tearDown(self) -> None:
        local_tools._run_ffmpeg = self._run_ffmpeg
        local_tools._probe_duration_ms = self._probe
        self._tmp.cleanup()

    def _fake_ffmpeg(self, args, timeout):
        self.calls.append(args)
        Path(args[-1]).write_bytes(b"clip")
        return True, ""

    def export(self, **arguments):
        return export_video(self.storage, "web", self.media_id, arguments)

    def test_explicit_range_exports_exactly_that_span(self):
        result = self.export(start_ms=10_000, end_ms=25_500)
        self.assertTrue(result["success"])
        self.assertEqual((result["start_ms"], result["end_ms"]), (10_000, 25_500))
        self.assertEqual(result["duration_ms"], 15_500)
        args = self.calls[0]
        self.assertEqual(args[args.index("-ss") + 1], "10.000")
        self.assertEqual(args[args.index("-t") + 1], "15.500")

    def test_range_is_not_forced_to_three_seconds(self):
        result = self.export(start_ms=0, end_ms=90_000)
        self.assertEqual(result["duration_ms"], 90_000)
        args = self.calls[0]
        self.assertEqual(args[args.index("-t") + 1], "90.000")

    def test_random_mode_still_works_when_only_duration_is_given(self):
        result = self.export(duration_ms=5_000)
        self.assertTrue(result["success"])
        self.assertEqual(result["duration_ms"], 5_000)
        self.assertGreaterEqual(result["start_ms"], 0)
        self.assertLessEqual(result["end_ms"], 120_000)

    def test_missing_arguments_is_an_error(self):
        result = self.export()
        self.assertFalse(result["success"])
        self.assertIn("start_ms", result["error"])
        self.assertEqual(self.calls, [])

    def test_reversed_range_is_rejected(self):
        result = self.export(start_ms=9_000, end_ms=9_000)
        self.assertFalse(result["success"])
        self.assertIn("end_ms", result["error"])
        self.assertEqual(self.calls, [])

    def test_range_past_the_end_is_rejected(self):
        result = self.export(start_ms=100_000, end_ms=200_000)
        self.assertFalse(result["success"])
        self.assertIn("超出", result["error"])

    def test_clip_longer_than_ten_minutes_is_rejected(self):
        self._probe = local_tools._probe_duration_ms
        local_tools._probe_duration_ms = lambda path: 3_600_000
        try:
            result = self.export(start_ms=0, end_ms=900_000)
        finally:
            local_tools._probe_duration_ms = self._probe
        self.assertFalse(result["success"])
        self.assertIn("10 分钟", result["error"])

    def test_legacy_tool_name_maps_to_the_same_handler(self):
        self.assertIs(local_tools.TOOLS["export_random_clip"], local_tools.TOOLS["export_video"])


if __name__ == "__main__":
    unittest.main()
