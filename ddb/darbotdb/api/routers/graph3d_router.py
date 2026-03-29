"""API routes for the 3D knowledge graph (/v1/3dkg)."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field

from graph3d.models import Node3D, Graph3DSnapshot
from graph3d.queries import SpatialQuery
from graph3d.sync import Graph3DSync
from micro.engine import MicroDB
from micro.manager import MicroDBManager

router = APIRouter()
_manager = MicroDBManager(Path("data"))


def _resolve_db(db_id: str) -> MicroDB:
    dbs = _manager.list_dbs()
    match = [p for p in dbs if db_id in p.stem]
    if not match:
        raise HTTPException(status_code=404, detail=f"Micro DB '{db_id}' not found")
    return MicroDB(match[0])


class NearestRequest(BaseModel):
    db_id: str
    x: float
    y: float
    z: float
    k: int = 10
    node_type: str | None = None


class BBoxRequest(BaseModel):
    db_id: str
    min_x: float
    min_y: float
    min_z: float
    max_x: float
    max_y: float
    max_z: float
    node_type: str | None = None


class PathRequest(BaseModel):
    db_id: str
    from_id: str
    to_id: str


class LayoutRequest(BaseModel):
    db_id: str
    iterations: int = 50
    jitter: bool = True


class SyncRequest(BaseModel):
    db_id: str
    use_arango: bool = False


@router.get("/snapshot")
async def snapshot(db_id: str):
    """Return a complete 3D graph snapshot from a micro DB."""
    db = _resolve_db(db_id)
    await db.open()
    try:
        syncer = Graph3DSync(use_coord_system=True)
        snap = await syncer.from_micro(db)
        # Apply coord system for initial placement
        snap = syncer.apply_coord_system(snap)
        # Load any persisted positions
        persisted = await syncer.load_positions(db)
        nodes = [
            n.model_copy(update={"x": persisted[n.id][0], "y": persisted[n.id][1], "z": persisted[n.id][2]})
            if n.id in persisted else n
            for n in snap.nodes
        ]
        return Graph3DSnapshot(nodes=nodes, edges=snap.edges).model_dump()
    finally:
        await db.close()


@router.get("/node/{node_id}")
async def get_node(node_id: str, db_id: str):
    """Get a single node with its 3D position."""
    db = _resolve_db(db_id)
    await db.open()
    try:
        row = await db.get_card(node_id)
        if not row:
            raise HTTPException(status_code=404, detail=f"Node '{node_id}' not found")
        pos_rows = await db.execute(
            "SELECT x, y, z FROM node_positions WHERE node_id = ? LIMIT 1", (node_id,)
        )
        x, y, z = (pos_rows[0]["x"], pos_rows[0]["y"], pos_rows[0]["z"]) if pos_rows else (0.0, 0.0, 0.0)
        return Node3D(
            id=node_id,
            node_type=row.get("card_type", "card"),
            label=row.get("title", ""),
            x=x, y=y, z=z,
            zone=row.get("zone"),
        ).model_dump()
    finally:
        await db.close()


@router.post("/nearest")
async def nearest_neighbors(body: NearestRequest):
    """Find k-nearest nodes to a given 3D point."""
    db = _resolve_db(body.db_id)
    await db.open()
    try:
        syncer = Graph3DSync(use_coord_system=False)
        snap = await syncer.from_micro(db)
        persisted = await syncer.load_positions(db)
        nodes = [
            n.model_copy(update={"x": persisted[n.id][0], "y": persisted[n.id][1], "z": persisted[n.id][2]})
            if n.id in persisted else n
            for n in snap.nodes
        ]
        snap_with_pos = Graph3DSnapshot(nodes=nodes, edges=snap.edges)
        sq = SpatialQuery(snap_with_pos)
        results = sq.nearest_neighbors((body.x, body.y, body.z), k=body.k, node_type=body.node_type)
        return {"point": {"x": body.x, "y": body.y, "z": body.z}, "results": [n.model_dump() for n in results]}
    finally:
        await db.close()


@router.post("/bbox")
async def bounding_box(body: BBoxRequest):
    """Find nodes within an axis-aligned bounding box."""
    db = _resolve_db(body.db_id)
    await db.open()
    try:
        syncer = Graph3DSync(use_coord_system=False)
        snap = await syncer.from_micro(db)
        persisted = await syncer.load_positions(db)
        nodes = [
            n.model_copy(update={"x": persisted[n.id][0], "y": persisted[n.id][1], "z": persisted[n.id][2]})
            if n.id in persisted else n
            for n in snap.nodes
        ]
        sq = SpatialQuery(Graph3DSnapshot(nodes=nodes, edges=snap.edges))
        results = sq.bounding_box(
            body.min_x, body.min_y, body.min_z,
            body.max_x, body.max_y, body.max_z,
            node_type=body.node_type,
        )
        return {"bbox": body.model_dump(exclude={"db_id", "node_type"}), "results": [n.model_dump() for n in results]}
    finally:
        await db.close()


@router.post("/path")
async def shortest_path(body: PathRequest):
    """Shortest path between two nodes in 3D graph space."""
    db = _resolve_db(body.db_id)
    await db.open()
    try:
        syncer = Graph3DSync(use_coord_system=False)
        snap = await syncer.from_micro(db)
        persisted = await syncer.load_positions(db)
        nodes = [
            n.model_copy(update={"x": persisted[n.id][0], "y": persisted[n.id][1], "z": persisted[n.id][2]})
            if n.id in persisted else n
            for n in snap.nodes
        ]
        sq = SpatialQuery(Graph3DSnapshot(nodes=nodes, edges=snap.edges))
        path = sq.shortest_path_3d(body.from_id, body.to_id)
        return {
            "from_id": body.from_id,
            "to_id": body.to_id,
            "hops": len(path) - 1 if path else -1,
            "path": [n.model_dump() for n in path],
        }
    finally:
        await db.close()


@router.post("/layout")
async def recompute_layout(body: LayoutRequest):
    """Recompute force-directed layout and persist updated positions."""
    db = _resolve_db(body.db_id)
    await db.open()
    try:
        syncer = Graph3DSync(layout_iterations=body.iterations, use_coord_system=True)
        snap = await syncer.full_sync(micro_db=db)
        return {
            "db_id": body.db_id,
            "nodes_updated": len(snap.nodes),
            "edges": len(snap.edges),
            "computed_at": snap.computed_at,
        }
    finally:
        await db.close()


@router.post("/sync")
async def sync_graph(body: SyncRequest):
    """Push current micro DB (and optionally ArangoDB) state into 3dkg positions."""
    db = _resolve_db(body.db_id)
    await db.open()
    try:
        arango_db = None
        if body.use_arango:
            from arango import ArangoClient
            from darbotdb.config import settings
            client = ArangoClient(hosts=settings.DDB_HOSTS)
            arango_db = client.db(settings.DDB_DATABASE, username=settings.DDB_USERNAME, password=settings.DDB_PASSWORD)

        syncer = Graph3DSync(use_coord_system=True)
        snap = await syncer.full_sync(micro_db=db, arango_db=arango_db)
        return {
            "db_id": body.db_id,
            "status": "synced",
            "nodes": len(snap.nodes),
            "edges": len(snap.edges),
            "computed_at": snap.computed_at,
        }
    finally:
        await db.close()


@router.get("/stats")
async def graph_stats(db_id: str):
    """Node and edge counts per type."""
    db = _resolve_db(db_id)
    await db.open()
    try:
        syncer = Graph3DSync(use_coord_system=False)
        snap = await syncer.from_micro(db)
        sq = SpatialQuery(snap)
        return sq.stats()
    finally:
        await db.close()