"""High-level service API for DarbotDB 3DKG sessions."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from darbotdb.config import settings
from darbotdb.session.backends import CompositeSessionBackend, DDBGraphBackend, MicroSessionBackend
from darbotdb.session.models import SceneRequest, SessionContext
from graph.manifest import Manifest
from micro.manager import MicroDBManager


class SessionService:
    def __init__(self) -> None:
        manager = MicroDBManager(Path(settings.MICRO_DATA_ROOT))
        self.micro = MicroSessionBackend(manager)
        self.graph = DDBGraphBackend()
        self.composite = CompositeSessionBackend(self.micro, self.graph)

    async def list_sessions(self):
        return await self.micro.list_sessions()

    async def get_session_status(self, db_id: str) -> dict[str, Any]:
        return await self.micro.get_status(db_id)

    async def ensure_context(self, context: SessionContext):
        return await self.micro.ensure_context(context)

    async def build_manifest(self, request: SceneRequest | dict[str, Any]) -> Manifest:
        body = request if isinstance(request, SceneRequest) else SceneRequest(**request)
        manifest = await self.composite.build_manifest(
            db_id=body.db_id,
            title=body.title,
            include_cards=body.include_cards,
            include_triples=body.include_triples,
            include_events=body.include_events,
            include_patterns=body.include_patterns,
        )
        if body.persist:
            await self.composite.persist_manifest(manifest, db_id=body.db_id, persist_to_graph=True)
        return manifest

    async def get_manifest(self, manifest_id: str, db_id: str | None = None) -> dict[str, Any] | None:
        if db_id:
            local = await self.micro.get_manifest(manifest_id, db_id=db_id)
            if local:
                return local
        graph_manifest = await self.graph.get_manifest(manifest_id)
        return graph_manifest

    async def list_manifests(self, db_id: str | None = None, limit: int = 50) -> list[dict[str, Any]]:
        if db_id:
            local = await self.micro.list_manifests(db_id=db_id, limit=limit)
            if local:
                return local
        return await self.graph.list_manifests(source_db_id=db_id, limit=limit)

    async def materialize_scene(self, manifest_id: str, db_id: str | None = None) -> dict[str, Any]:
        manifest = await self.get_manifest(manifest_id, db_id=db_id)
        if manifest is None:
            raise FileNotFoundError(f"Manifest '{manifest_id}' not found")
        if "manifest" in manifest and "nodes" in manifest and "edges" in manifest:
            return manifest
        return manifest

    async def recall(self, query: str, db_id: str | None = None, zone: str | None = None, limit: int = 20) -> dict[str, Any]:
        if db_id:
            local_cards = await self.micro.search_cards(db_id, query, limit=limit)
            local_triples = await self.micro.search_triples(db_id, query, limit=limit)
        else:
            local_cards = []
            local_triples = []
            for session in await self.list_sessions():
                local_cards.extend(await self.micro.search_cards(session.db_id, query, limit=limit))
                local_triples.extend(await self.micro.search_triples(session.db_id, query, limit=limit))
        graph_patterns = self.graph.queries().patterns_by_confidence(0.0, limit=limit)
        graph_triples = self.graph.queries().triples_for_entity(query, limit=limit)
        if zone:
            local_cards = [row for row in local_cards if row.get("zone") == zone]
            local_triples = [row for row in local_triples if row.get("zone") == zone]
            graph_triples = [row for row in graph_triples if row.get("zone") == zone]
        return {
            "query": query,
            "zone": zone,
            "results": local_cards,
            "local_cards": local_cards,
            "local_triples": local_triples,
            "graph_patterns": graph_patterns,
            "graph_triples": graph_triples,
        }