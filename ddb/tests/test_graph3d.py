"""Tests for the 3D knowledge graph module."""

from __future__ import annotations

import math
from pathlib import Path

import pytest
import pytest_asyncio

from graph3d.models import Node3D, Edge3D, Graph3DSnapshot
from graph3d.coords import CoordSystem
from graph3d.layout import LayoutEngine
from graph3d.queries import SpatialQuery
from graph3d.sync import Graph3DSync
from micro.engine import MicroDB


@pytest_asyncio.fixture
async def db(tmp_path: Path):
    path = tmp_path / "g3d_test.ddb"
    async with MicroDB(path) as _db:
        yield _db


# -- CoordSystem -------------------------------------------------------------

def test_coord_system_zone_placement():
    cs = CoordSystem()
    cs.assign_zones(["eng", "ops", "product"])
    pos = cs.get_position("eng")
    assert pos is not None
    x, y, z = pos
    dist = math.sqrt(x**2 + y**2 + z**2)
    # Should be close to ZONE_RADIUS
    assert abs(dist - CoordSystem.ZONE_RADIUS) < 5.0


def test_coord_system_agent_within_zone():
    cs = CoordSystem()
    cs.assign_zones(["eng"])
    cs.assign_agent("agent-1", zone="eng")
    zone_pos = cs.get_position("eng")
    agent_pos = cs.get_position("agent-1")
    assert zone_pos and agent_pos
    dx = agent_pos[0] - zone_pos[0]
    dy = agent_pos[1] - zone_pos[1]
    dz = agent_pos[2] - zone_pos[2]
    dist = math.sqrt(dx**2 + dy**2 + dz**2)
    # Agent should be close to AGENT_RADIUS from zone center
    assert abs(dist - CoordSystem.AGENT_RADIUS) < 5.0


def test_coord_system_card_within_agent():
    cs = CoordSystem()
    cs.assign_zones(["eng"])
    cs.assign_agent("agent-1", zone="eng")
    cs.assign_card("card-a", agent_id="agent-1")
    agent_pos = cs.get_position("agent-1")
    card_pos = cs.get_position("card-a")
    assert agent_pos and card_pos
    dx = card_pos[0] - agent_pos[0]
    dy = card_pos[1] - agent_pos[1]
    dz = card_pos[2] - agent_pos[2]
    dist = math.sqrt(dx**2 + dy**2 + dz**2)
    assert abs(dist - CoordSystem.CARD_RADIUS) < 2.0


def test_coord_system_all_positions():
    cs = CoordSystem()
    cs.assign_zones(["a", "b"])
    cs.assign_agent("agent-x", zone="a")
    cs.assign_card("card-1", agent_id="agent-x")
    positions = cs.all_positions()
    ids = {p.node_id for p in positions}
    assert "a" in ids
    assert "agent-x" in ids
    assert "card-1" in ids


def test_coord_system_apply_to_nodes():
    cs = CoordSystem()
    cs.assign_zones(["z1"])
    cs.assign_agent("agent-y", zone="z1")
    nodes = [
        Node3D(id="z1", node_type="zone", label="Z1"),
        Node3D(id="agent-y", node_type="agent", label="Agent Y"),
    ]
    updated = cs.apply_to_nodes(nodes)
    zone_node = next(n for n in updated if n.id == "z1")
    dist = math.sqrt(zone_node.x**2 + zone_node.y**2 + zone_node.z**2)
    assert dist > 0


# -- LayoutEngine ------------------------------------------------------------

def test_layout_engine_moves_nodes():
    nodes = [
        Node3D(id="n1", node_type="card", label="A", x=0.0, y=0.0, z=0.0),
        Node3D(id="n2", node_type="card", label="B", x=1.0, y=0.0, z=0.0),
        Node3D(id="n3", node_type="card", label="C", x=0.0, y=1.0, z=0.0),
    ]
    edges = [Edge3D(from_id="n1", to_id="n2", edge_type="card_to_card")]
    engine = LayoutEngine()
    result = engine.run_layout(nodes, edges, iterations=10)
    assert len(result) == 3
    # At least some nodes should have moved
    moved = sum(
        1 for orig, upd in zip(nodes, result)
        if abs(orig.x - upd.x) > 0.001 or abs(orig.y - upd.y) > 0.001
    )
    assert moved > 0


def test_layout_engine_empty():
    engine = LayoutEngine()
    result = engine.run_layout([], [], iterations=10)
    assert result == []


def test_layout_jitter():
    nodes = [Node3D(id="j1", node_type="card", label="J", x=0.0, y=0.0, z=0.0)]
    jittered = LayoutEngine.jitter(nodes, magnitude=5.0)
    assert len(jittered) == 1
    # Jittered should differ from original
    assert not (jittered[0].x == 0.0 and jittered[0].y == 0.0 and jittered[0].z == 0.0)


# -- SpatialQuery ------------------------------------------------------------

def _make_snapshot() -> Graph3DSnapshot:
    nodes = [
        Node3D(id="a", node_type="agent", label="Agent A", x=0.0, y=0.0, z=0.0),
        Node3D(id="b", node_type="card", label="Card B", x=5.0, y=0.0, z=0.0),
        Node3D(id="c", node_type="card", label="Card C", x=100.0, y=0.0, z=0.0),
        Node3D(id="d", node_type="pattern", label="Pattern D", x=3.0, y=4.0, z=0.0),
    ]
    edges = [
        Edge3D(from_id="a", to_id="b", edge_type="agent_to_card"),
        Edge3D(from_id="b", to_id="d", edge_type="card_to_card"),
    ]
    return Graph3DSnapshot(nodes=nodes, edges=edges)


def test_spatial_nearest_neighbors():
    sq = SpatialQuery(_make_snapshot())
    results = sq.nearest_neighbors((0.0, 0.0, 0.0), k=2)
    assert results[0].id == "a"  # closest is origin
    assert results[1].id == "b"  # second closest at distance 5


def test_spatial_nearest_by_type():
    sq = SpatialQuery(_make_snapshot())
    results = sq.nearest_neighbors((0.0, 0.0, 0.0), k=10, node_type="card")
    assert all(n.node_type == "card" for n in results)
    assert results[0].id == "b"


def test_spatial_bounding_box():
    sq = SpatialQuery(_make_snapshot())
    results = sq.bounding_box(-1, -1, -1, 10, 10, 10)
    ids = {n.id for n in results}
    assert "a" in ids
    assert "b" in ids
    assert "d" in ids
    assert "c" not in ids  # x=100 is outside


def test_spatial_shortest_path():
    sq = SpatialQuery(_make_snapshot())
    path = sq.shortest_path_3d("a", "d")
    assert len(path) == 3
    assert path[0].id == "a"
    assert path[-1].id == "d"


def test_spatial_path_not_found():
    sq = SpatialQuery(_make_snapshot())
    path = sq.shortest_path_3d("a", "c")  # no edge a->c
    assert path == []


def test_spatial_stats():
    sq = SpatialQuery(_make_snapshot())
    stats = sq.stats()
    assert stats["total_nodes"] == 4
    assert stats["total_edges"] == 2
    assert stats["nodes_by_type"]["card"] == 2


# -- Graph3DSync from micro DB -----------------------------------------------

@pytest.mark.asyncio
async def test_sync_from_micro_empty(db: MicroDB):
    syncer = Graph3DSync()
    snap = await syncer.from_micro(db)
    assert isinstance(snap, Graph3DSnapshot)
    assert snap.nodes == []


@pytest.mark.asyncio
async def test_sync_from_micro_with_cards(db: MicroDB):
    from datetime import datetime, timezone
    now = datetime.now(timezone.utc).isoformat()
    await db.insert_card({
        "id": "card-sync-1",
        "card_type": "memory",
        "title": "Sync Test Card",
        "schema_json": "{}",
        "zone": "engineering",
        "created_at": now,
        "updated_at": now,
    })
    syncer = Graph3DSync()
    snap = await syncer.from_micro(db)
    assert len(snap.nodes) == 1
    assert snap.nodes[0].id == "card-sync-1"
    assert snap.nodes[0].label == "Sync Test Card"


@pytest.mark.asyncio
async def test_sync_persist_and_load(db: MicroDB):
    nodes = [
        Node3D(id="p1", node_type="card", label="Pos Card 1", x=10.0, y=20.0, z=30.0),
        Node3D(id="p2", node_type="agent", label="Pos Agent", x=50.0, y=60.0, z=70.0),
    ]
    snap = Graph3DSnapshot(nodes=nodes)
    syncer = Graph3DSync()
    count = await syncer.persist_positions(snap, db)
    assert count == 2

    loaded = await syncer.load_positions(db)
    assert "p1" in loaded
    assert loaded["p1"] == (10.0, 20.0, 30.0)
    assert loaded["p2"] == (50.0, 60.0, 70.0)