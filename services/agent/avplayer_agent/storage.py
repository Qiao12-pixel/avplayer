"""Per-session file storage for the gRPC-hosted agent server.

Replaces the previous assumption (desktop-only AgentBridge) that the Qt
client and the Python runtime share a local filesystem. Uploaded media and
every artifact derived from it (extracted audio, transcripts, exported clips)
now live under the configured storage root (<storage>/<session_id>/<media_id>/).
"""

from __future__ import annotations

import uuid
from pathlib import Path


class SessionStorage:
    def __init__(self, root: Path) -> None:
        self.root = root
        self.root.mkdir(parents=True, exist_ok=True)

    def _media_dir(self, session_id: str, media_id: str) -> Path:
        directory = self.root / session_id / media_id
        directory.mkdir(parents=True, exist_ok=True)
        return directory

    def new_media_id(self) -> str:
        return uuid.uuid4().hex

    def original_path(self, session_id: str, media_id: str, file_name: str = "") -> Path:
        directory = self._media_dir(session_id, media_id)
        existing = next((item for item in directory.glob("original.*")), None)
        if existing is not None:
            return existing
        suffix = Path(file_name).suffix or ".bin"
        return directory / f"original{suffix}"

    def transcript_path(self, session_id: str, media_id: str) -> Path:
        return self._media_dir(session_id, media_id) / "transcript.srt"

    def clip_dir(self, session_id: str, media_id: str) -> Path:
        directory = self._media_dir(session_id, media_id) / "clips"
        directory.mkdir(parents=True, exist_ok=True)
        return directory

    def edit_plan_path(self, session_id: str, media_id: str) -> Path:
        return self._media_dir(session_id, media_id) / "edit_plan.json"
