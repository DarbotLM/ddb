"""3D Knowledge Graph Pydantic models."""

from __future__ import annotations

from datetime import datetime, timezone
from typing import Any
from uuid import uuid4

from pydantic import BaseModel, Field


def _now() -> str:
    return datetime.now(timezone.utc).isoformat()


class Node3D(BaseModel):
    """A node in the 3D knowledge graph with spatial coordinates."""

    id: str = Field(default_factory=lambda: str(uuid4()))
    node_type: str   # agent | card | pattern | triple | zone | session
    label: str
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0
    # Optional grouping for layout engine
    zone: str | None = None
    agent_id: str | None = None
    metadata: dict[str, Any] = Field(default_factory=dict)


class Edge3D(BaseModel):
    """A directed edge between two 3D nodes with an optional weight."""

    id: str = Field(default_factory=lambda: str(uuid4()))
    from_id: str
    to_id: str
    edge_type: str
    weight: float = 1.0
    metadata: dict[str, Any] = Field(default_factory=dict)


class Graph3DSnapshot(BaseModel):
    """A complete snapshot of the 3D graph at a point in time."""

    nodes: list[Node3D] = Field(default_factory=list)
    edges: list[Edge3D] = Field(default_factory=list)
    computed_at: str = Field(default_factory=_now)

    def node_map(self) -> dict[str, Node3D]:
        """Return a dict keyed by node id for O(1) lookup."""
        return {n.id: n for n in self.nodes}

    def adjacency(self) -> dict[str, list[str]]:
        """Return outbound adjacency list keyed by node id."""
        adj: dict[str, list[str]] = {n.id: [] for n in self.nodes}
        for e in self.edges:
            if e.from_id in adj:
                adj[e.from_id].append(e.to_id)
        return adj


class NodePosition(BaseModel):
    """Persisted x/y/z position record for storage in SQLite and ArangoDB."""

    node_id: str
    node_type: str
    x: float
    y: float
    z: float
    updated_at: str = Field(default_factory=_now)