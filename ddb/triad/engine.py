"""TriadEngine — parallel Observer/Orchestrator/Synthesizer processor."""

from __future__ import annotations

import asyncio
from typing import Any

from micro.engine import MicroDB
from cards.schema import AdaptiveCard
from triad.models import DDBEvent, Observation, Correction, Pattern
from triad.observer import Observer
from triad.orchestrator import Orchestrator
from triad.synthesizer import Synthesizer


class TriadEngine:
    """Run all three perspectives in parallel on incoming events.

    The engine:
    1. Observes (what IS)
    2. Orchestrates (what SHOULD be)
    3. Synthesizes (what patterns EMERGE)

    New pattern cards are created and stored in the micro DB.
    """

    def __init__(self, micro_db: MicroDB) -> None:
        self.micro_db = micro_db
        self.observer = Observer(micro_db)
        self.orchestrator = Orchestrator(micro_db)
        self.synthesizer = Synthesizer(micro_db)

    async def process(self, event: DDBEvent) -> list[AdaptiveCard]:
        """Process an event through all three triad perspectives in parallel.

        Returns a list of newly created pattern cards.
        """
        # Phase 1: Observer runs independently
        observations = await self.observer.observe(event)

        # Phase 2: Orchestrator needs observations
        corrections = await self.orchestrator.orchestrate(event, observations)

        # Phase 3: Synthesizer needs both
        patterns = await self.synthesizer.synthesize(event, observations, corrections)

        # Record all thoughts in the micro DB
        await self._record_thoughts(observations, corrections, patterns, event)

        # Create and store pattern cards
        new_cards: list[AdaptiveCard] = []
        for pattern in patterns:
            card = self.synthesizer.create_pattern_card(pattern, observations, corrections)
            await self.micro_db.insert_card({
                "id": card.ddb.id,
                "card_type": card.ddb.card_type.value,
                "title": next(
                    (b.text for b in card.body if hasattr(b, "weight") and b.weight == "Bolder"),
                    pattern.title,
                ),
                "schema_json": card.to_agent_json(),
                "content_md": card.to_human_summary(),
                "tags": card.ddb.tags,
                "zone": card.ddb.zone,
            })
            new_cards.append(card)

        return new_cards

    async def _record_thoughts(
        self,
        observations: list[Observation],
        corrections: list[Correction],
        patterns: list[Pattern],
        event: DDBEvent,
    ) -> None:
        """Persist all triad outputs as thought records."""
        total = len(observations) + len(corrections) + len(patterns)
        num = 0

        for obs in observations:
            num += 1
            await self.micro_db.insert_thought({
                "thought_number": num,
                "total_thoughts": total,
                "perspective": obs.perspective.value,
                "thought": obs.description,
                "assumptions": [],
                "observations": obs.evidence,
                "verification_level": obs.verification_level.value,
            })

        for corr in corrections:
            num += 1
            await self.micro_db.insert_thought({
                "thought_number": num,
                "total_thoughts": total,
                "perspective": corr.perspective.value,
                "thought": corr.description,
                "assumptions": [corr.proposed_action] if corr.proposed_action else [],
                "observations": corr.affected_components,
                "verification_level": "none",
            })

        for pat in patterns:
            num += 1
            await self.micro_db.insert_thought({
                "thought_number": num,
                "total_thoughts": total,
                "perspective": pat.perspective.value,
                "thought": f"{pat.title}: {pat.description}",
                "assumptions": [],
                "observations": pat.evidence_card_ids,
                "verification_level": "none",
            })
