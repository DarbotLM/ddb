"""MicroDB -- async SQLite engine for portable .ddb agent databases."""

from __future__ import annotations

import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import aiosqlite

from micro.schema import ALL_DDL, MIGRATIONS, SCHEMA_VERSION


class MicroDB:
    """A single portable .ddb SQLite micro database."""

    def __init__(self, path: Path) -> None:
        self.path = Path(path)
        self._conn: aiosqlite.Connection | None = None

    async def create(self, **meta: str) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._conn = await aiosqlite.connect(str(self.path))
        self._conn.row_factory = aiosqlite.Row
        await self._conn.execute("PRAGMA journal_mode=WAL;")
        await self._conn.execute("PRAGMA foreign_keys=ON;")
        for ddl in ALL_DDL:
            await self._conn.execute(ddl)
        await self._conn.commit()
        now = datetime.now(timezone.utc).isoformat()
        defaults = {"schema_version": SCHEMA_VERSION, "created_at": now}
        defaults.update(meta)
        for k, v in defaults.items():
            await self.set_meta(k, v)

    async def open(self) -> None:
        if not self.path.exists():
            raise FileNotFoundError(f"Micro DB not found: {self.path}")
        self._conn = await aiosqlite.connect(str(self.path))
        self._conn.row_factory = aiosqlite.Row
        await self._conn.execute("PRAGMA journal_mode=WAL;")
        await self._conn.execute("PRAGMA foreign_keys=ON;")
        stored = await self.get_meta("schema_version")
        if not stored:
            await self.set_meta("schema_version", SCHEMA_VERSION)
        elif stored != SCHEMA_VERSION:
            await self._migrate(stored)

    async def close(self) -> None:
        if self._conn:
            await self._conn.close()
            self._conn = None

    async def __aenter__(self) -> "MicroDB":
        if self.path.exists():
            await self.open()
        else:
            await self.create()
        return self

    async def __aexit__(self, *exc: object) -> None:
        await self.close()

    @property
    def conn(self) -> aiosqlite.Connection:
        if self._conn is None:
            raise RuntimeError("MicroDB is not open. Call create() or open() first.")
        return self._conn

    @staticmethod
    def _hash(data: str) -> str:
        return hashlib.sha256(data.encode("utf-8")).hexdigest()

    @staticmethod
    def _now() -> str:
        return datetime.now(timezone.utc).isoformat()

    @staticmethod
    def _json(obj: Any) -> str | None:
        if obj is None:
            return None
        return json.dumps(obj) if not isinstance(obj, str) else obj

    @staticmethod
    def _parse_version(version: str) -> tuple[int, ...]:
        text = version.lstrip("vV")
        return tuple(int(part) for part in text.split("."))

    async def _migrate(self, stored_version: str) -> None:
        stored = self._parse_version(stored_version)
        target = self._parse_version(SCHEMA_VERSION)
        if stored > target:
            raise ValueError(f"Schema mismatch: file has {stored_version}, engine expects {SCHEMA_VERSION}")
        for version in sorted(MIGRATIONS.keys(), key=self._parse_version):
            if self._parse_version(version) > stored and self._parse_version(version) <= target:
                for ddl in MIGRATIONS[version]:
                    await self.conn.execute(ddl)
        await self.conn.commit()
        await self.set_meta("schema_version", SCHEMA_VERSION)

    async def execute(self, sql: str, params: tuple[Any, ...] | dict[str, Any] = ()) -> list[dict[str, Any]]:
        cursor = await self.conn.execute(sql, params)
        rows = await cursor.fetchall()
        if rows and isinstance(rows[0], aiosqlite.Row):
            return [dict(r) for r in rows]
        return []

    async def get_meta(self, key: str) -> str | None:
        cursor = await self.conn.execute("SELECT value FROM meta WHERE key = ?", (key,))
        row = await cursor.fetchone()
        return row["value"] if row else None

    async def set_meta(self, key: str, value: str) -> None:
        await self.conn.execute(
            "INSERT INTO meta(key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value",
            (key, value),
        )
        await self.conn.commit()

    async def insert_turn(self, turn: dict[str, Any]) -> int:
        content = turn["content"]
        hash_val = self._hash(f"{turn.get('conversation_id', '')}:{turn.get('turn_number', '')}:{content}")
        now = turn.get("timestamp_utc", self._now())
        cursor = await self.conn.execute(
            """INSERT INTO turns(conversation_id, turn_number, timestamp_utc, role, content, model, tools_used, hash, schema_version)
               VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)""",
            (
                turn["conversation_id"],
                turn["turn_number"],
                now,
                turn["role"],
                content,
                turn.get("model"),
                self._json(turn.get("tools_used")),
                hash_val,
                turn.get("schema_version", SCHEMA_VERSION),
            ),
        )
        await self.conn.commit()
        return cursor.lastrowid

    async def get_turns(self, conversation_id: str) -> list[dict[str, Any]]:
        return await self.execute("SELECT * FROM turns WHERE conversation_id = ? ORDER BY turn_number", (conversation_id,))

    async def insert_card(self, card: dict[str, Any]) -> None:
        schema_json = self._json(card["schema_json"]) or "{}"
        hash_val = self._hash(schema_json)
        now = self._now()
        await self.conn.execute(
            """INSERT INTO cards(id, card_type, title, schema_json, content_md, embedding, parent_card_id, tags, zone, created_at, updated_at, expires_at, hash)
               VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
               ON CONFLICT(id) DO UPDATE SET
                   card_type = excluded.card_type,
                   title = excluded.title,
                   schema_json = excluded.schema_json,
                   content_md = excluded.content_md,
                   embedding = excluded.embedding,
                   parent_card_id = excluded.parent_card_id,
                   tags = excluded.tags,
                   zone = excluded.zone,
                   updated_at = excluded.updated_at,
                   expires_at = excluded.expires_at,
                   hash = excluded.hash""",
            (
                card["id"],
                card["card_type"],
                card["title"],
                schema_json,
                card.get("content_md"),
                card.get("embedding"),
                card.get("parent_card_id"),
                self._json(card.get("tags")),
                card.get("zone"),
                card.get("created_at", now),
                card.get("updated_at", now),
                card.get("expires_at"),
                hash_val,
            ),
        )
        await self.conn.commit()

    async def get_card(self, card_id: str) -> dict[str, Any] | None:
        rows = await self.execute("SELECT * FROM cards WHERE id = ?", (card_id,))
        return rows[0] if rows else None

    async def search_cards(self, query: str, limit: int = 50) -> list[dict[str, Any]]:
        return await self.execute(
            """SELECT c.* FROM cards_fts f
               JOIN cards c ON c.rowid = f.rowid
               WHERE cards_fts MATCH ?
               ORDER BY rank
               LIMIT ?""",
            (query, limit),
        )

    async def get_card_tree(self, card_id: str, depth: int = 3) -> list[dict[str, Any]]:
        result: list[dict[str, Any]] = []
        await self._collect_children(card_id, depth, 0, result)
        return result

    async def _collect_children(self, card_id: str, max_depth: int, current: int, acc: list[dict[str, Any]]) -> None:
        card = await self.get_card(card_id)
        if card is None:
            return
        card["_depth"] = current
        acc.append(card)
        if current >= max_depth:
            return
        children = await self.execute("SELECT id FROM cards WHERE parent_card_id = ?", (card_id,))
        for child in children:
            await self._collect_children(child["id"], max_depth, current + 1, acc)

    async def insert_thought(self, thought: dict[str, Any]) -> int:
        cursor = await self.conn.execute(
            """INSERT INTO thoughts(thought_number, total_thoughts, perspective, thought, assumptions, observations, verification_level, is_revision, revises_thought, branch_id, timestamp_utc)
               VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
            (
                thought["thought_number"],
                thought["total_thoughts"],
                thought["perspective"],
                thought["thought"],
                self._json(thought.get("assumptions")),
                self._json(thought.get("observations")),
                thought["verification_level"],
                int(thought.get("is_revision", False)),
                thought.get("revises_thought"),
                thought.get("branch_id"),
                thought.get("timestamp_utc", self._now()),
            ),
        )
        await self.conn.commit()
        return cursor.lastrowid

    async def get_thoughts(self, perspective: str | None = None) -> list[dict[str, Any]]:
        if perspective:
            return await self.execute("SELECT * FROM thoughts WHERE perspective = ? ORDER BY thought_number", (perspective,))
        return await self.execute("SELECT * FROM thoughts ORDER BY thought_number")

    async def insert_link(self, link: dict[str, Any]) -> int:
        cursor = await self.conn.execute(
            """INSERT INTO links(link_type, source_id, target_uri, metadata, created_at)
               VALUES (?, ?, ?, ?, ?)""",
            (
                link["link_type"],
                link["source_id"],
                link["target_uri"],
                self._json(link.get("metadata")),
                link.get("created_at", self._now()),
            ),
        )
        await self.conn.commit()
        return cursor.lastrowid

    async def get_links(self, source_id: str | None = None, link_type: str | None = None) -> list[dict[str, Any]]:
        sql = "SELECT * FROM links WHERE 1=1"
        params: list[Any] = []
        if source_id:
            sql += " AND source_id = ?"
            params.append(source_id)
        if link_type:
            sql += " AND link_type = ?"
            params.append(link_type)
        return await self.execute(sql, tuple(params))

    async def insert_event(self, event: dict[str, Any]) -> int:
        now = event.get("timestamp_utc", self._now())
        cursor = await self.conn.execute(
            """INSERT INTO events(
                   event_type, source_agent, payload_json, triad_status,
                   observer_thoughts, orchestrator_thoughts, synthesizer_thoughts,
                   new_cards, error_message, processed_at, timestamp_utc
               ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
            (
                event["event_type"],
                event.get("source_agent"),
                self._json(event.get("payload")),
                event.get("triad_status", "pending"),
                event.get("observer_thoughts", 0),
                event.get("orchestrator_thoughts", 0),
                event.get("synthesizer_thoughts", 0),
                event.get("new_cards", 0),
                event.get("error_message"),
                event.get("processed_at"),
                now,
            ),
        )
        await self.conn.commit()
        return cursor.lastrowid

    async def update_event(self, event_id: int, **fields: Any) -> None:
        allowed = {"triad_status", "observer_thoughts", "orchestrator_thoughts", "synthesizer_thoughts", "new_cards", "error_message", "processed_at"}
        updates = {k: v for k, v in fields.items() if k in allowed}
        if not updates:
            return
        set_clause = ", ".join(f"{k} = ?" for k in updates)
        await self.conn.execute(f"UPDATE events SET {set_clause} WHERE id = ?", (*updates.values(), event_id))
        await self.conn.commit()

    async def get_events(self, event_type: str | None = None, source_agent: str | None = None, triad_status: str | None = None, limit: int = 50) -> list[dict[str, Any]]:
        sql = "SELECT * FROM events WHERE 1=1"
        params: list[Any] = []
        if event_type:
            sql += " AND event_type = ?"
            params.append(event_type)
        if source_agent:
            sql += " AND source_agent = ?"
            params.append(source_agent)
        if triad_status:
            sql += " AND triad_status = ?"
            params.append(triad_status)
        sql += " ORDER BY timestamp_utc DESC LIMIT ?"
        params.append(limit)
        return await self.execute(sql, tuple(params))

    async def insert_triple(self, triple: dict[str, Any]) -> int:
        cursor = await self.conn.execute(
            """INSERT INTO triples(subject, predicate, object, confidence, source_card_id, source_event_id, model, zone, graph_key, created_at)
               VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
            (
                triple["subject"],
                triple["predicate"],
                triple["object"],
                triple.get("confidence", 1.0),
                triple.get("source_card_id"),
                triple.get("source_event_id"),
                triple.get("model"),
                triple.get("zone"),
                triple.get("graph_key"),
                triple.get("created_at", self._now()),
            ),
        )
        await self.conn.commit()
        return cursor.lastrowid

    async def get_triples(self, subject: str | None = None, predicate: str | None = None, object_: str | None = None, zone: str | None = None, limit: int = 100) -> list[dict[str, Any]]:
        sql = "SELECT * FROM triples WHERE 1=1"
        params: list[Any] = []
        if subject:
            sql += " AND subject = ?"
            params.append(subject)
        if predicate:
            sql += " AND predicate = ?"
            params.append(predicate)
        if object_:
            sql += " AND object = ?"
            params.append(object_)
        if zone:
            sql += " AND zone = ?"
            params.append(zone)
        sql += " ORDER BY confidence DESC LIMIT ?"
        params.append(limit)
        return await self.execute(sql, tuple(params))

    async def search_triples(self, entity: str, limit: int = 50) -> list[dict[str, Any]]:
        return await self.execute("SELECT * FROM triples WHERE subject = ? OR object = ? ORDER BY confidence DESC LIMIT ?", (entity, entity, limit))

    async def insert_triples_bulk(self, triples: list[dict[str, Any]]) -> int:
        now = self._now()
        rows = [(
            t["subject"], t["predicate"], t["object"], t.get("confidence", 1.0), t.get("source_card_id"), t.get("source_event_id"), t.get("model"), t.get("zone"), t.get("graph_key"), t.get("created_at", now),
        ) for t in triples]
        await self.conn.executemany(
            """INSERT INTO triples(subject, predicate, object, confidence, source_card_id, source_event_id, model, zone, graph_key, created_at)
               VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
            rows,
        )
        await self.conn.commit()
        return len(rows)

    async def upsert_embedding(self, embedding: dict[str, Any]) -> int:
        cursor = await self.conn.execute(
            """INSERT INTO embeddings(source_id, source_type, model, dimensions, vector, created_at)
               VALUES (?, ?, ?, ?, ?, ?)
               ON CONFLICT(source_id, source_type, model) DO UPDATE SET
                   dimensions = excluded.dimensions,
                   vector = excluded.vector,
                   created_at = excluded.created_at""",
            (
                embedding["source_id"],
                embedding["source_type"],
                embedding["model"],
                embedding["dimensions"],
                embedding["vector"],
                embedding.get("created_at", self._now()),
            ),
        )
        await self.conn.commit()
        return cursor.lastrowid

    async def get_embedding(self, source_id: str, source_type: str, model: str) -> dict[str, Any] | None:
        rows = await self.execute("SELECT * FROM embeddings WHERE source_id = ? AND source_type = ? AND model = ?", (source_id, source_type, model))
        return rows[0] if rows else None

    async def get_embeddings_by_source(self, source_id: str, source_type: str) -> list[dict[str, Any]]:
        return await self.execute("SELECT * FROM embeddings WHERE source_id = ? AND source_type = ? ORDER BY created_at DESC", (source_id, source_type))

    async def upsert_manifest(self, manifest: dict[str, Any]) -> None:
        manifest_id = manifest["id"]
        manifest_json = self._json(manifest) or "{}"
        now = self._now()
        created_at = manifest.get("created_at", now)
        updated_at = manifest.get("updated_at", now)
        await self.conn.execute(
            """INSERT INTO manifests(id, title, description, session_id, source_db_id, source_kind, schema_version, layout, manifest_json, created_at, updated_at)
               VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
               ON CONFLICT(id) DO UPDATE SET
                   title = excluded.title,
                   description = excluded.description,
                   session_id = excluded.session_id,
                   source_db_id = excluded.source_db_id,
                   source_kind = excluded.source_kind,
                   schema_version = excluded.schema_version,
                   layout = excluded.layout,
                   manifest_json = excluded.manifest_json,
                   updated_at = excluded.updated_at""",
            (
                manifest_id,
                manifest["title"],
                manifest.get("description", ""),
                manifest.get("session_id"),
                manifest.get("source_db_id"),
                manifest.get("source_kind", "micro"),
                manifest.get("schema_version", SCHEMA_VERSION),
                manifest.get("layout", "force-3d"),
                manifest_json,
                created_at,
                updated_at,
            ),
        )
        await self.conn.execute("DELETE FROM manifest_nodes WHERE manifest_id = ?", (manifest_id,))
        await self.conn.execute("DELETE FROM manifest_edges WHERE manifest_id = ?", (manifest_id,))
        await self.conn.execute("DELETE FROM session_views WHERE manifest_id = ?", (manifest_id,))
        for node in manifest.get("nodes", []):
            await self.conn.execute(
                """INSERT INTO manifest_nodes(id, manifest_id, entity_id, entity_kind, label, layer, node_type, inspector_card_id, spatial_json, metadata_json, created_at)
                   VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
                (
                    node["id"],
                    manifest_id,
                    node["entity_id"],
                    node["entity_kind"],
                    node["label"],
                    node.get("layer"),
                    node.get("node_type"),
                    node.get("inspector_card_id"),
                    self._json(node.get("spatial", {})),
                    self._json(node.get("metadata", {})),
                    now,
                ),
            )
        for edge in manifest.get("edges", []):
            await self.conn.execute(
                """INSERT INTO manifest_edges(id, manifest_id, source_node_id, target_node_id, relation, edge_type, label, metadata_json, created_at)
                   VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)""",
                (
                    edge["id"],
                    manifest_id,
                    edge["source_node_id"] if "source_node_id" in edge else edge["source"],
                    edge["target_node_id"] if "target_node_id" in edge else edge["target"],
                    edge["relation"],
                    edge.get("edge_type"),
                    edge.get("label"),
                    self._json(edge.get("metadata", {})),
                    now,
                ),
            )
        for view in manifest.get("views", []):
            view_id = view["id"]
            await self.conn.execute(
                """INSERT INTO session_views(id, manifest_id, title, layout, view_json, created_at, updated_at)
                   VALUES (?, ?, ?, ?, ?, ?, ?)""",
                (
                    view_id,
                    manifest_id,
                    view.get("title", "Default View"),
                    view.get("layout", manifest.get("layout", "force-3d")),
                    self._json(view),
                    now,
                    now,
                ),
            )
        await self.conn.commit()

    async def get_manifest(self, manifest_id: str) -> dict[str, Any] | None:
        rows = await self.execute("SELECT * FROM manifests WHERE id = ?", (manifest_id,))
        if not rows:
            return None
        row = rows[0]
        try:
            manifest = json.loads(row["manifest_json"])
        except Exception:
            manifest = {
                "id": row["id"],
                "title": row["title"],
                "description": row.get("description", ""),
                "session_id": row.get("session_id"),
                "source_db_id": row.get("source_db_id"),
                "source_kind": row.get("source_kind"),
                "schema_version": row.get("schema_version"),
                "layout": row.get("layout"),
            }
        manifest["nodes"] = await self.get_manifest_nodes(manifest_id)
        manifest["edges"] = await self.get_manifest_edges(manifest_id)
        manifest["views"] = await self.get_session_views(manifest_id)
        return manifest

    async def list_manifests(self, source_db_id: str | None = None, session_id: str | None = None, limit: int = 50) -> list[dict[str, Any]]:
        sql = "SELECT id, title, description, session_id, source_db_id, source_kind, schema_version, layout, created_at, updated_at FROM manifests WHERE 1=1"
        params: list[Any] = []
        if source_db_id:
            sql += " AND source_db_id = ?"
            params.append(source_db_id)
        if session_id:
            sql += " AND session_id = ?"
            params.append(session_id)
        sql += " ORDER BY updated_at DESC LIMIT ?"
        params.append(limit)
        return await self.execute(sql, tuple(params))

    async def get_manifest_nodes(self, manifest_id: str) -> list[dict[str, Any]]:
        rows = await self.execute("SELECT * FROM manifest_nodes WHERE manifest_id = ? ORDER BY created_at, id", (manifest_id,))
        for row in rows:
            row["spatial"] = json.loads(row.pop("spatial_json") or "{}")
            row["metadata"] = json.loads(row.pop("metadata_json") or "{}")
        return rows

    async def get_manifest_edges(self, manifest_id: str) -> list[dict[str, Any]]:
        rows = await self.execute("SELECT * FROM manifest_edges WHERE manifest_id = ? ORDER BY created_at, id", (manifest_id,))
        for row in rows:
            row["metadata"] = json.loads(row.pop("metadata_json") or "{}")
        return rows

    async def save_scene_view(self, view: dict[str, Any]) -> None:
        now = self._now()
        await self.conn.execute(
            """INSERT INTO session_views(id, manifest_id, title, layout, view_json, created_at, updated_at)
               VALUES (?, ?, ?, ?, ?, ?, ?)
               ON CONFLICT(id) DO UPDATE SET
                   title = excluded.title,
                   layout = excluded.layout,
                   view_json = excluded.view_json,
                   updated_at = excluded.updated_at""",
            (
                view["id"],
                view["manifest_id"],
                view.get("title", "Default View"),
                view.get("layout", "force-3d"),
                self._json(view),
                view.get("created_at", now),
                view.get("updated_at", now),
            ),
        )
        await self.conn.commit()

    async def get_session_views(self, manifest_id: str) -> list[dict[str, Any]]:
        rows = await self.execute("SELECT * FROM session_views WHERE manifest_id = ? ORDER BY updated_at DESC", (manifest_id,))
        for row in rows:
            row.update(json.loads(row["view_json"]))
        return rows