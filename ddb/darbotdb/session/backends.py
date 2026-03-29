"""Backend adapters for DarbotDB 3DKG sessions."""

from __future__ import annotations

from abc import ABC, abstractmethod
from pathlib import Path
from typing import Any

from arango import ArangoClient

from darbotdb.config import settings
from darbotdb.session.models import SessionContext, SessionSummary
from graph.manifest import Manifest
from graph.projection import ManifestProjector
from graph.queries import DDBQueries
from micro.engine import MicroDB
from micro.manager import MicroDBManager


class KnowledgeBackend(ABC):
    @abstractmethod
    async def list_sessions(self) -> list[SessionSummary]:
        raise NotImplementedError


class ManifestBackend(ABC):
    @abstractmethod
    async def save_manifest(self, manifest: Manifest, db_id: str | None = None) -> Manifest:
        raise NotImplementedError

    @abstractmethod
    async def get_manifest(self, manifest_id: str, db_id: str | None = None) -> dict[str, Any] | None:
        raise NotImplementedError


class MicroSessionBackend(KnowledgeBackend, ManifestBackend):
    def __init__(self, manager: MicroDBManager | None = None) -> None:
        self.manager = manager or MicroDBManager(Path(settings.MICRO_DATA_ROOT))

    def resolve_db(self, db_id: str) -> MicroDB:
        dbs = self.manager.list_dbs()
        match = [p for p in dbs if db_id in p.stem]
        if not match:
            raise FileNotFoundError(f"Micro DB '{db_id}' not found")
        return MicroDB(match[0])

    async def ensure_context(self, context: SessionContext) -> MicroDB:
        if context.db_id:
            db = self.resolve_db(context.db_id)
            await db.open()
            return db
        if context.scope.value == "zone" and context.zone:
            db = await self.manager.create_zone_db(context.zone)
            return db
        if context.scope.value == "agent" and context.agent_id:
            db = await self.manager.create_agent_db(context.agent_id)
            return db
        if context.session_id and context.agent_id:
            db = await self.manager.create_session_db(context.session_id, context.agent_id)
            return db
        raise FileNotFoundError("Unable to resolve or create session context")

    async def list_sessions(self) -> list[SessionSummary]:
        result: list[SessionSummary] = []
        for path in self.manager.list_dbs():
            db = MicroDB(path)
            await db.open()
            try:
                result.append(SessionSummary(
                    db_id=path.stem,
                    path=str(path),
                    db_type=await db.get_meta("db_type"),
                    agent_id=await db.get_meta("agent_id"),
                    zone=await db.get_meta("zone"),
                    created_at=await db.get_meta("created_at"),
                ))
            finally:
                await db.close()
        return result

    async def get_status(self, db_id: str) -> dict[str, Any]:
        db = self.resolve_db(db_id)
        await db.open()
        try:
            cards = await db.execute("SELECT COUNT(*) as cnt FROM cards")
            turns = await db.execute("SELECT COUNT(*) as cnt FROM turns")
            triples = await db.execute("SELECT COUNT(*) as cnt FROM triples")
            manifests = await db.execute("SELECT COUNT(*) as cnt FROM manifests")
            return {
                "db_id": db_id,
                "path": str(db.path),
                "db_type": await db.get_meta("db_type"),
                "agent_id": await db.get_meta("agent_id"),
                "zone": await db.get_meta("zone"),
                "cards": cards[0]["cnt"] if cards else 0,
                "turns": turns[0]["cnt"] if turns else 0,
                "triples": triples[0]["cnt"] if triples else 0,
                "manifests": manifests[0]["cnt"] if manifests else 0,
            }
        finally:
            await db.close()

    async def search_cards(self, db_id: str, query: str, limit: int = 50) -> list[dict[str, Any]]:
        db = self.resolve_db(db_id)
        await db.open()
        try:
            return await db.search_cards(query, limit=limit)
        finally:
            await db.close()

    async def search_triples(self, db_id: str, entity: str, limit: int = 50) -> list[dict[str, Any]]:
        db = self.resolve_db(db_id)
        await db.open()
        try:
            return await db.search_triples(entity, limit=limit)
        finally:
            await db.close()

    async def list_events(self, db_id: str, limit: int = 50) -> list[dict[str, Any]]:
        db = self.resolve_db(db_id)
        await db.open()
        try:
            return await db.get_events(limit=limit)
        finally:
            await db.close()

    async def fetch_projection_inputs(self, db_id: str) -> dict[str, Any]:
        db = self.resolve_db(db_id)
        await db.open()
        try:
            return {
                "cards": await db.execute("SELECT * FROM cards ORDER BY updated_at DESC"),
                "triples": await db.execute("SELECT * FROM triples ORDER BY created_at DESC"),
                "events": await db.execute("SELECT * FROM events ORDER BY timestamp_utc DESC"),
                "meta": {
                    "db_type": await db.get_meta("db_type"),
                    "agent_id": await db.get_meta("agent_id"),
                    "zone": await db.get_meta("zone"),
                    "created_at": await db.get_meta("created_at"),
                },
            }
        finally:
            await db.close()

    async def save_manifest(self, manifest: Manifest, db_id: str | None = None) -> Manifest:
        target_db_id = db_id or manifest.source_db_id
        if not target_db_id:
            raise ValueError("db_id is required to save a manifest to a micro DB")
        db = self.resolve_db(target_db_id)
        await db.open()
        try:
            await db.upsert_manifest(manifest.model_dump(mode="json"))
            return manifest
        finally:
            await db.close()

    async def get_manifest(self, manifest_id: str, db_id: str | None = None) -> dict[str, Any] | None:
        if not db_id:
            return None
        db = self.resolve_db(db_id)
        await db.open()
        try:
            return await db.get_manifest(manifest_id)
        finally:
            await db.close()

    async def list_manifests(self, db_id: str, limit: int = 50) -> list[dict[str, Any]]:
        db = self.resolve_db(db_id)
        await db.open()
        try:
            return await db.list_manifests(source_db_id=db_id, limit=limit)
        finally:
            await db.close()


class DDBGraphBackend(ManifestBackend):
    def _db(self):
        client = ArangoClient(hosts=settings.DDB_HOSTS)
        return client.db(settings.DDB_DATABASE, username=settings.DDB_USERNAME, password=settings.DDB_PASSWORD)

    def queries(self) -> DDBQueries:
        return DDBQueries(self._db())

    async def save_manifest(self, manifest: Manifest, db_id: str | None = None) -> Manifest:
        db = self._db()
        manifests = db.collection("manifests")
        nodes = db.collection("manifest_nodes")
        edges = db.collection("manifest_edges")
        views = db.collection("session_views")
        manifest_to_node = db.collection("manifest_to_node")
        manifest_to_edge = db.collection("manifest_to_edge")
        view_to_manifest = db.collection("view_to_manifest")

        doc = {
            "_key": manifest.id,
            "id": manifest.id,
            "title": manifest.title,
            "description": manifest.description,
            "session_id": manifest.session_id,
            "source_db_id": db_id or manifest.source_db_id,
            "source_kind": manifest.source_kind,
            "schema_version": manifest.schema_version,
            "layout": manifest.layout,
            "metadata": manifest.metadata,
            "created_at": manifest.created_at.isoformat(),
            "updated_at": manifest.updated_at.isoformat(),
        }
        if manifests.has(manifest.id):
            manifests.update(doc)
        else:
            manifests.insert(doc)

        db.aql.execute("FOR n IN manifest_nodes FILTER n.manifest_id == @id REMOVE n IN manifest_nodes", bind_vars={"id": manifest.id})
        db.aql.execute("FOR e IN manifest_edges FILTER e.manifest_id == @id REMOVE e IN manifest_edges", bind_vars={"id": manifest.id})
        db.aql.execute("FOR v IN session_views FILTER v.manifest_id == @id REMOVE v IN session_views", bind_vars={"id": manifest.id})
        db.aql.execute("FOR e IN manifest_to_node FILTER e._from == @from REMOVE e IN manifest_to_node", bind_vars={"from": f"manifests/{manifest.id}"})
        db.aql.execute("FOR e IN manifest_to_edge FILTER e._from == @from REMOVE e IN manifest_to_edge", bind_vars={"from": f"manifests/{manifest.id}"})
        db.aql.execute("FOR e IN view_to_manifest FILTER e._to == @to REMOVE e IN view_to_manifest", bind_vars={"to": f"manifests/{manifest.id}"})

        for node in manifest.nodes:
            node_doc = node.model_dump(mode="json")
            node_doc.update({"_key": node.id, "manifest_id": manifest.id})
            nodes.insert(node_doc)
            manifest_to_node.insert({"_from": f"manifests/{manifest.id}", "_to": f"manifest_nodes/{node.id}"})

        for edge in manifest.edges:
            edge_doc = edge.model_dump(mode="json")
            edge_doc.update({"_key": edge.id, "manifest_id": manifest.id})
            edges.insert(edge_doc)
            manifest_to_edge.insert({"_from": f"manifests/{manifest.id}", "_to": f"manifest_edges/{edge.id}"})

        for view in manifest.views:
            view_doc = view.model_dump(mode="json")
            view_doc.update({"_key": view.id, "manifest_id": manifest.id})
            views.insert(view_doc)
            view_to_manifest.insert({"_from": f"session_views/{view.id}", "_to": f"manifests/{manifest.id}"})

        return manifest

    async def get_manifest(self, manifest_id: str, db_id: str | None = None) -> dict[str, Any] | None:
        return self.queries().manifest_scene(manifest_id)

    async def list_manifests(self, source_db_id: str | None = None, limit: int = 50) -> list[dict[str, Any]]:
        return self.queries().list_manifests(source_db_id=source_db_id, limit=limit)


class CompositeSessionBackend:
    def __init__(self, micro: MicroSessionBackend, graph: DDBGraphBackend, projector: ManifestProjector | None = None) -> None:
        self.micro = micro
        self.graph = graph
        self.projector = projector or ManifestProjector()

    async def build_manifest(self, db_id: str, title: str, include_cards: bool = True, include_triples: bool = True, include_events: bool = True, include_patterns: bool = True) -> Manifest:
        data = await self.micro.fetch_projection_inputs(db_id)
        patterns: list[dict[str, Any]] = []
        if include_patterns:
            try:
                patterns = self.graph.queries().patterns_by_confidence(0.0, limit=25)
            except Exception:
                patterns = []
        manifest = self.projector.project(
            title=title,
            source_db_id=db_id,
            cards=data["cards"] if include_cards else [],
            triples=data["triples"] if include_triples else [],
            events=data["events"] if include_events else [],
            patterns=patterns,
        )
        manifest.metadata.update(data["meta"])
        return manifest

    async def persist_manifest(self, manifest: Manifest, db_id: str, persist_to_graph: bool = True) -> Manifest:
        await self.micro.save_manifest(manifest, db_id=db_id)
        if persist_to_graph:
            await self.graph.save_manifest(manifest, db_id=db_id)
        return manifest