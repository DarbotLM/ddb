"""Synthesizer — identifies early patterns and 'remembers forward'."""

from __future__ import annotations

from typing import Any

from triad.models import Correction, DDBEvent, Observation, Pattern
from micro.engine import MicroDB
from cards.builder import CardBuilder
from cards.schema import AdaptiveCard, CardType


class Synthesizer:
    """Cross-pattern identification engine. Creates memory cards from patterns."""

    def __init__(self, micro_db: MicroDB) -> None:
        self.db = micro_db

    async def synthesize(
        self,
        event: DDBEvent,
        observations: list[Observation],
        corrections: list[Correction],
    ) -> list[Pattern]:
        """Identify patterns from observations + corrections and 'remember forward'.

        The synthesizer is the only perspective that creates new artifacts
        (pattern cards, memory cards) in the micro DB.
        """
        patterns: list[Pattern] = []

        # Look for recurring themes in recent observations
        recent_thoughts = await self.db.get_thoughts("observer")
        recent_obs = recent_thoughts[-20:] if len(recent_thoughts) > 20 else recent_thoughts

        # Simple frequency-based pattern detection on observation evidence
        evidence_terms: dict[str, int] = {}
        for thought in recent_obs:
            obs_text = thought.get("thought", "")
            for word in obs_text.lower().split():
                if len(word) > 4:
                    evidence_terms[word] = evidence_terms.get(word, 0) + 1

        # Terms appearing 3+ times suggest a pattern
        for term, count in evidence_terms.items():
            if count >= 3:
                patterns.append(Pattern(
                    title=f"Recurring theme: {term}",
                    description=f"Term '{term}' appeared in {count} recent observations",
                    confidence=min(count / 10.0, 1.0),
                    tags=[term, "auto-detected"],
                ))

        # If corrections suggest structural issues, synthesize a meta-pattern
        if len(corrections) >= 2:
            components = []
            for c in corrections:
                components.extend(c.affected_components)
            patterns.append(Pattern(
                title="Structural attention needed",
                description=f"Multiple corrections across: {', '.join(set(components))}",
                confidence=0.6,
                evidence_card_ids=[],
                tags=["structural", "meta-pattern"],
            ))

        return patterns

    def create_pattern_card(
        self,
        pattern: Pattern,
        observations: list[Observation],
        corrections: list[Correction],
    ) -> AdaptiveCard:
        """Convert a discovered pattern into an adaptive memory card."""
        evidence_summary = "; ".join(o.description for o in observations[:5])
        correction_summary = "; ".join(c.description for c in corrections[:3])

        card = (
            CardBuilder()
            .card_type(CardType.PATTERN)
            .title(pattern.title)
            .fact("Confidence", f"{pattern.confidence:.2f}")
            .fact("Evidence Count", str(len(observations)))
            .text(pattern.description)
            .text(f"**Evidence:** {evidence_summary}")
        )

        if correction_summary:
            card = card.text(f"**Corrections:** {correction_summary}")

        for tag in pattern.tags:
            card = card.tag(tag)

        card = card.action_execute("Recall Full Context", "ddb.recall", {"depth": 3})
        card = card.action_execute("Link to Current Task", "ddb.link", {})

        return card.build()
