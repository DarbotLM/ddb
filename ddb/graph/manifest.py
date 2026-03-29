"""Manifest schema for DarbotDB 3DKG scene projection."""

from __future__ import annotations

from datetime import datetime, timezone
from typing import Any
from uuid import uuid4

from pydantic import BaseModel, Field

MANIFEST_SCHEMA_VERSION = "v1.0.0"


class SpatialHint(BaseModel):
    x: float | None = None
    y: float | None = None
    z: float | None = None
    cluster: str | None = None
    radius: float | None = None
    pinned: bool = False
    weight: float = 1.0
    metadata: dict[str, Any] = Field(default_factory=dict)


class ManifestNode(BaseModel):
    id: str = Field(default_factory=lambda: str(uuid4()))
    entity_id: str
    entity_kind: str
    label: str
    layer: str = "semantic"
    node_type: str = "entity"
    inspector_card_id: str | None = None
    spatial: SpatialHint = Field(default_factory=SpatialHint)
    metadata: dict[str, Any] = Field(default_factory=dict)


class ManifestEdge(BaseModel):
    id: str = Field(default_factory=lambda: str(uuid4()))
    source_node_id: str
    target_node_id: str
    relation: str
    edge_type: str = "semantic"
    label: str | None = None
    metadata: dict[str, Any] = Field(default_factory=dict)


class SceneCamera(BaseModel):
    x: float = 0.0
    y: float = 0.0
    z: float = 600.0


class SceneView(BaseModel):
    id: str = Field(default_factory=lambda: str(uuid4()))
    manifest_id: str | None = None
    title: str = "Default View"
    layout: str = "force-3d"
    selected_node_id: str | None = None
    visible_layers: list[str] = Field(default_factory=list)
    filters: dict[str, Any] = Field(default_factory=dict)
    camera: SceneCamera = Field(default_factory=SceneCamera)
    metadata: dict[str, Any] = Field(default_factory=dict)


class Manifest(BaseModel):
    id: str = Field(default_factory=lambda: str(uuid4()))
    title: str
    description: str = ""
    session_id: str | None = None
    source_db_id: str | None = None
    source_kind: str = "micro"
    schema_version: str = MANIFEST_SCHEMA_VERSION
    layout: str = "force-3d"
    nodes: list[ManifestNode] = Field(default_factory=list)
    edges: list[ManifestEdge] = Field(default_factory=list)
    views: list[SceneView] = Field(default_factory=list)
    metadata: dict[str, Any] = Field(default_factory=dict)
    created_at: datetime = Field(default_factory=lambda: datetime.now(timezone.utc))
    updated_at: datetime = Field(default_factory=lambda: datetime.now(timezone.utc))

    def to_scene_json(self) -> dict[str, Any]:
        return {
            "manifest": {
                "id": self.id,
                "title": self.title,
                "description": self.description,
                "layout": self.layout,
                "schema_version": self.schema_version,
                "session_id": self.session_id,
                "source_db_id": self.source_db_id,
                "source_kind": self.source_kind,
                "metadata": self.metadata,
            },
            "nodes": [
                {
                    "id": n.id,
                    "label": n.label,
                    "entity_id": n.entity_id,
                    "entity_kind": n.entity_kind,
                    "layer": n.layer,
                    "node_type": n.node_type,
                    "inspector_card_id": n.inspector_card_id,
                    "spatial": n.spatial.model_dump(exclude_none=True),
                    "metadata": n.metadata,
                }
                for n in self.nodes
            ],
            "edges": [
                {
                    "id": e.id,
                    "source": e.source_node_id,
                    "target": e.target_node_id,
                    "relation": e.relation,
                    "edge_type": e.edge_type,
                    "label": e.label,
                    "metadata": e.metadata,
                }
                for e in self.edges
            ],
            "views": [v.model_dump(exclude_none=True) for v in self.views],
        }