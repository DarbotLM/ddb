"""Bidirectional sync between SQLite micro DBs and the DDB graph."""

from __future__ import annotations

import json
from typing import Any

from arango.database import StandardDatabase

from micro.engine import MicroDB


class GraphSync:
    """Sync cards and patterns between a micro DB and the DDB graph."""

    def __init__(self, db: StandardDatabase) -> None:
        self.db = db

    async def push_cards(self, micro: MicroDB) -> int:
        """Push cards from a micro DB into the DDB ``cards`` collection."""
        rows = await micro.execute("SELECT * FROM cards")
        col = self.db.collection("cards")
        pushed = 0
        for row in rows:
            doc = {
                "_key": row["id"],
                "card_type": row["card_type"],
                "title": row["title"],
                "schema_json": row["schema_json"],
                "content_md": row.get("content_md"),
                "tags": json.loads(row["tags"]) if row.get("tags") else [],
                "zone": row.get("zone"),
                "created_at": row["created_at"],
                "updated_at": row["updated_at"],
                "hash": row.get("hash"),
            }
            if col.has(row["id"]):
                col.update(doc)
            else:
                col.insert(doc)
            pushed += 1
        return pushed

    async def push_agent(self, micro: MicroDB) -> str | None:
        """Ensure the agent from this micro DB exists in the ``agents`` collection."""
        agent_id = await micro.get_meta("agent_id")
        if not agent_id:
            return None
        col = self.db.collection("agents")
        if not col.has(agent_id):
            col.insert({
                "_key": agent_id,
                "name": agent_id,
                "status": "active",
                "db_type": await micro.get_meta("db_type") or "agent",
            })
        return agent_id

    async def push_links(self, micro: MicroDB) -> int:
        """Push cross-references as DDB graph edges."""
        links = await micro.execute("SELECT * FROM links")
        pushed = 0
        for link in links:
            lt = link["link_type"]
            if lt == "card" and link["target_uri"].startswith("ddb://"):
                # card_to_card edge
                col = self.db.collection("card_to_card")
                edge = {
                    "_from": f"cards/{link['source_id']}",
                    "_to": f"cards/{link['target_uri'].split('/')[-1]}",
                    "link_type": lt,
                }
                col.insert(edge)
                pushed += 1
        return pushed

    async def pull_patterns(self, micro: MicroDB, since: str | None = None) -> int:
        """Pull newly discovered patterns from the DDB graph into the micro DB."""
        bind = {}
        aql = "FOR p IN patterns"
        if since:
            aql += " FILTER p.discovered_at > @since"
            bind["since"] = since
        aql += " RETURN p"
        cursor = self.db.aql.execute(aql, bind_vars=bind)
        pulled = 0
        for doc in cursor:
            existing = await micro.get_card(doc["_key"])
            if existing is None:
                await micro.insert_card({
                    "id": doc["_key"],
                    "card_type": "pattern",
                    "title": doc.get("title", "Pattern"),
                    "schema_json": json.dumps(doc),
                    "content_md": doc.get("description"),
                    "tags": json.dumps(doc.get("tags", [])),
                    "zone": doc.get("zone"),
                })
                pulled += 1
        return pulled

    async def sync_bidirectional(self, micro: MicroDB, since: str | None = None) -> dict[str, int]:
        """Full sync: push local cards/agent, pull remote patterns."""
        agent = await self.push_agent(micro)
        pushed = await self.push_cards(micro)
        link_count = await self.push_links(micro)
        pulled = await self.pull_patterns(micro, since)
        return {
            "agent": agent or "",
            "cards_pushed": pushed,
            "links_pushed": link_count,
            "patterns_pulled": pulled,
        }
