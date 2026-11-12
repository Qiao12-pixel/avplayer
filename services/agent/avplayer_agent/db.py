"""Optional MySQL persistence for sessions, media, transcripts and task runs.

MySQL is the durable cross-instance store; the files under ``storage/`` remain
the source of truth for the media pipeline (``media_server`` reads the same
files). Redis (see ``cache.py``) sits in front of the transcript read path.

Everything here is best-effort: if the driver is missing or the server is
unreachable, every call becomes a silent no-op and the agent keeps working
against disk only.

DSN format: ``mysql://user:password@host:port/database``
"""

from __future__ import annotations

import threading
from typing import Any, Iterable
from urllib.parse import unquote, urlparse

from .settings import mysql_dsn

try:  # pragma: no cover - depends on the optional package
    import pymysql
except ImportError:  # pragma: no cover
    pymysql = None

SCHEMA = (
    """CREATE TABLE IF NOT EXISTS sessions (
        session_id VARCHAR(64) PRIMARY KEY,
        created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
    )""",
    """CREATE TABLE IF NOT EXISTS media (
        media_id CHAR(32) PRIMARY KEY,
        session_id VARCHAR(64) NOT NULL,
        file_name VARCHAR(255) NOT NULL DEFAULT '',
        size_bytes BIGINT NOT NULL DEFAULT 0,
        created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
        KEY idx_media_session (session_id)
    )""",
    """CREATE TABLE IF NOT EXISTS transcripts (
        media_id CHAR(32) PRIMARY KEY,
        session_id VARCHAR(64) NOT NULL,
        segment_count INT NOT NULL DEFAULT 0,
        text MEDIUMTEXT NOT NULL,
        created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
    )""",
    """CREATE TABLE IF NOT EXISTS segments (
        media_id CHAR(32) NOT NULL,
        idx INT NOT NULL,
        start_ms INT NOT NULL,
        end_ms INT NOT NULL,
        text TEXT NOT NULL,
        PRIMARY KEY (media_id, idx)
    )""",
    """CREATE TABLE IF NOT EXISTS task_runs (
        id BIGINT AUTO_INCREMENT PRIMARY KEY,
        session_id VARCHAR(64) NOT NULL,
        media_id CHAR(32) NOT NULL DEFAULT '',
        task VARCHAR(64) NOT NULL,
        success TINYINT(1) NOT NULL,
        result MEDIUMTEXT,
        created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
        KEY idx_task_session (session_id, created_at)
    )""",
)


def _parse_dsn(dsn: str) -> dict[str, Any]:
    parsed = urlparse(dsn)
    return {
        "host": parsed.hostname or "127.0.0.1",
        "port": parsed.port or 3306,
        "user": unquote(parsed.username or "root"),
        "password": unquote(parsed.password or ""),
        "database": (parsed.path or "/avplayer").lstrip("/"),
        "charset": "utf8mb4",
        "autocommit": True,
        "connect_timeout": 2,
    }


class Database:
    """Lazy, failure-tolerant MySQL writer/reader."""

    def __init__(self, dsn: str = "", schema: Iterable[str] = SCHEMA) -> None:
        self._dsn = dsn
        self._schema = tuple(schema)
        self._lock = threading.Lock()
        self._conn: Any = None
        self._broken = False
        self._ready = False

    @property
    def enabled(self) -> bool:
        return bool(self._dsn) and pymysql is not None and not self._broken

    def _connection(self) -> Any:
        if not self.enabled:
            return None
        with self._lock:
            if self._conn is not None:
                return self._conn
            try:
                self._conn = pymysql.connect(**_parse_dsn(self._dsn))
                if not self._ready:
                    with self._conn.cursor() as cursor:
                        for statement in self._schema:
                            cursor.execute(statement)
                    self._ready = True
            except Exception:
                self._broken = True
                self._conn = None
            return self._conn

    def _execute(self, sql: str, params: tuple[Any, ...] = ()) -> int:
        conn = self._connection()
        if conn is None:
            return 0
        with self._lock:
            try:
                with conn.cursor() as cursor:
                    return cursor.execute(sql, params)
            except Exception:
                self._broken = True
                self._conn = None
                return 0

    def _fetchall(self, sql: str, params: tuple[Any, ...] = ()) -> list[tuple]:
        conn = self._connection()
        if conn is None:
            return []
        with self._lock:
            try:
                with conn.cursor() as cursor:
                    cursor.execute(sql, params)
                    return list(cursor.fetchall())
            except Exception:
                self._broken = True
                self._conn = None
                return []

    # -- writes ---------------------------------------------------------
    def record_session(self, session_id: str) -> None:
        self._execute("INSERT IGNORE INTO sessions (session_id) VALUES (%s)", (session_id,))

    def record_media(self, session_id: str, media_id: str, file_name: str, size_bytes: int) -> None:
        self.record_session(session_id)
        self._execute(
            "INSERT INTO media (media_id, session_id, file_name, size_bytes) VALUES (%s, %s, %s, %s) "
            "ON DUPLICATE KEY UPDATE file_name = VALUES(file_name), size_bytes = VALUES(size_bytes)",
            (media_id, session_id, file_name, size_bytes))

    def save_transcript(self, session_id: str, media_id: str,
                        segments: list[dict[str, Any]]) -> None:
        text = "\n".join(str(item.get("text", "")) for item in segments)
        self._execute(
            "REPLACE INTO transcripts (media_id, session_id, segment_count, text) VALUES (%s, %s, %s, %s)",
            (media_id, session_id, len(segments), text))
        self._execute("DELETE FROM segments WHERE media_id = %s", (media_id,))
        for index, item in enumerate(segments):
            self._execute(
                "INSERT INTO segments (media_id, idx, start_ms, end_ms, text) VALUES (%s, %s, %s, %s, %s)",
                (media_id, index, int(item.get("start_ms", 0)), int(item.get("end_ms", 0)),
                 str(item.get("text", ""))))

    def record_task(self, session_id: str, media_id: str, task: str,
                    success: bool, result: str) -> None:
        self._execute(
            "INSERT INTO task_runs (session_id, media_id, task, success, result) VALUES (%s, %s, %s, %s, %s)",
            (session_id, media_id[:32], task[:64], 1 if success else 0, result[:20000]))

    # -- reads ----------------------------------------------------------
    def load_transcript(self, media_id: str) -> list[dict[str, Any]] | None:
        rows = self._fetchall(
            "SELECT start_ms, end_ms, text FROM segments WHERE media_id = %s ORDER BY idx", (media_id,))
        if not rows:
            return None
        return [{"start_ms": row[0], "end_ms": row[1], "text": row[2]} for row in rows]

    def list_media(self, session_id: str) -> list[dict[str, Any]]:
        rows = self._fetchall(
            "SELECT media_id, file_name, size_bytes, created_at FROM media "
            "WHERE session_id = %s ORDER BY created_at DESC", (session_id,))
        return [{"media_id": row[0], "file_name": row[1], "size_bytes": row[2],
                 "created_at": str(row[3])} for row in rows]


_DATABASE: Database | None = None


def database() -> Database:
    """Process-wide instance (settings read once, like the Redis cache)."""
    global _DATABASE
    if _DATABASE is None:
        _DATABASE = Database(mysql_dsn())
    return _DATABASE
