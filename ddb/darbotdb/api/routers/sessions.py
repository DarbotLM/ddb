"""API routes for backend-agnostic DDB session management."""

from __future__ import annotations

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field

from darbotdb.session.models import SessionContext, SessionScope
from darbotdb.session.service import SessionService

router = APIRouter()
_service = SessionService()


class CreateSessionRequest(BaseModel):
    db_id: str | None = None
    session_id: str | None = None
    agent_id: str | None = None
    zone: str | None = None
    scope: SessionScope = SessionScope.SESSION
    metadata: dict[str, str] = Field(default_factory=dict)


@router.get("")
async def list_sessions():
    sessions = await _service.list_sessions()
    return {"sessions": [s.model_dump() for s in sessions]}


@router.post("")
async def create_or_open_session(body: CreateSessionRequest):
    try:
        db = await _service.ensure_context(SessionContext(**body.model_dump()))
        try:
            return {
                "db_id": db.path.stem,
                "path": str(db.path),
                "db_type": await db.get_meta("db_type"),
                "agent_id": await db.get_meta("agent_id"),
                "zone": await db.get_meta("zone"),
                "status": "ready",
            }
        finally:
            await db.close()
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))


@router.get("/{db_id}")
async def session_status(db_id: str):
    try:
        return await _service.get_session_status(db_id)
    except Exception as e:
        raise HTTPException(status_code=404, detail=str(e))