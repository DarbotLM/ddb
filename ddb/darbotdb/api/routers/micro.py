"""API routes for SQLite micro DB lifecycle and queries."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field

from darbotdb.config import settings
from micro.engine import MicroDB
from micro.manager import MicroDBManager

router = APIRouter()

_manager = MicroDBManager(Path(settings.MICRO_DATA_ROOT))


class CreateMicroRequest(BaseModel):
    agent_id: str | None = None
    session_id: str | None = None
    zone: str | None = None
    db_type: str = "agent"
    meta: dict[str, str] = Field(default_factory=dict)


class MicroQueryRequest(BaseModel):
    sql: str
    params: list[Any] = Field(default_factory=list)


@router.post("/create")
async def create_micro(body: CreateMicroRequest):
    try:
        if body.db_type == "zone" and body.zone:
            db = await _manager.create_zone_db(body.zone)
        elif body.db_type == "session" and body.session_id and body.agent_id:
            db = await _manager.create_session_db(body.session_id, body.agent_id)
        elif body.agent_id:
            db = await _manager.create_agent_db(body.agent_id, **body.meta)
        else:
            raise HTTPException(status_code=400, detail="Provide agent_id, session_id, or zone")
        path = str(db.path)
        await db.close()
        return {"path": path, "status": "created"}
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))


@router.get("/{db_id}/status")
async def micro_status(db_id: str):
    dbs = _manager.list_dbs()
    match = [p for p in dbs if db_id in p.stem]
    if not match:
        raise HTTPException(status_code=404, detail=f"Micro DB '{db_id}' not found")
    db = MicroDB(match[0])
    try:
        await db.open()
        cards = await db.execute("SELECT COUNT(*) as cnt FROM cards")
        turns = await db.execute("SELECT COUNT(*) as cnt FROM turns")
        thoughts = await db.execute("SELECT COUNT(*) as cnt FROM thoughts")
        schema_ver = await db.get_meta("schema_version")
        return {
            "path": str(match[0]),
            "schema_version": schema_ver,
            "cards": cards[0]["cnt"] if cards else 0,
            "turns": turns[0]["cnt"] if turns else 0,
            "thoughts": thoughts[0]["cnt"] if thoughts else 0,
        }
    finally:
        await db.close()


@router.post("/{db_id}/query")
async def micro_query(db_id: str, body: MicroQueryRequest):
    dbs = _manager.list_dbs()
    match = [p for p in dbs if db_id in p.stem]
    if not match:
        raise HTTPException(status_code=404, detail=f"Micro DB '{db_id}' not found")
    db = MicroDB(match[0])
    try:
        await db.open()
        rows = await db.execute(body.sql, tuple(body.params))
        await db.conn.commit()
        return {"results": rows}
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))
    finally:
        await db.close()


@router.delete("/{db_id}")
async def delete_micro(db_id: str):
    dbs = _manager.list_dbs()
    match = [p for p in dbs if db_id in p.stem]
    if not match:
        raise HTTPException(status_code=404, detail=f"Micro DB '{db_id}' not found")
    await _manager.delete_db(match[0])
    return {"status": "deleted"}


@router.get("/list")
async def list_micros():
    dbs = _manager.list_dbs()
    return {"databases": [{"path": str(p), "name": p.stem} for p in dbs]}
