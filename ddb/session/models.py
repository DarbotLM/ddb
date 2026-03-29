"""Shared Pydantic models for the DBSession abstraction layer.

These models form the common schema contract between all session backends
(SQLite micro DB, ArangoDB, hybrid). All backends read and write these types.
"""

from __future__ import annotations

from datetime import datetime, timezone
from typing import Any
from uuid import uuid4

from pydantic import BaseModel, Field


def _now() -> str:
    return datetime.now(timezone.utc).isoformat()


class NodeRecord(BaseModel):
    """A logical node in the DDB knowledge graph.

    Maps to a row in the SQLite ``cards`` / ``agents`` / ``patterns`` tables
    and to a document in the ArangoDB agent/card/pattern collections.
    """

    id: str = Field(default_factory=lambda: str(uuid4()))
    entity_type: str  # agent | card | pattern | triple | zone | session
    label: str
    properties: dict[str, Any] = Field(default_factory=dict)
    zone: str | None = None
    agent_id: str | None = None
    tags: list[str] = Field(default_factory=list)
    created_at: str = Field(default_factory=_now)
    updated_at: str = Field(default_factory=_now)


class EdgeRecord(BaseModel):
    """A directed edge between two nodes.

    Maps to a row in the SQLite ``links`` table or to an edge document
    in an ArangoDB edge collection.
    """

    id: str = Field(default_factory=lambda: str(uuid4()))
    edge_type: str  # agent_to_card | card_to_card | triple_to_card | ...
    from_id: str
    to_id: str
    weight: float = 1.0
    metadata: dict[str, Any] = Field(default_factory=dict)
    created_at: str = Field(default_factory=_now)


class TripleRecord(BaseModel):
    """A Subject-Predicate-Object knowledge triple.

    Maps to a row in the SQLite ``triples`` table or to a document in the
    ArangoDB ``triples`` collection.
    """

    id: str = Field(default_factory=lambda: str(uuid4()))
    subject: str
    predicate: str
    object: str  # noqa: A003
    confidence: float = 1.0
    source_id: str | None = None  # card_id or event_id that produced this triple
    model: str | None = None
    zone: str | None = None
    graph_key: str | None = None  # ArangoDB _key if synced
    created_at: str = Field(default_factory=_now)