"""API routes for the Observer/Orchestrator/Synthesizer triad engine."""

from __future__ import annotations

from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel, Field
from arango import ArangoClient

from darbotdb.config import settings
from darbotdb.session.models import SessionContext, SessionScope
from darbotdb.session.service import SessionService
from graph.queries import DDBQueries
from micro.engine import MicroDB
from micro.manager import MicroDBManager
from triad.engine import TriadEngine
from triad.models import DDBEvent

router = APIRouter()
_service = SessionService()
_manager = MicroDBManager(Path(settings.MICRO_DATA_ROOT))


def get_queries() -> DDBQueries:
    client = ArangoClient(hosts=settings.DDB_HOSTS)
    db = client.db(settings.DDB_DATABASE, username=settings.DDB_USERNAME, password=settings.DDB_PASSWORD)
    return DDBQueries(db)


class TriadProcessRequest(BaseModel):
    db_id: str | None = None
    event_type: str
    source_agent: str | None = None
    payload: dict[str, Any] = Field(default_factory=dict)


def _resolve_db(db_id: str) -> MicroDB:
    dbs = _manager.list_dbs()
    match = [p for p in dbs if db_id in p.stem]
    if not match:
        raise HTTPException(status_code=404, detail=f"Micro DB '{db_id}' not found")
    return MicroDB(match[0])


@router.post("/process")
async def triad_process(body: TriadProcessRequest):
    db: MicroDB | None = None
    event_id: int | None = None
    try:
        if body.db_id:
            db = _resolve_db(body.db_id)
            await db.open()
        else:
            db = await _service.ensure_context(SessionContext(agent_id=body.source_agent or "triad", scope=SessionScope.AGENT))
        event_id = await db.insert_event({
            "event_type": body.event_type,
            "source_agent": body.source_agent,
            "payload": body.payload,
            "triad_status": "processing",
        })
        event = DDBEvent(event_type=body.event_type, source_agent=body.source_agent, payload=body.payload)
        engine = TriadEngine(db)
        new_cards = await engine.process(event)
        thoughts = await db.get_thoughts()
        obs = sum(1 for t in thoughts if t["perspective"] == "observer")
        orch = sum(1 for t in thoughts if t["perspective"] == "orchestrator")
        syn = sum(1 for t in thoughts if t["perspective"] == "synthesizer")
        await db.update_event(
            event_id,
            triad_status="complete",
            observer_thoughts=obs,
            orchestrator_thoughts=orch,
            synthesizer_thoughts=syn,
            new_cards=len(new_cards),
            processed_at=datetime.now(timezone.utc).isoformat(),
        )
        return {
            "event_type": body.event_type,
            "event_id": event_id,
            "db_id": db.path.stem,
            "status": "complete",
            "new_cards": len(new_cards),
            "card_ids": [c.ddb.id for c in new_cards],
            "observer_thoughts": obs,
            "orchestrator_thoughts": orch,
            "synthesizer_thoughts": syn,
        }
    except Exception as e:
        if db is not None and event_id is not None:
            await db.update_event(event_id, triad_status="error", error_message=str(e))
        raise HTTPException(status_code=500, detail=str(e))
    finally:
        if db is not None:
            await db.close()


@router.get("/thoughts/{db_id}")
async def get_thoughts(db_id: str, perspective: str | None = None):
    db = _resolve_db(db_id)
    await db.open()
    try:
        return {"db_id": db_id, "perspective": perspective, "thoughts": await db.get_thoughts(perspective=perspective)}
    finally:
        await db.close()


@router.get("/events/{db_id}")
async def get_events(db_id: str, event_type: str | None = None, triad_status: str | None = None, limit: int = 50):
    db = _resolve_db(db_id)
    await db.open()
    try:
        return {"db_id": db_id, "events": await db.get_events(event_type=event_type, triad_status=triad_status, limit=limit)}
    finally:
        await db.close()


@router.get("/patterns")
async def list_patterns(min_confidence: float = 0.0, limit: int = 50, q: DDBQueries = Depends(get_queries)):
    try:
        return {"min_confidence": min_confidence, "limit": limit, "patterns": q.patterns_by_confidence(min_confidence, limit)}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))