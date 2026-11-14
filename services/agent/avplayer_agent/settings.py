"""Deployment configuration shared by the agent server and Web gateway.

Every knob is an environment variable so the same package runs from a source
checkout, a local virtualenv, or a container:

    AVPLAYER_PROJECT_ROOT   repository/application root (config/, resource/)
    AVPLAYER_STORAGE_ROOT   session media + artifacts (default <root>/storage)
    AVPLAYER_AGENT_BIND     gRPC bind address (default [::])
    AVPLAYER_AGENT_PORT     gRPC port (default 50061)
    AVPLAYER_AGENT_SERVER   agent gRPC target for uploads (default localhost:50061)
    AVPLAYER_MEDIA_SERVER   media_server gRPC target (default localhost:50062)
    AVPLAYER_WEB_GATEWAY_HOST / AVPLAYER_WEB_GATEWAY_PORT
    AVPLAYER_ICE_SERVERS    comma-separated STUN/TURN URLs for the browser
    AVPLAYER_REDIS_URL      optional transcript cache (empty disables it)
    AVPLAYER_REDIS_TTL      cache lifetime in seconds (default 3600)
    AVPLAYER_MYSQL_DSN      optional durable store (empty disables it)
"""

from __future__ import annotations

import os
from pathlib import Path


def project_root() -> Path:
    return Path(os.environ.get("AVPLAYER_PROJECT_ROOT", Path.cwd())).resolve()


def storage_root() -> Path:
    override = os.environ.get("AVPLAYER_STORAGE_ROOT")
    return Path(override).resolve() if override else project_root() / "storage"


def agent_bind() -> str:
    return os.environ.get("AVPLAYER_AGENT_BIND", "[::]")


def agent_port() -> int:
    return int(os.environ.get("AVPLAYER_AGENT_PORT", "50061"))


def agent_server_target() -> str:
    return os.environ.get("AVPLAYER_AGENT_SERVER", "localhost:50061")


def media_server_target() -> str:
    return os.environ.get("AVPLAYER_MEDIA_SERVER", "localhost:50062")


def ai_provider() -> str:
    return "cloud" if os.environ.get("AVPLAYER_AI_PROVIDER") == "cloud" else "local"
def ice_servers() -> list[dict[str, str]]:
    raw = os.environ.get("AVPLAYER_ICE_SERVERS", "stun:stun.l.google.com:19302")
    return [{"urls": item.strip()} for item in raw.split(",") if item.strip()]