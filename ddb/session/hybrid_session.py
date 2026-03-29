"""HybridSession -- write-both, read-local-first DBSession implementation."""

from __future__ import annotations

from session.micro_session import MicroDBSession
from session.arango_session import ArangoDBSession
from session.models import NodeRecord, EdgeRecord, TripleRecord


class HybridSession:
    """Composes MicroDBSession (SQLite) and ArangoDBSession (ArangoDB).

    Write strategy: write to both backends on every mutation.
    Read strategy: try SQLite first; if miss, fall back to ArangoDB.
    Search strategy: run FTS5 locally + AQL remotely, merge and deduplicate.
    """

    def __init__(
        self,
        micro: MicroDBSession,
        arango: ArangoDBSession,
    ) -> None:
        self._micro = micro
        self._arango = arango

    # -- node CRUD -----------------------------------------------------------

    async def get(self, entity_type: str, entity_id: str) -> NodeRecord | None:
        result = await self._micro.get(entity_type, entity_id)
        if result is not None:
            return result
        return await self._arango.get(entity_type, entity_id)

    async def put(self, record: NodeRecord) -> str:
        await self._micro.put(record)
        await self._arango.put(record)
        return record.id

    async def search(
        self,
        query: str,
        entity_type: str | None = None,
        limit: int = 50,
    ) -> list[NodeRecord]:
        local = await self._micro.search(query, entity_type=entity_type, limit=limit)
        remote = await self._arango.search(query, entity_type=entity_type, limit=limit)

        # Merge, deduplicate by id, local results take precedence
        seen: set[str] = set()
        merged: list[NodeRecord] = []
        for node in local + remote:
            if node.id not in seen:
                seen.add(node.id)
                merged.append(node)
        return merged[:limit]

    async def delete(self, entity_type: str, entity_id: str) -> bool:
        local_ok = await self._micro.delete(entity_type, entity_id)
        remote_ok = await self._arango.delete(entity_type, entity_id)
        return local_ok or remote_ok

    # -- edge operations -----------------------------------------------------

    async def link(self, edge: EdgeRecord) -> str:
        local_id = await self._micro.link(edge)
        await self._arango.link(edge)
        return local_id

    async def get_links(
        self,
        entity_id: str,
        direction: str = "outbound",
        edge_type: str | None = None,
    ) -> list[EdgeRecord]:
        local = await self._micro.get_links(entity_id, direction=direction, edge_type=edge_type)
        if local:
            return local
        return await self._arango.get_links(entity_id, direction=direction, edge_type=edge_type)

    # -- triple operations ---------------------------------------------------

    async def put_triple(self, triple: TripleRecord) -> str:
        local_id = await self._micro.put_triple(triple)
        await self._arango.put_triple(triple)
        return local_id

    async def search_triples(self, entity: str, limit: int = 50) -> list[TripleRecord]:
        local = await self._micro.search_triples(entity, limit=limit)
        if local:
            return local
        return await self._arango.search_triples(entity, limit=limit)

    # -- lifecycle -----------------------------------------------------------

    async def close(self) -> None:
        await self._micro.close()
        await self._arango.close()