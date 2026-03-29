"""DBSession Protocol -- the common interface for all DDB storage backends.

Any class that implements these methods is a valid DBSession. The Protocol
is structural (runtime_checkable) so isinstance() checks work without
explicit inheritance.
"""

from __future__ import annotations

from typing import Protocol, runtime_checkable

from session.models import NodeRecord, EdgeRecord, TripleRecord


@runtime_checkable
class DBSession(Protocol):
    """Common read/write interface for DDB storage backends.

    Implementations:
      - MicroDBSession  (session/micro_session.py) -- SQLite .ddb files
      - ArangoDBSession (session/arango_session.py) -- ArangoDB collections
      - HybridSession   (session/hybrid_session.py) -- write-both, read-local-first
    """

    # -- node CRUD -----------------------------------------------------------

    async def get(self, entity_type: str, entity_id: str) -> NodeRecord | None:
        """Retrieve a single node by type and id."""
        ...

    async def put(self, record: NodeRecord) -> str:
        """Insert or update a node. Returns the stored id."""
        ...

    async def search(
        self,
        query: str,
        entity_type: str | None = None,
        limit: int = 50,
    ) -> list[NodeRecord]:
        """Full-text search across node labels and properties."""
        ...

    async def delete(self, entity_type: str, entity_id: str) -> bool:
        """Delete a node. Returns True if found and deleted."""
        ...

    # -- edge operations -----------------------------------------------------

    async def link(self, edge: EdgeRecord) -> str:
        """Create or update an edge. Returns the stored edge id."""
        ...

    async def get_links(
        self,
        entity_id: str,
        direction: str = "outbound",
        edge_type: str | None = None,
    ) -> list[EdgeRecord]:
        """Get edges for a node. direction: "outbound" | "inbound" | "any"."""
        ...

    # -- triple operations ---------------------------------------------------

    async def put_triple(self, triple: TripleRecord) -> str:
        """Insert a knowledge triple. Returns the stored id."""
        ...

    async def search_triples(
        self,
        entity: str,
        limit: int = 50,
    ) -> list[TripleRecord]:
        """Find triples where entity appears as subject or object."""
        ...

    # -- lifecycle -----------------------------------------------------------

    async def close(self) -> None:
        """Release any held resources (connections, file handles)."""
        ...