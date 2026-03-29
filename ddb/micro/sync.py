"""MicroDBSync — stub for bidirectional sync between micro DBs and the DDB graph."""

from __future__ import annotations

from typing import Any


class MicroDBSync:
    """Placeholder for future sync between SQLite micro DBs and the DDB graph.

    When implemented this will:
    - Push new/updated cards from a micro DB into the DDB ``cards`` collection
    - Pull graph-discovered patterns back into the local micro DB
    - Maintain edge collections (agent_to_card, card_to_card, etc.)
    """

    async def sync_cards_to_graph(self, micro_db: Any, graph_client: Any) -> int:
        """Push cards from *micro_db* to the DDB graph. Returns count synced."""
        raise NotImplementedError("DDB graph sync not yet implemented")

    async def sync_from_graph(self, micro_db: Any, graph_client: Any) -> int:
        """Pull graph patterns into *micro_db*. Returns count synced."""
        raise NotImplementedError("DDB graph sync not yet implemented")

    async def sync_bidirectional(self, micro_db: Any, graph_client: Any) -> dict[str, int]:
        """Full bidirectional sync. Returns ``{"pushed": n, "pulled": m}``."""
        raise NotImplementedError("DDB graph sync not yet implemented")
