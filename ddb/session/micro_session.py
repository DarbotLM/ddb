"""MicroDBSession -- DBSession implementation backed by a SQLite .ddb micro database."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from micro.engine import MicroDB
from session.models import NodeRecord, EdgeRecord, TripleRecord


class MicroDBSession:
    """Implements DBSession over a single SQLite .ddb micro database.

    Node types are mapped to the ``cards`` table (agents and zones are stored
    as special card_types). Edges map to the ``links`` table. Triples map to
    the ``triples`` table.
    """

    def __init__(self, db: MicroDB) -> None:
        self._db = db

    # -- node CRUD -----------------------------------------------------------

    async def get(self, entity_type: str, entity_id: str) -> NodeRecord | None:
        rows = await self._db.execute(
            "SELECT * FROM cards WHERE id = ?", (entity_id,)
        )
        if not rows:
            return None
        return self._row_to_node(rows[0])

    async def put(self, record: NodeRecord) -> str:
        existing = await self._db.get_card(record.id)
        props = json.dumps(record.properties)
        tags = json.dumps(record.tags)

        if existing is None:
            await self._db.conn.execute(
                """INSERT INTO cards(id, card_type, title, schema_json, content_md, tags, zone, created_at, updated_at)
                   VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)""",
                (
                    record.id, record.entity_type, record.label,
                    props, None, tags, record.zone,
                    record.created_at, record.updated_at,
                ),
            )
        else:
            await self._db.conn.execute(
                """UPDATE cards SET card_type=?, title=?, schema_json=?, tags=?, zone=?, updated_at=?
                   WHERE id=?""",
                (record.entity_type, record.label, props, tags, record.zone,
                 record.updated_at, record.id),
            )
        await self._db.conn.commit()
        return record.id

    async def search(
        self,
        query: str,
        entity_type: str | None = None,
        limit: int = 50,
    ) -> list[NodeRecord]:
        rows = await self._db.search_cards(query, limit=limit)
        nodes = [self._row_to_node(r) for r in rows]
        if entity_type:
            nodes = [n for n in nodes if n.entity_type == entity_type]
        return nodes

    async def delete(self, entity_type: str, entity_id: str) -> bool:
        existing = await self._db.get_card(entity_id)
        if existing is None:
            return False
        await self._db.conn.execute("DELETE FROM cards WHERE id = ?", (entity_id,))
        await self._db.conn.commit()
        return True

    # -- edge operations -----------------------------------------------------

    async def link(self, edge: EdgeRecord) -> str:
        row_id = await self._db.insert_link({
            "link_type": edge.edge_type,
            "source_id": edge.from_id,
            "target_uri": f"ddb://cards/{edge.to_id}",
            "metadata": edge.metadata,
            "created_at": edge.created_at,
        })
        return str(row_id)

    async def get_links(
        self,
        entity_id: str,
        direction: str = "outbound",
        edge_type: str | None = None,
    ) -> list[EdgeRecord]:
        if direction in ("outbound", "any"):
            rows = await self._db.get_links(source_id=entity_id, link_type=edge_type)
        else:
            rows = []

        if direction in ("inbound", "any"):
            uri_pattern = f"ddb://cards/{entity_id}"
            inbound = await self._db.execute(
                "SELECT * FROM links WHERE target_uri = ?" +
                (" AND link_type = ?" if edge_type else ""),
                (uri_pattern, edge_type) if edge_type else (uri_pattern,),
            )
            rows = rows + inbound

        return [self._row_to_edge(r) for r in rows]

    # -- triple operations ---------------------------------------------------

    async def put_triple(self, triple: TripleRecord) -> str:
        row_id = await self._db.insert_triple({
            "subject": triple.subject,
            "predicate": triple.predicate,
            "object": triple.object,
            "confidence": triple.confidence,
            "source_card_id": triple.source_id,
            "model": triple.model,
            "zone": triple.zone,
            "graph_key": triple.graph_key,
            "created_at": triple.created_at,
        })
        return str(row_id)

    async def search_triples(self, entity: str, limit: int = 50) -> list[TripleRecord]:
        rows = await self._db.search_triples(entity, limit=limit)
        return [self._row_to_triple(r) for r in rows]

    # -- lifecycle -----------------------------------------------------------

    async def close(self) -> None:
        await self._db.close()

    # -- helpers -------------------------------------------------------------

    @staticmethod
    def _row_to_node(row: dict[str, Any]) -> NodeRecord:
        try:
            props = json.loads(row.get("schema_json") or "{}")
        except (json.JSONDecodeError, TypeError):
            props = {}
        try:
            tags = json.loads(row.get("tags") or "[]")
        except (json.JSONDecodeError, TypeError):
            tags = []
        return NodeRecord(
            id=row["id"],
            entity_type=row.get("card_type", "card"),
            label=row.get("title", ""),
            properties=props,
            zone=row.get("zone"),
            tags=tags,
            created_at=row.get("created_at", ""),
            updated_at=row.get("updated_at", ""),
        )

    @staticmethod
    def _row_to_edge(row: dict[str, Any]) -> EdgeRecord:
        target = row.get("target_uri", "")
        to_id = target.split("/")[-1] if "/" in target else target
        try:
            meta = json.loads(row.get("metadata") or "{}")
        except (json.JSONDecodeError, TypeError):
            meta = {}
        return EdgeRecord(
            id=str(row.get("id", "")),
            edge_type=row.get("link_type", ""),
            from_id=row.get("source_id", ""),
            to_id=to_id,
            metadata=meta,
            created_at=row.get("created_at", ""),
        )

    @staticmethod
    def _row_to_triple(row: dict[str, Any]) -> TripleRecord:
        return TripleRecord(
            id=str(row.get("id", "")),
            subject=row.get("subject", ""),
            predicate=row.get("predicate", ""),
            object=row.get("object", ""),
            confidence=row.get("confidence", 1.0),
            source_id=row.get("source_card_id"),
            model=row.get("model"),
            zone=row.get("zone"),
            graph_key=row.get("graph_key"),
            created_at=row.get("created_at", ""),
        )