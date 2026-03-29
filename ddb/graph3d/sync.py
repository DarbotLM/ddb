"""Graph3DSync: populate Node3D list from micro DB and ArangoDB, then persist positions."""

from __future__ import annotations

from datetime import datetime, timezone
from typing import Any

from graph3d.models import Node3D, Edge3D, Graph3DSnapshot, NodePosition
from graph3d.coords import CoordSystem
from graph3d.layout import LayoutEngine


class Graph3DSync:
    """Pull graph nodes from micro DB and/or ArangoDB, compute 3D layout, persist positions.

    Usage::

        sync = Graph3DSync()
        snapshot = await sync.from_micro(db)
        snapshot = sync.apply_layout(snapshot)
        await sync.persist_positions(snapshot, db)
    """

    def __init__(
        self,
        layout_iterations: int = 50,
        use_coord_system: bool = True,
    ) -> None:
        self.layout_iterations = layout_iterations
        self.use_coord_system = use_coord_system

    # -- pull from micro DB --------------------------------------------------

    async def from_micro(self, db: Any) -> Graph3DSnapshot:
        """Build a Graph3DSnapshot from a MicroDB instance.

        Reads cards (as card/agent/zone nodes), thoughts (as pattern nodes),
        triples (as triple nodes), and links (as edges).
        """
        nodes: list[Node3D] = []
        edges: list[Edge3D] = []
        seen_ids: set[str] = set()

        # Cards -> nodes
        cards = await db.execute(
            "SELECT id, card_type, title, zone, created_at FROM cards LIMIT 10000"
        )
        for c in cards:
            nid = c["id"]
            if nid in seen_ids:
                continue
            seen_ids.add(nid)
            nodes.append(Node3D(
                id=nid,
                node_type=c.get("card_type", "card"),
                label=c.get("title", ""),
                zone=c.get("zone"),
                metadata={"created_at": c.get("created_at", "")},
            ))

        # Triples -> nodes
        triples = await db.execute(
            "SELECT id, subject, predicate, object, zone, confidence FROM triples LIMIT 10000"
        )
        for t in triples:
            nid = str(t["id"])
            if nid in seen_ids:
                continue
            seen_ids.add(nid)
            nodes.append(Node3D(
                id=nid,
                node_type="triple",
                label=f"{t['subject']} -- {t['predicate']} --> {t['object']}",
                zone=t.get("zone"),
                metadata={"confidence": t.get("confidence", 1.0)},
            ))

        # Links -> edges
        links = await db.execute(
            "SELECT id, link_type, source_id, target_uri FROM links LIMIT 10000"
        )
        for lnk in links:
            target = lnk.get("target_uri", "")
            to_id = target.split("/")[-1] if "/" in target else target
            edges.append(Edge3D(
                id=str(lnk["id"]),
                from_id=lnk["source_id"],
                to_id=to_id,
                edge_type=lnk.get("link_type", "link"),
            ))

        return Graph3DSnapshot(nodes=nodes, edges=edges)

    # -- pull from ArangoDB --------------------------------------------------

    def from_arango(self, db: Any) -> Graph3DSnapshot:
        """Build a Graph3DSnapshot from an ArangoDB StandardDatabase.

        Reads agents, cards, patterns, triples collections and all edge collections.
        """
        nodes: list[Node3D] = []
        edges: list[Edge3D] = []
        seen_ids: set[str] = set()

        entity_collections = {
            "agents": "agent",
            "cards": "card",
            "patterns": "pattern",
            "triples": "triple",
            "memory_zones": "zone",
            "sessions": "session",
        }

        edge_collections = [
            "agent_to_agent", "agent_to_card", "card_to_card",
            "card_to_zone", "pattern_to_card", "session_to_agent",
            "triple_to_card", "agent_to_zone", "session_to_card", "event_to_card",
        ]

        for col_name, node_type in entity_collections.items():
            if not db.has_collection(col_name):
                continue
            for doc in db.collection(col_name).all():
                nid = doc.get("_key", doc.get("_id", ""))
                if nid in seen_ids:
                    continue
                seen_ids.add(nid)
                label = (
                    doc.get("name") or doc.get("title") or
                    doc.get("subject") or doc.get("mssh_name") or nid
                )
                nodes.append(Node3D(
                    id=nid,
                    node_type=node_type,
                    label=str(label),
                    zone=doc.get("zone"),
                    agent_id=doc.get("agent_id"),
                ))

        for col_name in edge_collections:
            if not db.has_collection(col_name):
                continue
            for doc in db.collection(col_name).all():
                from_full = doc.get("_from", "/")
                to_full = doc.get("_to", "/")
                edges.append(Edge3D(
                    id=doc.get("_key", ""),
                    from_id=from_full.split("/")[-1],
                    to_id=to_full.split("/")[-1],
                    edge_type=col_name,
                    weight=doc.get("weight", 1.0),
                ))

        return Graph3DSnapshot(nodes=nodes, edges=edges)

    # -- merge two snapshots -------------------------------------------------

    @staticmethod
    def merge(a: Graph3DSnapshot, b: Graph3DSnapshot) -> Graph3DSnapshot:
        """Deduplicate and merge two snapshots. 'a' takes precedence on id conflicts."""
        seen_nodes: set[str] = set()
        seen_edges: set[str] = set()
        nodes: list[Node3D] = []
        edges: list[Edge3D] = []
        for n in a.nodes + b.nodes:
            if n.id not in seen_nodes:
                seen_nodes.add(n.id)
                nodes.append(n)
        for e in a.edges + b.edges:
            key = f"{e.from_id}:{e.to_id}:{e.edge_type}"
            if key not in seen_edges:
                seen_edges.add(key)
                edges.append(e)
        return Graph3DSnapshot(nodes=nodes, edges=edges)

    # -- layout --------------------------------------------------------------

    def apply_coord_system(self, snapshot: Graph3DSnapshot) -> Graph3DSnapshot:
        """Apply CoordSystem initial placement to nodes."""
        cs = CoordSystem()

        # Assign zones
        zone_names = list({n.id for n in snapshot.nodes if n.node_type == "zone"})
        if zone_names:
            cs.assign_zones(zone_names)

        # Assign agents
        for n in snapshot.nodes:
            if n.node_type == "agent":
                cs.assign_agent(n.id, zone=n.zone)

        # Assign cards
        for n in snapshot.nodes:
            if n.node_type == "card":
                cs.assign_card(n.id, agent_id=n.agent_id)

        # Assign patterns and triples by centroid of linked nodes
        edge_map: dict[str, list[str]] = {}
        for e in snapshot.edges:
            edge_map.setdefault(e.from_id, []).append(e.to_id)

        for n in snapshot.nodes:
            if n.node_type == "pattern":
                evidence = edge_map.get(n.id, [])
                cs.assign_pattern(n.id, evidence)
            elif n.node_type == "triple":
                sources = edge_map.get(n.id, [])
                cs.assign_triple(n.id, sources)

        updated_nodes = cs.apply_to_nodes(snapshot.nodes)
        return Graph3DSnapshot(nodes=updated_nodes, edges=snapshot.edges)

    def apply_layout(self, snapshot: Graph3DSnapshot, jitter: bool = True) -> Graph3DSnapshot:
        """Apply force-directed layout to refine positions."""
        engine = LayoutEngine()
        nodes = snapshot.nodes
        if jitter:
            nodes = engine.jitter(nodes, magnitude=2.0)
        nodes = engine.run_layout(nodes, snapshot.edges, iterations=self.layout_iterations)
        return Graph3DSnapshot(nodes=nodes, edges=snapshot.edges)

    # -- persist positions ---------------------------------------------------

    async def persist_positions(self, snapshot: Graph3DSnapshot, db: Any) -> int:
        """Write node x/y/z back to the micro DB node_positions table."""
        now = datetime.now(timezone.utc).isoformat()
        count = 0
        for n in snapshot.nodes:
            await db.conn.execute(
                """INSERT INTO node_positions(node_id, node_type, x, y, z, updated_at)
                   VALUES (?, ?, ?, ?, ?, ?)
                   ON CONFLICT(node_id, node_type) DO UPDATE SET
                     x=excluded.x, y=excluded.y, z=excluded.z, updated_at=excluded.updated_at""",
                (n.id, n.node_type, n.x, n.y, n.z, now),
            )
            count += 1
        await db.conn.commit()
        return count

    async def load_positions(self, db: Any) -> dict[str, tuple[float, float, float]]:
        """Load persisted positions from micro DB. Returns {node_id: (x,y,z)}."""
        rows = await db.execute("SELECT node_id, x, y, z FROM node_positions")
        return {r["node_id"]: (r["x"], r["y"], r["z"]) for r in rows}

    # -- full pipeline -------------------------------------------------------

    async def full_sync(
        self,
        micro_db: Any | None = None,
        arango_db: Any | None = None,
    ) -> Graph3DSnapshot:
        """Run the complete pipeline: pull -> coord init -> layout -> persist.

        At least one of micro_db or arango_db must be provided.
        """
        snapshot = Graph3DSnapshot()

        if micro_db is not None:
            micro_snap = await self.from_micro(micro_db)
            snapshot = self.merge(snapshot, micro_snap)

        if arango_db is not None:
            arango_snap = self.from_arango(arango_db)
            snapshot = self.merge(snapshot, arango_snap)

        if self.use_coord_system:
            snapshot = self.apply_coord_system(snapshot)

        snapshot = self.apply_layout(snapshot)

        if micro_db is not None:
            await self.persist_positions(snapshot, micro_db)

        return snapshot