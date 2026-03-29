"""Tests for the DDB micro database module."""

import json
from pathlib import Path

import pytest
import pytest_asyncio

from micro.engine import MicroDB
from micro.manager import MicroDBManager
from micro.schema import SCHEMA_VERSION


@pytest_asyncio.fixture
async def db(tmp_path: Path):
    path = tmp_path / "test.ddb"
    async with MicroDB(path) as _db:
        yield _db


@pytest.fixture
def manager(tmp_path: Path):
    return MicroDBManager(tmp_path)


# -- schema ------------------------------------------------------------------

@pytest.mark.asyncio
async def test_create_micro_db(tmp_path: Path):
    path = tmp_path / "new.ddb"
    db = MicroDB(path)
    await db.create(agent_id="agent-1")
    assert path.exists()
    ver = await db.get_meta("schema_version")
    assert ver == SCHEMA_VERSION
    agent = await db.get_meta("agent_id")
    assert agent == "agent-1"
    await db.close()


@pytest.mark.asyncio
async def test_open_existing(tmp_path: Path):
    path = tmp_path / "existing.ddb"
    db = MicroDB(path)
    await db.create()
    await db.close()
    db2 = MicroDB(path)
    await db2.open()
    ver = await db2.get_meta("schema_version")
    assert ver == SCHEMA_VERSION
    await db2.close()


# -- turns -------------------------------------------------------------------

@pytest.mark.asyncio
async def test_insert_and_get_turn(db: MicroDB):
    turn = {
        "conversation_id": "conv-1",
        "turn_number": 1,
        "role": "user",
        "content": "Hello world",
        "model": "claude-opus-4-6",
    }
    row_id = await db.insert_turn(turn)
    assert row_id == 1

    turns = await db.get_turns("conv-1")
    assert len(turns) == 1
    assert turns[0]["content"] == "Hello world"
    assert turns[0]["hash"] is not None


@pytest.mark.asyncio
async def test_turn_hash_deterministic(db: MicroDB):
    turn = {
        "conversation_id": "conv-1",
        "turn_number": 1,
        "role": "user",
        "content": "Same content",
    }
    await db.insert_turn(turn)
    turns = await db.get_turns("conv-1")
    hash1 = turns[0]["hash"]
    assert len(hash1) == 64  # SHA256 hex


# -- cards -------------------------------------------------------------------

@pytest.mark.asyncio
async def test_insert_and_search_card(db: MicroDB):
    card = {
        "id": "card-001",
        "card_type": "memory",
        "title": "API Rate Limiting Pattern",
        "schema_json": {"type": "AdaptiveCard", "body": []},
        "content_md": "Discovered a rate limiting pattern in the API gateway",
        "tags": ["api", "pattern", "rate-limit"],
        "zone": "engineering",
    }
    await db.insert_card(card)

    fetched = await db.get_card("card-001")
    assert fetched is not None
    assert fetched["title"] == "API Rate Limiting Pattern"
    assert fetched["hash"] is not None

    results = await db.search_cards("rate limiting")
    assert len(results) >= 1
    assert results[0]["id"] == "card-001"


@pytest.mark.asyncio
async def test_card_tree(db: MicroDB):
    parent = {
        "id": "root",
        "card_type": "index",
        "title": "Root Index",
        "schema_json": "{}",
    }
    child1 = {
        "id": "child-1",
        "card_type": "memory",
        "title": "Child Memory 1",
        "schema_json": "{}",
        "parent_card_id": "root",
    }
    child2 = {
        "id": "child-2",
        "card_type": "memory",
        "title": "Child Memory 2",
        "schema_json": "{}",
        "parent_card_id": "root",
    }
    grandchild = {
        "id": "grandchild-1",
        "card_type": "observation",
        "title": "Grandchild Observation",
        "schema_json": "{}",
        "parent_card_id": "child-1",
    }
    for c in (parent, child1, child2, grandchild):
        await db.insert_card(c)

    tree = await db.get_card_tree("root", depth=2)
    assert len(tree) == 4
    depths = {t["id"]: t["_depth"] for t in tree}
    assert depths["root"] == 0
    assert depths["child-1"] == 1
    assert depths["grandchild-1"] == 2


# -- thoughts ---------------------------------------------------------------

@pytest.mark.asyncio
async def test_insert_thought(db: MicroDB):
    thought = {
        "thought_number": 1,
        "total_thoughts": 3,
        "perspective": "observer",
        "thought": "The API returns 429 status codes under load",
        "assumptions": [],
        "observations": ["Measured 429 responses at 100 req/s"],
        "verification_level": "artifact",
    }
    row_id = await db.insert_thought(thought)
    assert row_id == 1

    thoughts = await db.get_thoughts("observer")
    assert len(thoughts) == 1
    assert thoughts[0]["perspective"] == "observer"


@pytest.mark.asyncio
async def test_thoughts_filter(db: MicroDB):
    for perspective in ("observer", "orchestrator", "synthesizer"):
        await db.insert_thought({
            "thought_number": 1,
            "total_thoughts": 1,
            "perspective": perspective,
            "thought": f"Thinking as {perspective}",
            "verification_level": "none",
        })
    all_thoughts = await db.get_thoughts()
    assert len(all_thoughts) == 3
    obs = await db.get_thoughts("observer")
    assert len(obs) == 1


# -- meta --------------------------------------------------------------------

@pytest.mark.asyncio
async def test_meta(db: MicroDB):
    await db.set_meta("custom_key", "custom_value")
    val = await db.get_meta("custom_key")
    assert val == "custom_value"
    # overwrite
    await db.set_meta("custom_key", "updated")
    val = await db.get_meta("custom_key")
    assert val == "updated"


# -- links -------------------------------------------------------------------

@pytest.mark.asyncio
async def test_insert_and_get_links(db: MicroDB):
    await db.insert_link({
        "link_type": "graph_node",
        "source_id": "card-001",
        "target_uri": "darbotdb://ddb/cards/card-key",
    })
    await db.insert_link({
        "link_type": "micro_db",
        "source_id": "card-001",
        "target_uri": "ddb://agent-2",
    })
    links = await db.get_links(source_id="card-001")
    assert len(links) == 2
    graph_links = await db.get_links(link_type="graph_node")
    assert len(graph_links) == 1


# -- manager -----------------------------------------------------------------

@pytest.mark.asyncio
async def test_manager_lifecycle(manager: MicroDBManager):
    db = await manager.create_agent_db("agent-alpha", owner="darbot")
    assert db.path.exists()
    agent_id = await db.get_meta("agent_id")
    assert agent_id == "agent-alpha"
    await db.close()

    dbs = manager.list_dbs()
    assert any("agent-alpha" in str(p) for p in dbs)

    db2 = await manager.open_db(db.path)
    assert await db2.get_meta("agent_id") == "agent-alpha"
    await db2.close()

    await manager.delete_db(db.path)
    assert not db.path.exists()
