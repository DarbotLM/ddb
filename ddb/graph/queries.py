"""Pre-built AQL queries for the DDB knowledge graph."""

from __future__ import annotations

from typing import Any

from arango.database import StandardDatabase


class DDBQueries:
    def __init__(self, db: StandardDatabase) -> None:
        self.db = db

    def _run(self, aql: str, bind_vars: dict[str, Any] | None = None) -> list[dict[str, Any]]:
        cursor = self.db.aql.execute(aql, bind_vars=bind_vars or {})
        return list(cursor)

    def remember_forward(self, since: str, limit: int = 50) -> list[dict[str, Any]]:
        return self._run(
            """
            FOR p IN patterns
                FILTER p.discovered_at > @since
                FOR c IN 1..3 OUTBOUND p pattern_to_card
                    FOR a IN 1..1 INBOUND c agent_to_card
                        LIMIT @limit
                        RETURN { pattern: p, evidence: c, discoveredBy: a }
            """,
            {"since": since, "limit": limit},
        )

    def card_tree(self, root_card_key: str, max_depth: int = 10) -> list[dict[str, Any]]:
        return self._run(
            """
            FOR v, e, p IN 1..@depth OUTBOUND @root card_to_card
                RETURN { depth: LENGTH(p.edges), card: v }
            """,
            {"root": f"cards/{root_card_key}", "depth": max_depth},
        )

    def zone_contents(self, zone_name: str) -> list[dict[str, Any]]:
        return self._run(
            """
            FOR z IN memory_zones FILTER z.name == @zone
                FOR c IN 1..1 INBOUND z card_to_zone
                    LET agents = (FOR a IN 1..1 INBOUND c agent_to_card RETURN a.name)
                    RETURN { card: c, agents: agents }
            """,
            {"zone": zone_name},
        )

    def agent_connections(self, agent_key: str, depth: int = 2) -> list[dict[str, Any]]:
        return self._run(
            """
            FOR v IN 1..@depth ANY @agent agent_to_agent
                RETURN DISTINCT v
            """,
            {"agent": f"agents/{agent_key}", "depth": depth},
        )

    def shared_cards(self, agent_key_a: str, agent_key_b: str) -> list[dict[str, Any]]:
        return self._run(
            """
            LET cardsA = (FOR c IN 1..1 OUTBOUND @a agent_to_card RETURN c._id)
            LET cardsB = (FOR c IN 1..1 OUTBOUND @b agent_to_card RETURN c._id)
            FOR id IN INTERSECTION(cardsA, cardsB)
                RETURN DOCUMENT(id)
            """,
            {"a": f"agents/{agent_key_a}", "b": f"agents/{agent_key_b}"},
        )

    def patterns_by_confidence(self, min_confidence: float = 0.5, limit: int = 20) -> list[dict[str, Any]]:
        return self._run(
            """
            FOR p IN patterns
                FILTER p.confidence >= @min
                SORT p.confidence DESC
                LIMIT @limit
                RETURN p
            """,
            {"min": min_confidence, "limit": limit},
        )

    def triples_for_entity(self, entity: str, limit: int = 50) -> list[dict[str, Any]]:
        return self._run(
            """
            FOR t IN triples
                FILTER t.subject == @entity OR t.object == @entity
                SORT t.confidence DESC
                LIMIT @limit
                RETURN t
            """,
            {"entity": entity, "limit": limit},
        )

    def triple_chain(self, start_entity: str, depth: int = 2, limit: int = 100) -> list[dict[str, Any]]:
        return self._run(
            """
            LET start = @entity
            LET chain = (FOR t IN triples FILTER t.subject == start RETURN t)
            FOR root IN chain
                FOR t2 IN triples
                    FILTER t2.subject == root.object
                    LIMIT @limit
                    RETURN {
                        hop: 1,
                        root_subject: root.subject,
                        root_predicate: root.predicate,
                        root_object: root.object,
                        subject: t2.subject,
                        predicate: t2.predicate,
                        object: t2.object,
                        confidence: t2.confidence
                    }
            """,
            {"entity": start_entity, "limit": limit},
        )

    def triples_by_predicate(self, predicate: str, limit: int = 50) -> list[dict[str, Any]]:
        return self._run(
            """
            FOR t IN triples
                FILTER t.predicate == @predicate
                SORT t.confidence DESC
                LIMIT @limit
                RETURN t
            """,
            {"predicate": predicate, "limit": limit},
        )

    def triple_evidence(self, triple_key: str) -> list[dict[str, Any]]:
        return self._run(
            """
            FOR c IN 1..1 OUTBOUND @triple triple_to_card
                RETURN c
            """,
            {"triple": f"triples/{triple_key}"},
        )

    def events_by_agent(self, agent_key: str, limit: int = 50) -> list[dict[str, Any]]:
        return self._run(
            """
            FOR e IN events
                FILTER e.source_agent == @agent
                SORT e.timestamp_utc DESC
                LIMIT @limit
                RETURN e
            """,
            {"agent": agent_key, "limit": limit},
        )

    def events_by_type(self, event_type: str, limit: int = 50) -> list[dict[str, Any]]:
        return self._run(
            """
            FOR e IN events
                FILTER e.event_type == @type
                SORT e.timestamp_utc DESC
                LIMIT @limit
                RETURN e
            """,
            {"type": event_type, "limit": limit},
        )

    def event_cards(self, event_key: str) -> list[dict[str, Any]]:
        return self._run(
            """
            FOR c IN 1..1 OUTBOUND @event event_to_card
                RETURN c
            """,
            {"event": f"events/{event_key}"},
        )

    def list_manifests(self, source_db_id: str | None = None, limit: int = 50) -> list[dict[str, Any]]:
        if source_db_id:
            return self._run(
                """
                FOR m IN manifests
                    FILTER m.source_db_id == @source_db_id
                    SORT m.updated_at DESC
                    LIMIT @limit
                    RETURN m
                """,
                {"source_db_id": source_db_id, "limit": limit},
            )
        return self._run(
            """
            FOR m IN manifests
                SORT m.updated_at DESC
                LIMIT @limit
                RETURN m
            """,
            {"limit": limit},
        )

    def manifest_scene(self, manifest_key: str) -> dict[str, Any] | None:
        results = self._run(
            """
            FOR m IN manifests
                FILTER m._key == @key OR m.id == @key
                LET nodes = (FOR n IN 1..1 OUTBOUND m manifest_to_node RETURN n)
                LET edges = (FOR e IN 1..1 OUTBOUND m manifest_to_edge RETURN e)
                LET views = (FOR v IN 1..1 INBOUND m view_to_manifest RETURN v)
                RETURN { manifest: m, nodes: nodes, edges: edges, views: views }
            """,
            {"key": manifest_key},
        )
        return results[0] if results else None