"""Tests for the DBSession abstraction layer."""

from __future__ import annotations

from pathlib import Path

import pytest
import pytest_asyncio

from micro.engine import MicroDB
from session.models import NodeRecord, EdgeRecord, TripleRecord
from session.micro_session import MicroDBSession
from session.hybrid_session import HybridSession
from session.factory import SessionFactory


@pytest_asyncio.fixture
async def micro_session(tmp_path: Path) -> MicroDBSession:
    db = MicroDB(tmp_path / "session_test.ddb")
    await db.create(agent_id="test-agent")
    session = MicroDBSession(db)
    yield session
    await session.close()


# -- NodeRecord / EdgeRecord / TripleRecord serialisation --------------------

def test_node_record_defaults():
    node = NodeRecord(entity_type="card", label="My Card")
    assert node.id  # auto-generated UUID
    assert node.entity_type == "card"
    assert node.properties == {}


def test_edge_record_defaults():
    edge = EdgeRecord(edge_type="card_to_card", from_id="a", to_id="b")
    assert edge.weight == 1.0


def test_triple_record_defaults():
    triple = TripleRecord(subject="Alice", predicate="knows", object="Bob")
    assert triple.confidence == 1.0


# -- MicroDBSession ----------------------------------------------------------

@pytest.mark.asyncio
async def test_micro_session_put_get(micro_session: MicroDBSession):
    node = NodeRecord(id="node-1", entity_type="card", label="Test Card", zone="eng")
    stored_id = await micro_session.put(node)
    assert stored_id == "node-1"

    fetched = await micro_session.get("card", "node-1")
    assert fetched is not None
    assert fetched.label == "Test Card"
    assert fetched.zone == "eng"


@pytest.mark.asyncio
async def test_micro_session_put_update(micro_session: MicroDBSession):
    node = NodeRecord(id="node-u", entity_type="card", label="Original")
    await micro_session.put(node)
    updated = NodeRecord(id="node-u", entity_type="card", label="Updated")
    await micro_session.put(updated)
    fetched = await micro_session.get("card", "node-u")
    assert fetched is not None
    assert fetched.label == "Updated"


@pytest.mark.asyncio
async def test_micro_session_search(micro_session: MicroDBSession):
    await micro_session.put(NodeRecord(id="s1", entity_type="card", label="Rate Limiting Pattern"))
    await micro_session.put(NodeRecord(id="s2", entity_type="card", label="Circuit Breaker Pattern"))
    results = await micro_session.search("Rate Limiting")
    assert len(results) >= 1
    assert results[0].id == "s1"


@pytest.mark.asyncio
async def test_micro_session_delete(micro_session: MicroDBSession):
    await micro_session.put(NodeRecord(id="del-1", entity_type="card", label="Temp"))
    ok = await micro_session.delete("card", "del-1")
    assert ok
    gone = await micro_session.get("card", "del-1")
    assert gone is None


@pytest.mark.asyncio
async def test_micro_session_link_and_get(micro_session: MicroDBSession):
    await micro_session.put(NodeRecord(id="from-1", entity_type="card", label="From"))
    await micro_session.put(NodeRecord(id="to-1", entity_type="card", label="To"))
    edge = EdgeRecord(edge_type="card_to_card", from_id="from-1", to_id="to-1")
    await micro_session.link(edge)
    links = await micro_session.get_links("from-1", direction="outbound")
    assert len(links) >= 1
    assert links[0].from_id == "from-1"


@pytest.mark.asyncio
async def test_micro_session_put_triple(micro_session: MicroDBSession):
    triple = TripleRecord(subject="DDB", predicate="stores", object="memories", zone="core")
    stored_id = await micro_session.put_triple(triple)
    assert stored_id  # rowid as string

    results = await micro_session.search_triples("DDB")
    assert len(results) == 1
    assert results[0].predicate == "stores"


# -- SessionFactory ----------------------------------------------------------

@pytest.mark.asyncio
async def test_factory_create_micro(tmp_path: Path):
    session = await SessionFactory.create("micro", db_path=str(tmp_path / "factory.ddb"))
    assert isinstance(session, MicroDBSession)
    await session.close()


@pytest.mark.asyncio
async def test_factory_unknown_backend():
    with pytest.raises(ValueError, match="Unknown backend"):
        await SessionFactory.create("redis")