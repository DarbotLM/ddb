"""API routes for txt2kg knowledge graph integration."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel

from darbotdb.config import settings as _settings
from micro.engine import MicroDB
from micro.manager import MicroDBManager
from txt2kg.bridge import Txt2KGBridge
from txt2kg.client import Txt2KGClient, Txt2KGConfig

router = APIRouter()
_manager = MicroDBManager(Path(_settings.MICRO_DATA_ROOT))
_config = Txt2KGConfig(base_url=_settings.TXT2KG_URL, ddb_url=_settings.TXT2KG_DDB_URL, ddb_name=_settings.TXT2KG_DDB_NAME, ollama_url=_settings.TXT2KG_OLLAMA_URL, default_model=_settings.TXT2KG_MODEL)


class ExtractRequest(BaseModel):
    text: str
    model: str | None = None


class StoreRequest(BaseModel):
    triples: list[dict[str, Any]]


class RAGRequest(BaseModel):
    query: str
    top_k: int = 5


class BridgeRequest(BaseModel):
    db_id: str
    zone: str = "txt2kg"
    query: str | None = None


@router.get("/status")
async def txt2kg_status():
    async with Txt2KGClient(_config) as client:
        try:
            conn = await client.test_connection()
            stats = await client.graph_stats()
            return {"status": "connected", "host": _config.base_url, "ollama": conn, "graph": stats.model_dump()}
        except Exception as e:
            return {"status": "error", "host": _config.base_url, "error": str(e)}


@router.post("/extract")
async def extract_triples(body: ExtractRequest):
    async with Txt2KGClient(_config) as client:
        result = await client.extract(body.text, model=body.model)
        return {"triples": [t.model_dump(by_alias=True) for t in result.triples], "model": result.model, "duration_ms": result.duration_ms, "chunk_count": result.chunk_count}


@router.post("/store")
async def store_triples(body: StoreRequest):
    from txt2kg.models import Triple
    async with Txt2KGClient(_config) as client:
        triples = [Triple(**t) for t in body.triples]
        return await client.store_triples(triples)


@router.post("/rag")
async def rag_search(body: RAGRequest):
    async with Txt2KGClient(_config) as client:
        return (await client.rag_search(body.query, top_k=body.top_k)).model_dump()


@router.post("/rag/answer")
async def rag_answer(body: RAGRequest):
    async with Txt2KGClient(_config) as client:
        return {"answer": await client.rag_answer(body.query)}


@router.get("/stats")
async def graph_stats():
    async with Txt2KGClient(_config) as client:
        return (await client.graph_stats()).model_dump()


@router.get("/models")
async def list_models():
    async with Txt2KGClient(_config) as client:
        return {"models": await client.list_models()}


@router.post("/bridge/push")
async def bridge_push_cards(body: BridgeRequest):
    dbs = _manager.list_dbs()
    match = [p for p in dbs if body.db_id in p.stem]
    if not match:
        raise HTTPException(status_code=404, detail=f"Micro DB '{body.db_id}' not found")
    db = MicroDB(match[0])
    await db.open()
    try:
        async with Txt2KGClient(_config) as client:
            bridge = Txt2KGBridge(client)
            result = await bridge.cards_to_triples(db)
            return {"triples_extracted": len(result.triples), "model": result.model, "source_db": body.db_id}
    finally:
        await db.close()


@router.post("/bridge/pull")
async def bridge_pull_triples(body: BridgeRequest):
    dbs = _manager.list_dbs()
    match = [p for p in dbs if body.db_id in p.stem]
    if not match:
        raise HTTPException(status_code=404, detail=f"Micro DB '{body.db_id}' not found")
    db = MicroDB(match[0])
    await db.open()
    try:
        async with Txt2KGClient(_config) as client:
            bridge = Txt2KGBridge(client)
            count = await bridge.import_triples_as_cards(db, query=body.query, zone=body.zone)
            return {"cards_created": count, "zone": body.zone, "target_db": body.db_id}
    finally:
        await db.close()


@router.post("/bridge/recall")
async def bridge_recall(body: RAGRequest):
    async with Txt2KGClient(_config) as client:
        bridge = Txt2KGBridge(client)
        cards = await bridge.recall_from_kg(body.query)
        return {"cards": [c.to_agent_json() for c in cards]}


@router.post("/bridge/thoughts")
async def bridge_thoughts_to_kg(body: BridgeRequest):
    dbs = _manager.list_dbs()
    match = [p for p in dbs if body.db_id in p.stem]
    if not match:
        raise HTTPException(status_code=404, detail=f"Micro DB '{body.db_id}' not found")
    db = MicroDB(match[0])
    await db.open()
    try:
        async with Txt2KGClient(_config) as client:
            bridge = Txt2KGBridge(client)
            result = await bridge.thoughts_to_kg(db)
            return {"triples_extracted": len(result.triples), "source_db": body.db_id, "note": "O/O/S thoughts pushed to knowledge graph"}
    finally:
        await db.close()