"""API routes for AG-UI protocol -- SSE streaming agent sessions."""

from __future__ import annotations

from pathlib import Path

from fastapi import APIRouter, HTTPException
from fastapi.responses import StreamingResponse
from pydantic import BaseModel, Field

from agui.adapter import from_agui_messages
from agui.models import AGUIMessage, RunAgentInput
from agui.session import AGUISession
from darbotdb.config import settings
from micro.engine import MicroDB
from micro.manager import MicroDBManager

router = APIRouter()
_manager = MicroDBManager(Path(settings.MICRO_DATA_ROOT))


@router.post("/run")
async def agui_run(body: RunAgentInput):
    agent_id = body.state.get("agent_id", body.thread_id)
    dbs = _manager.list_dbs()
    match = [p for p in dbs if agent_id in p.stem]
    if match:
        db = MicroDB(match[0])
        await db.open()
    else:
        db = await _manager.create_agent_db(agent_id)
    session = AGUISession(db)

    async def generate():
        try:
            async for event in session.run(body):
                yield event.to_sse()
        finally:
            await db.close()

    return StreamingResponse(generate(), media_type="text/event-stream", headers={"Cache-Control": "no-cache", "Connection": "keep-alive", "X-DDB-Thread": body.thread_id, "X-DDB-Run": body.run_id})


@router.post("/replay/{conversation_id}")
async def agui_replay(conversation_id: str, agent_id: str | None = None):
    search_id = agent_id or conversation_id
    dbs = _manager.list_dbs()
    match = [p for p in dbs if search_id in p.stem]
    if not match:
        raise HTTPException(status_code=404, detail=f"No micro DB found for '{search_id}'")
    db = MicroDB(match[0])
    await db.open()

    async def generate():
        try:
            async for event in AGUISession(db).replay_as_agui(conversation_id):
                yield event.to_sse()
        finally:
            await db.close()

    return StreamingResponse(generate(), media_type="text/event-stream")


class IngestRequest(BaseModel):
    thread_id: str = Field(alias="threadId")
    messages: list[AGUIMessage]
    model_config = {"populate_by_name": True}


@router.post("/ingest")
async def agui_ingest(body: IngestRequest):
    dbs = _manager.list_dbs()
    match = [p for p in dbs if body.thread_id in p.stem]
    if match:
        db = MicroDB(match[0])
        await db.open()
    else:
        db = await _manager.create_agent_db(body.thread_id)
    turns = from_agui_messages(body.messages)
    count = 0
    for turn in turns:
        turn["conversation_id"] = body.thread_id
        await db.insert_turn(turn)
        count += 1
    await db.close()
    return {"thread_id": body.thread_id, "turns_ingested": count}