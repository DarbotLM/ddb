"""API routes for memory recall and radial data zones."""

from __future__ import annotations

from datetime import datetime, timezone
from pathlib import Path

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field
from arango import ArangoClient

from cards.builder import CardBuilder
from cards.schema import CardType
from darbotdb.config import settings
from darbotdb.session.models import SessionContext, SessionScope
from darbotdb.session.service import SessionService
from graph.queries import DDBQueries
from micro.manager import MicroDBManager

router = APIRouter()
_service = SessionService()
_manager = MicroDBManager(Path(settings.MICRO_DATA_ROOT))


def get_queries() -> DDBQueries:
    client = ArangoClient(hosts=settings.DDB_HOSTS)
    db = client.db(settings.DDB_DATABASE, username=settings.DDB_USERNAME, password=settings.DDB_PASSWORD)
    return DDBQueries(db)


class RecallRequest(BaseModel):
    db_id: str | None = None
    query: str
    zone: str | None = None
    agent_id: str | None = None
    depth: int = 3
    limit: int = 20


class MemoryStoreRequest(BaseModel):
    db_id: str | None = None
    card_type: str = "memory"
    title: str
    content: str = ""
    zone: str | None = None
    tags: list[str] = Field(default_factory=list)
    agent_id: str | None = None


class ZoneCreateRequest(BaseModel):
    name: str
    description: str = ""
    visibility: str = "shared"


@router.post("/recall")
async def memory_recall(body: RecallRequest):
    try:
        return await _service.recall(query=body.query, db_id=body.db_id, zone=body.zone, limit=body.limit)
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))


@router.post("/store")
async def memory_store(body: MemoryStoreRequest):
    try:
        if body.db_id:
            db = _service.micro.resolve_db(body.db_id)
            await db.open()
        else:
            scope = SessionScope.AGENT if body.agent_id else SessionScope.ZONE
            db = await _service.ensure_context(SessionContext(agent_id=body.agent_id, zone=body.zone, scope=scope))
        try:
            card_type = CardType(body.card_type)
            card = (
                CardBuilder()
                .card_type(card_type)
                .title(body.title)
                .text(body.content)
                .tag(*body.tags)
                .zone(body.zone or "memory")
                .agent(body.agent_id or "memory")
                .projection_source("memory-store")
                .build()
            )
            await db.insert_card({
                "id": card.ddb.id,
                "card_type": card.ddb.card_type.value,
                "title": body.title,
                "schema_json": card.to_agent_json(),
                "content_md": card.to_human_summary(),
                "tags": card.ddb.tags,
                "zone": card.ddb.zone,
                "created_at": datetime.now(timezone.utc).isoformat(),
                "updated_at": datetime.now(timezone.utc).isoformat(),
            })
            return {"status": "stored", "card_id": card.ddb.id, "db_id": body.db_id or db.path.stem}
        finally:
            await db.close()
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))


@router.get("/zones")
async def list_zones():
    try:
        zones = get_queries()._run("FOR z IN memory_zones RETURN z")
        return {"zones": zones}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@router.post("/zones")
async def create_zone(body: ZoneCreateRequest):
    try:
        db = await _manager.create_zone_db(body.name)
        await db.close()
        client = ArangoClient(hosts=settings.DDB_HOSTS)
        graph_db = client.db(settings.DDB_DATABASE, username=settings.DDB_USERNAME, password=settings.DDB_PASSWORD)
        zones = graph_db.collection("memory_zones")
        key = body.name.lower().replace(" ", "-")
        doc = {"_key": key, "name": body.name, "description": body.description, "visibility": body.visibility}
        if zones.has(key):
            zones.update(doc)
        else:
            zones.insert(doc)
        return {"name": body.name, "visibility": body.visibility, "description": body.description, "status": "created"}
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))


@router.get("/zones/{zone_name}")
async def get_zone(zone_name: str):
    try:
        contents = get_queries().zone_contents(zone_name)
        return {"zone": zone_name, "contents": contents}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))