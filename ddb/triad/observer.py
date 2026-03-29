"""Observer — watches what IS. Reports measured state. Never proposes action."""

from __future__ import annotations

from triad.models import DDBEvent, Observation, VerificationLevel
from micro.engine import MicroDB


class Observer:
    """Reads current state from the micro DB and reports facts."""

    def __init__(self, micro_db: MicroDB) -> None:
        self.db = micro_db

    async def observe(self, event: DDBEvent) -> list[Observation]:
        """Produce observations from an incoming event.

        The observer NEVER assumes — it only reports what can be
        directly measured from the micro DB or the event payload.
        """
        observations: list[Observation] = []

        # Observe event metadata
        observations.append(Observation(
            description=f"Event received: {event.event_type} from {event.source_agent or 'unknown'}",
            evidence=[f"event_type={event.event_type}", f"payload_keys={list(event.payload.keys())}"],
            verification_level=VerificationLevel.ARTIFACT,
        ))

        # Observe current DB state
        card_count = await self.db.execute("SELECT COUNT(*) as cnt FROM cards")
        turn_count = await self.db.execute("SELECT COUNT(*) as cnt FROM turns")
        thought_count = await self.db.execute("SELECT COUNT(*) as cnt FROM thoughts")

        observations.append(Observation(
            description="Current micro DB state measured",
            evidence=[
                f"cards={card_count[0]['cnt'] if card_count else 0}",
                f"turns={turn_count[0]['cnt'] if turn_count else 0}",
                f"thoughts={thought_count[0]['cnt'] if thought_count else 0}",
            ],
            verification_level=VerificationLevel.ARTIFACT,
        ))

        # If the event contains content, observe its characteristics
        content = event.payload.get("content", "")
        if content:
            observations.append(Observation(
                description=f"Content length: {len(content)} chars",
                evidence=[f"content_length={len(content)}"],
                verification_level=VerificationLevel.ARTIFACT,
            ))

        return observations
