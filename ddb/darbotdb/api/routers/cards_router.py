"""API routes for adaptive card CRUD, search, and composition."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field

from cards.builder import CardBuilder
from cards.schema import CardType
from cards.validator import validate_card
from darbotdb.config import settings
from darbotdb.session.service import SessionService
from micro.engine import MicroDB
from micro.manager import MicroDBManager

router = APIRouter()
_manager = MicroDBManager(Path(settings.MICRO_DATA_ROOT))
_service = SessionService()


class CardCreateRequest(BaseModel):
    db_id: str | None = None
    card_type: str = "memory"
    title: str
    content: str = ""
    agent_id: str | None = None
    zone: str | None = None
    tags: list[str] = Field(default_factory=list)
    parent_card_id: str | None = None
    facts: dict[str, str] = Field(default_factory=dict)
    manifest_id: str | None = None
    view_id: str | None = None
    entity_id: str | None = None
    entity_kind: str | None = None
    spatial: dict[str, Any] = Field(default_factory=dict)
    view_hints: dict[str, Any] = Field(default_factory=dict)


class CardSearchRequest(BaseModel):
    db_id: str
    query: str
    zone: str | None = None
    card_type: str | None = None
    limit: int = 50


class ComposeRequest(BaseModel):
    card_ids: list[str]
    title: str
    zone: str | None = None
    manifest_id: str | None = None


def _resolve_db(db_id: str) -> MicroDB:
    dbs = _manager.list_dbs()
    match = [p for p in dbs if db_id in p.stem]
    if not match:
        raise HTTPException(status_code=404, detail=f"Micro DB '{db_id}' not found")
    return MicroDB(match[0])


@router.post("")
async def create_card(body: CardCreateRequest):
    try:
        ct = CardType(body.card_type)
    except ValueError:
        raise HTTPException(status_code=400, detail=f"Invalid card_type: {body.card_type}")

    builder = CardBuilder().card_type(ct).title(body.title)
    if body.content:
        builder = builder.text(body.content)
    for k, v in body.facts.items():
        builder = builder.fact(k, v)
    if body.agent_id:
        builder = builder.agent(body.agent_id)
    if body.zone:
        builder = builder.zone(body.zone)
    if body.tags:
        builder = builder.tag(*body.tags)
    if body.parent_card_id:
        builder = builder.parent(body.parent_card_id)
    if body.manifest_id:
        builder = builder.manifest(body.manifest_id, body.view_id)
    if body.entity_id and body.entity_kind:
        builder = builder.entity(body.entity_id, body.entity_kind).inspector_for(body.entity_id)
    if body.spatial:
        builder = builder.spatial(**body.spatial)
    if body.view_hints:
        builder = builder.view_hint(**body.view_hints)

    card = builder.build()
    errors = validate_card(card)
    if errors:
        raise HTTPException(status_code=400, detail=errors)

    if body.db_id:
        db = _resolve_db(body.db_id)
        await db.open()
        try:
            await db.insert_card({
                "id": card.ddb.id,
                "card_type": card.ddb.card_type.value,
                "title": body.title,
                "schema_json": card.to_agent_json(),
                "content_md": card.to_human_summary(),
                "parent_card_id": body.parent_card_id,
                "tags": card.ddb.tags,
                "zone": card.ddb.zone,
            })
        finally:
            await db.close()

    return card.to_agent_json()


@router.post("/search")
async def search_cards(body: CardSearchRequest):
    try:
        results = await _service.micro.search_cards(body.db_id, body.query, limit=body.limit)
        if body.zone:
            results = [r for r in results if r.get("zone") == body.zone]
        if body.card_type:
            results = [r for r in results if r.get("card_type") == body.card_type]
        return {"query": body.query, "results": results}
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))


@router.post("/compose")
async def compose_cards(body: ComposeRequest):
    card = (
        CardBuilder()
        .card_type(CardType.INDEX)
        .title(body.title)
        .fact("Referenced Cards", str(len(body.card_ids)))
        .container([{"type": "TextBlock", "text": f"Index: {', '.join(body.card_ids)}", "wrap": True}])
        .view_hint(composition="index")
    )
    if body.zone:
        card = card.zone(body.zone)
    if body.manifest_id:
        card = card.manifest(body.manifest_id)
    return card.build().to_agent_json()


@router.get("/{db_id}/{card_id}/tree")
async def card_tree(db_id: str, card_id: str, depth: int = 3):
    db = _resolve_db(db_id)
    await db.open()
    try:
        tree = await db.get_card_tree(card_id, depth=depth)
        return {"card_id": card_id, "depth": depth, "tree": tree}
    finally:
        await db.close()