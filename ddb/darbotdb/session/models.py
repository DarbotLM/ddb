"""Session models for backend-agnostic 3DKG operations."""

from __future__ import annotations

from datetime import datetime, timezone
from enum import Enum
from typing import Any

from pydantic import BaseModel, Field


class SessionScope(str, Enum):
    AGENT = "agent"
    SESSION = "session"
    ZONE = "zone"
    MEMORY = "memory"


class SessionContext(BaseModel):
    db_id: str | None = None
    session_id: str | None = None
    agent_id: str | None = None
    zone: str | None = None
    scope: SessionScope = SessionScope.SESSION
    manifest_id: str | None = None
    view_id: str | None = None
    metadata: dict[str, Any] = Field(default_factory=dict)


class SessionSummary(BaseModel):
    db_id: str
    path: str
    db_type: str | None = None
    agent_id: str | None = None
    zone: str | None = None
    created_at: str | None = None


class SceneRequest(BaseModel):
    db_id: str
    title: str = "3DKG Scene"
    include_cards: bool = True
    include_triples: bool = True
    include_events: bool = True
    include_patterns: bool = True
    persist: bool = True


class SessionManifestCreate(BaseModel):
    db_id: str
    title: str
    description: str = ""
    layout: str = "force-3d"
    persist_to_graph: bool = True
    metadata: dict[str, Any] = Field(default_factory=dict)


class SessionAudit(BaseModel):
    status: str
    created_at: datetime = Field(default_factory=lambda: datetime.now(timezone.utc))
    detail: dict[str, Any] = Field(default_factory=dict)