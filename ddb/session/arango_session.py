"""ArangoDBSession -- DBSession implementation backed by ArangoDB."""

from __future__ import annotations

from typing import Any

from arango.database import StandardDatabase

from session.models import NodeRecord, EdgeRecord, TripleRecord

# Map entity_type -> ArangoDB collection name
_ENTITY_COLLECTION: dict[str, str] = {
    "agent": "agents",
    "card": "cards",
    "pattern": "patterns",
    "triple": "triples",
    "zone": "memory_zones",
    "session": "sessions",
}

# Map edge_type -> ArangoDB edge collection name
_EDGE_COLLECTION: dict[str, str] = {
    "agent_to_card": "agent_to_card",
    "card_to_card": "card_to_card",
    "card_to_zone": "card_to_zone",
    "pattern_to_card": "pattern_to_card",
    "session_to_agent": "session_to_agent",
    "triple_to_card": "triple_to_card",
    "agent_to_zone": "agent_to_zone",
    "session_to_card": "session_to_card",
    "event_to_card": "event_to_card",
    "agent_to_agent": "agent_to_agent",
}


class ArangoDBSession:
    """Implements DBSession over an ArangoDB database connection."""

    def __init__(self, db: StandardDatabase) -> None:
        self._db = db

    # -- node CRUD -----------------------------------------------------------

    async def get(self, entity_type: str, entity_id: str) -> NodeRecord | None:
        col_name = _ENTITY_COLLECTION.get(entity_type, entity_type)
        if not self._db.has_collection(col_name):
            return None
        col = self._db.collection(col_name)
        if not col.has(entity_id):
            return None
        doc = col.get(entity_id)
        return self._doc_to_node(doc, entity_type)

    async def put(self, record: NodeRecord) -> str:
        col_name = _ENTITY_COLLECTION.get(record.entity_type, record.entity_type)
        if not self._db.has_collection(col_name):
            self._db.create_collection(col_name)
        col = self._db.collection(col_name)
        doc = self._node_to_doc(record)
        if col.has(record.id):
            col.update(doc)
        else:
            col.insert(doc)
        return record.id

    async def search(
        self,
        query: str,
        entity_type: str | None = None,
        limit: int = 50,
    ) -> list[NodeRecord]:
        collections = (
            [_ENTITY_COLLECTION.get(entity_type, entity_type)]
            if entity_type
            else list(_ENTITY_COLLECTION.values())
        )
        results: list[NodeRecord] = []
        for col_name in collections:
            if not self._db.has_collection(col_name):
                continue
            aql = """
                FOR doc IN @@col
                    FILTER CONTAINS(LOWER(doc.name ?? doc.title ?? doc.subject ?? ""), LOWER(@q))
                    LIMIT @limit
                    RETURN doc
            """
            cursor = self._db.aql.execute(
                aql,
                bind_vars={"@col": col_name, "q": query, "limit": limit},
            )
            for doc in cursor:
                et = {v: k for k, v in _ENTITY_COLLECTION.items()}.get(col_name, col_name)
                results.append(self._doc_to_node(doc, et))
        return results[:limit]

    async def delete(self, entity_type: str, entity_id: str) -> bool:
        col_name = _ENTITY_COLLECTION.get(entity_type, entity_type)
        if not self._db.has_collection(col_name):
            return False
        col = self._db.collection(col_name)
        if not col.has(entity_id):
            return False
        col.delete(entity_id)
        return True

    # -- edge operations -----------------------------------------------------

    async def link(self, edge: EdgeRecord) -> str:
        col_name = _EDGE_COLLECTION.get(edge.edge_type, edge.edge_type)
        if not self._db.has_collection(col_name):
            self._db.create_collection(col_name, edge=True)
        col = self._db.collection(col_name)

        # Determine from/to collections from edge_type parts
        parts = edge.edge_type.split("_to_")
        from_col = _ENTITY_COLLECTION.get(parts[0], parts[0] + "s") if len(parts) == 2 else "cards"
        to_col = _ENTITY_COLLECTION.get(parts[-1], parts[-1] + "s") if len(parts) == 2 else "cards"

        doc = {
            "_from": f"{from_col}/{edge.from_id}",
            "_to": f"{to_col}/{edge.to_id}",
            "edge_type": edge.edge_type,
            "weight": edge.weight,
            **edge.metadata,
        }
        result = col.insert(doc)
        return result["_key"]

    async def get_links(
        self,
        entity_id: str,
        direction: str = "outbound",
        edge_type: str | None = None,
    ) -> list[EdgeRecord]:
        edge_cols = (
            [_EDGE_COLLECTION.get(edge_type, edge_type)]
            if edge_type
            else list(_EDGE_COLLECTION.values())
        )
        results: list[EdgeRecord] = []
        dir_aql = direction.upper() if direction in ("outbound", "inbound") else "ANY"

        for col_name in edge_cols:
            if not self._db.has_collection(col_name):
                continue
            # Try each known entity collection as the start vertex
            for start_col in _ENTITY_COLLECTION.values():
                try:
                    cursor = self._db.aql.execute(
                        f"""
                        FOR v, e IN 1..1 {dir_aql} @start @@edge_col
                            RETURN e
                        """,
                        bind_vars={
                            "start": f"{start_col}/{entity_id}",
                            "@edge_col": col_name,
                        },
                    )
                    for doc in cursor:
                        results.append(self._doc_to_edge(doc))
                    if results:
                        break
                except Exception:
                    continue
        return results

    # -- triple operations ---------------------------------------------------

    async def put_triple(self, triple: TripleRecord) -> str:
        if not self._db.has_collection("triples"):
            self._db.create_collection("triples")
        col = self._db.collection("triples")
        doc = {
            "_key": triple.id.replace("-", "_"),
            "subject": triple.subject,
            "predicate": triple.predicate,
            "object": triple.object,
            "confidence": triple.confidence,
            "source_id": triple.source_id,
            "model": triple.model,
            "zone": triple.zone,
            "created_at": triple.created_at,
        }
        if col.has(doc["_key"]):
            col.update(doc)
        else:
            col.insert(doc)
        return triple.id

    async def search_triples(self, entity: str, limit: int = 50) -> list[TripleRecord]:
        if not self._db.has_collection("triples"):
            return []
        cursor = self._db.aql.execute(
            """
            FOR t IN triples
                FILTER t.subject == @e OR t.object == @e
                SORT t.confidence DESC
                LIMIT @limit
                RETURN t
            """,
            bind_vars={"e": entity, "limit": limit},
        )
        return [self._doc_to_triple(doc) for doc in cursor]

    # -- lifecycle -----------------------------------------------------------

    async def close(self) -> None:
        pass  # python-arango connections are managed externally

    # -- helpers -------------------------------------------------------------

    @staticmethod
    def _node_to_doc(record: NodeRecord) -> dict[str, Any]:
        return {
            "_key": record.id,
            "name": record.label,
            "title": record.label,
            "entity_type": record.entity_type,
            "zone": record.zone,
            "agent_id": record.agent_id,
            "tags": record.tags,
            "properties": record.properties,
            "created_at": record.created_at,
            "updated_at": record.updated_at,
        }

    @staticmethod
    def _doc_to_node(doc: dict[str, Any], entity_type: str) -> NodeRecord:
        return NodeRecord(
            id=doc.get("_key", doc.get("_id", "")),
            entity_type=entity_type,
            label=doc.get("name") or doc.get("title") or doc.get("subject") or "",
            properties=doc.get("properties", {}),
            zone=doc.get("zone"),
            agent_id=doc.get("agent_id"),
            tags=doc.get("tags", []),
            created_at=doc.get("created_at", ""),
            updated_at=doc.get("updated_at", ""),
        )

    @staticmethod
    def _doc_to_edge(doc: dict[str, Any]) -> EdgeRecord:
        from_full = doc.get("_from", "/")
        to_full = doc.get("_to", "/")
        return EdgeRecord(
            id=doc.get("_key", ""),
            edge_type=doc.get("edge_type", ""),
            from_id=from_full.split("/")[-1],
            to_id=to_full.split("/")[-1],
            weight=doc.get("weight", 1.0),
            metadata={k: v for k, v in doc.items() if not k.startswith("_") and k not in ("edge_type", "weight")},
        )

    @staticmethod
    def _doc_to_triple(doc: dict[str, Any]) -> TripleRecord:
        return TripleRecord(
            id=doc.get("_key", "").replace("_", "-"),
            subject=doc.get("subject", ""),
            predicate=doc.get("predicate", ""),
            object=doc.get("object", ""),
            confidence=doc.get("confidence", 1.0),
            source_id=doc.get("source_id"),
            model=doc.get("model"),
            zone=doc.get("zone"),
            created_at=doc.get("created_at", ""),
        )