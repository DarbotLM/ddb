"""DDB graph setup -- collections, edges, named graph, indexes."""

from __future__ import annotations

from typing import Any

from arango.database import StandardDatabase

DDB_GRAPH_NAME = "ddb_knowledge_graph"

DOCUMENT_COLLECTIONS = [
    "agents",
    "cards",
    "memory_zones",
    "sessions",
    "patterns",
    "triples",
    "events",
    "manifests",
    "manifest_nodes",
    "manifest_edges",
    "session_views",
]

EDGE_DEFINITIONS = [
    {"edge_collection": "agent_to_agent", "from_vertex_collections": ["agents"], "to_vertex_collections": ["agents"]},
    {"edge_collection": "agent_to_card", "from_vertex_collections": ["agents"], "to_vertex_collections": ["cards"]},
    {"edge_collection": "card_to_card", "from_vertex_collections": ["cards"], "to_vertex_collections": ["cards"]},
    {"edge_collection": "card_to_zone", "from_vertex_collections": ["cards"], "to_vertex_collections": ["memory_zones"]},
    {"edge_collection": "pattern_to_card", "from_vertex_collections": ["patterns"], "to_vertex_collections": ["cards"]},
    {"edge_collection": "session_to_agent", "from_vertex_collections": ["sessions"], "to_vertex_collections": ["agents"]},
    {"edge_collection": "triple_to_card", "from_vertex_collections": ["triples"], "to_vertex_collections": ["cards"]},
    {"edge_collection": "agent_to_zone", "from_vertex_collections": ["agents"], "to_vertex_collections": ["memory_zones"]},
    {"edge_collection": "session_to_card", "from_vertex_collections": ["sessions"], "to_vertex_collections": ["cards"]},
    {"edge_collection": "event_to_card", "from_vertex_collections": ["events"], "to_vertex_collections": ["cards"]},
    {"edge_collection": "manifest_to_node", "from_vertex_collections": ["manifests"], "to_vertex_collections": ["manifest_nodes"]},
    {"edge_collection": "manifest_to_edge", "from_vertex_collections": ["manifests"], "to_vertex_collections": ["manifest_edges"]},
    {"edge_collection": "session_to_manifest", "from_vertex_collections": ["sessions"], "to_vertex_collections": ["manifests"]},
    {"edge_collection": "view_to_manifest", "from_vertex_collections": ["session_views"], "to_vertex_collections": ["manifests"]},
]

COLLECTION_INDEXES: dict[str, list[dict[str, Any]]] = {
    "agents": [{"fields": ["name"], "unique": True}, {"fields": ["status"]}],
    "cards": [{"fields": ["card_type"]}, {"fields": ["zone"]}, {"fields": ["agent_id"]}, {"fields": ["created_at"]}],
    "memory_zones": [{"fields": ["name"], "unique": True}],
    "sessions": [{"fields": ["agent_id"]}, {"fields": ["status"]}],
    "patterns": [{"fields": ["discovered_at"]}, {"fields": ["confidence"]}],
    "triples": [{"fields": ["subject"]}, {"fields": ["predicate"]}, {"fields": ["object"]}, {"fields": ["zone"]}, {"fields": ["confidence"]}, {"fields": ["source_agent"]}],
    "events": [{"fields": ["event_type"]}, {"fields": ["source_agent"]}, {"fields": ["triad_status"]}, {"fields": ["timestamp_utc"]}],
    "manifests": [{"fields": ["title"]}, {"fields": ["session_id"]}, {"fields": ["source_db_id"]}, {"fields": ["updated_at"]}],
    "manifest_nodes": [{"fields": ["manifest_id"]}, {"fields": ["entity_id"]}, {"fields": ["entity_kind"]}, {"fields": ["layer"]}],
    "manifest_edges": [{"fields": ["manifest_id"]}, {"fields": ["source_node_id"]}, {"fields": ["target_node_id"]}, {"fields": ["relation"]}],
    "session_views": [{"fields": ["manifest_id"]}, {"fields": ["layout"]}, {"fields": ["updated_at"]}],
}


class DDBGraph:
    def __init__(self, db: StandardDatabase) -> None:
        self.db = db

    def ensure_collections(self) -> list[str]:
        created: list[str] = []
        existing = {c["name"] for c in self.db.collections() if not c["name"].startswith("_")}
        for name in DOCUMENT_COLLECTIONS:
            if name not in existing:
                self.db.create_collection(name)
                created.append(name)
        for edge_def in EDGE_DEFINITIONS:
            name = edge_def["edge_collection"]
            if name not in existing:
                self.db.create_collection(name, edge=True)
                created.append(name)
        return created

    def ensure_indexes(self) -> int:
        count = 0
        for col_name, indexes in COLLECTION_INDEXES.items():
            if not self.db.has_collection(col_name):
                continue
            col = self.db.collection(col_name)
            for idx in indexes:
                col.add_persistent_index(fields=idx["fields"], unique=idx.get("unique", False))
                count += 1
        return count

    def ensure_graph(self) -> bool:
        if self.db.has_graph(DDB_GRAPH_NAME):
            return False
        self.db.create_graph(DDB_GRAPH_NAME, edge_definitions=EDGE_DEFINITIONS)
        return True

    def setup_all(self) -> dict[str, Any]:
        return {
            "collections_created": self.ensure_collections(),
            "indexes_created": self.ensure_indexes(),
            "graph_created": self.ensure_graph(),
        }

    def drop_all(self) -> None:
        if self.db.has_graph(DDB_GRAPH_NAME):
            self.db.delete_graph(DDB_GRAPH_NAME, drop_collections=True)