"""Tests for 3DKG manifest schema, projection, and micro persistence."""

from __future__ import annotations

from pathlib import Path

import pytest
import pytest_asyncio

from cards.builder import CardBuilder
from cards.schema import CardType
from graph.manifest import Manifest, ManifestEdge, ManifestNode, SceneView
from graph.projection import ManifestProjector
from micro.engine import MicroDB


@pytest_asyncio.fixture
async def db(tmp_path: Path):
    path = tmp_path / "manifest-test.ddb"
    async with MicroDB(path) as _db:
        yield _db


def test_manifest_to_scene_json():
    manifest = Manifest(
        id="manifest-1",
        title="Engineering Scene",
        nodes=[
            ManifestNode(id="node-a", entity_id="card-1", entity_kind="card", label="Card 1", layer="cards"),
            ManifestNode(id="node-b", entity_id="entity-1", entity_kind="entity", label="Entity 1", layer="entities"),
        ],
        edges=[
            ManifestEdge(id="edge-1", source_node_id="node-a", target_node_id="node-b", relation="mentions"),
        ],
        views=[SceneView(id="view-1", manifest_id="manifest-1", title="Default")],
    )
    scene = manifest.to_scene_json()
    assert scene["manifest"]["id"] == "manifest-1"
    assert len(scene["nodes"]) == 2
    assert len(scene["edges"]) == 1
    assert scene["views"][0]["id"] == "view-1"


def test_projector_builds_manifest_from_cards_triples_and_events():
    projector = ManifestProjector()
    manifest = projector.project(
        title="Projected Scene",
        source_db_id="agent-alpha",
        cards=[
            {
                "id": "card-1",
                "title": "API Rate Limiting",
                "card_type": "memory",
                "zone": "engineering",
                "tags": '["api"]',
            }
        ],
        triples=[
            {
                "subject": "API Gateway",
                "predicate": "enforces",
                "object": "Rate Limit",
                "confidence": 0.9,
                "source_card_id": "card-1",
            }
        ],
        events=[
            {
                "id": 1,
                "event_type": "card_created",
                "source_agent": "agent-alpha",
                "triad_status": "complete",
            }
        ],
        patterns=[
            {
                "id": "pattern-1",
                "title": "Rate Limit Pattern",
                "confidence": 0.95,
            }
        ],
    )
    labels = {node.label for node in manifest.nodes}
    assert "API Rate Limiting" in labels
    assert "engineering" in labels
    assert "API Gateway" in labels
    assert "Rate Limit" in labels
    assert "card_created" in labels
    assert "Rate Limit Pattern" in labels
    relations = {edge.relation for edge in manifest.edges}
    assert "contains" in relations
    assert "enforces" in relations
    assert "emitted" in relations
    assert manifest.views


@pytest.mark.asyncio
async def test_microdb_manifest_roundtrip(db: MicroDB):
    manifest = Manifest(
        id="manifest-42",
        title="Stored Scene",
        source_db_id="manifest-test",
        nodes=[ManifestNode(id="node-1", entity_id="card-1", entity_kind="card", label="Card 1")],
        edges=[],
        views=[SceneView(id="view-42", manifest_id="manifest-42", title="Default")],
    )
    await db.upsert_manifest(manifest.model_dump(mode="json"))
    loaded = await db.get_manifest("manifest-42")
    assert loaded is not None
    assert loaded["id"] == "manifest-42"
    assert loaded["title"] == "Stored Scene"
    assert len(loaded["nodes"]) == 1
    assert loaded["nodes"][0]["entity_id"] == "card-1"
    assert len(loaded["views"]) == 1
    manifests = await db.list_manifests(source_db_id="manifest-test")
    assert len(manifests) == 1


def test_card_builder_carries_manifest_metadata():
    card = (
        CardBuilder()
        .card_type(CardType.OBSERVATION)
        .title("Projected Triple")
        .text("Inspector content")
        .entity("triple:1", "triple")
        .manifest("manifest-abc", "view-xyz")
        .spatial(x=10, y=20, z=30)
        .view_hint(layer="entities", inspector=True)
        .projection_source("txt2kg")
        .inspector_for("triple:1")
        .build()
    )
    payload = card.to_agent_json()
    assert payload["_ddb"]["entity_id"] == "triple:1"
    assert payload["_ddb"]["entity_kind"] == "triple"
    assert payload["_ddb"]["manifest_id"] == "manifest-abc"
    assert payload["_ddb"]["view_id"] == "view-xyz"
    assert payload["_ddb"]["spatial"]["x"] == 10
    assert payload["_ddb"]["view_hints"]["layer"] == "entities"