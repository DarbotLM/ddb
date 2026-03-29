"""API routes for DDB graph traversal, manifests, and pattern discovery."""

from __future__ import annotations

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel, Field
from arango import ArangoClient

from darbotdb.config import settings
from darbotdb.session.service import SessionService
from graph.queries import DDBQueries

router = APIRouter()
_service = SessionService()


def get_queries() -> DDBQueries:
    client = ArangoClient(hosts=settings.DDB_HOSTS)
    db = client.db(settings.DDB_DATABASE, username=settings.DDB_USERNAME, password=settings.DDB_PASSWORD)
    return DDBQueries(db)


class TraverseRequest(BaseModel):
    start_key: str
    collection: str = "cards"
    edge_collection: str = "card_to_card"
    depth: int = 3
    direction: str = "outbound"


class PatternRequest(BaseModel):
    min_confidence: float = 0.5
    since: str | None = None
    limit: int = 20


class LinkRequest(BaseModel):
    from_collection: str
    from_key: str
    to_collection: str
    to_key: str
    edge_collection: str
    metadata: dict = Field(default_factory=dict)


@router.post("/traverse")
async def graph_traverse(body: TraverseRequest, q: DDBQueries = Depends(get_queries)):
    try:
        if body.collection == "cards":
            results = q.card_tree(body.start_key, body.depth)
        elif body.collection == "agents":
            results = q.agent_connections(body.start_key, body.depth)
        elif body.collection == "manifests":
            manifest = q.manifest_scene(body.start_key)
            results = manifest["nodes"] if manifest else []
        else:
            results = []
        return {"results": results}
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))


@router.post("/pattern")
async def discover_patterns(body: PatternRequest, q: DDBQueries = Depends(get_queries)):
    try:
        if body.since:
            results = q.remember_forward(body.since, body.limit)
        else:
            results = q.patterns_by_confidence(body.min_confidence, body.limit)
        return {"results": results}
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))


@router.get("/agents")
async def list_agents(q: DDBQueries = Depends(get_queries)):
    try:
        return {"agents": q._run("FOR a IN agents RETURN a")}
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))


@router.post("/link")
async def create_link(body: LinkRequest, q: DDBQueries = Depends(get_queries)):
    try:
        edge = {"_from": f"{body.from_collection}/{body.from_key}", "_to": f"{body.to_collection}/{body.to_key}", **body.metadata}
        result = q.db.collection(body.edge_collection).insert(edge)
        return {"created": result}
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))


@router.get("/zones/{zone_name}")
async def zone_contents(zone_name: str, q: DDBQueries = Depends(get_queries)):
    try:
        return {"zone": zone_name, "contents": q.zone_contents(zone_name)}
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))


@router.get("/manifests")
async def list_manifests(source_db_id: str | None = None, limit: int = 50, q: DDBQueries = Depends(get_queries)):
    try:
        return {"manifests": q.list_manifests(source_db_id=source_db_id, limit=limit)}
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))


@router.get("/manifests/{manifest_id}")
async def get_manifest_scene(manifest_id: str, db_id: str | None = None):
    try:
        return await _service.materialize_scene(manifest_id, db_id=db_id)
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))