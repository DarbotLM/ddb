"""Orchestrator — sees system-level patterns. Designs corrections and protocols."""

from __future__ import annotations

from triad.models import Correction, DDBEvent, Observation
from micro.engine import MicroDB


class Orchestrator:
    """Analyzes observations and designs system-level corrections."""

    def __init__(self, micro_db: MicroDB) -> None:
        self.db = micro_db

    async def orchestrate(
        self, event: DDBEvent, observations: list[Observation]
    ) -> list[Correction]:
        """Analyze the event and observations to design corrections.

        The orchestrator sees patterns and designs protocols but
        NEVER executes — only the synthesizer creates artifacts.
        """
        corrections: list[Correction] = []

        # Check for card saturation in zones
        zone_counts = await self.db.execute(
            "SELECT zone, COUNT(*) as cnt FROM cards WHERE zone IS NOT NULL GROUP BY zone"
        )
        for zc in zone_counts:
            if zc["cnt"] > 100:
                corrections.append(Correction(
                    description=f"Zone '{zc['zone']}' has {zc['cnt']} cards — consider indexing",
                    affected_components=[f"zone:{zc['zone']}"],
                    proposed_action="Create an index card to consolidate this zone",
                ))

        # Check for unlinked cards (orphans)
        orphans = await self.db.execute(
            """SELECT COUNT(*) as cnt FROM cards
               WHERE parent_card_id IS NULL AND card_type != 'index'"""
        )
        orphan_count = orphans[0]["cnt"] if orphans else 0
        if orphan_count > 20:
            corrections.append(Correction(
                description=f"{orphan_count} orphan cards detected — consider composing",
                affected_components=["cards"],
                proposed_action="Run pattern detection to group related orphans",
            ))

        # Check thought balance across perspectives
        perspective_counts = await self.db.execute(
            "SELECT perspective, COUNT(*) as cnt FROM thoughts GROUP BY perspective"
        )
        counts = {r["perspective"]: r["cnt"] for r in perspective_counts}
        obs = counts.get("observer", 0)
        orch = counts.get("orchestrator", 0)
        synth = counts.get("synthesizer", 0)
        total = obs + orch + synth
        if total > 0 and synth < total * 0.1:
            corrections.append(Correction(
                description="Synthesizer underutilised — patterns may be missed",
                affected_components=["triad"],
                proposed_action="Increase synthesizer processing for incoming events",
            ))

        return corrections
