"""Pre-built card factory functions for common DDB card types."""

from __future__ import annotations

from typing import Any

from cards.builder import CardBuilder
from cards.schema import AdaptiveCard, CardType


def _apply_common(b: CardBuilder, kwargs: dict[str, Any]) -> CardBuilder:
    """Apply common optional kwargs to a builder."""
    if "agent_id" in kwargs:
        b = b.agent(kwargs["agent_id"])
    if "zone" in kwargs:
        b = b.zone(kwargs["zone"])
    if "tags" in kwargs:
        b = b.tag(*kwargs["tags"])
    if "parent_card_id" in kwargs:
        b = b.parent(kwargs["parent_card_id"])
    return b


def _add_standard_actions(b: CardBuilder, card_id: str | None = None) -> CardBuilder:
    """Add the standard DDB actions to a card."""
    data = {"card_id": card_id} if card_id else {}
    b = b.action_execute("Recall Full Context", "ddb.recall", {**data, "depth": 3})
    b = b.action_execute("Link to Current Task", "ddb.link", data)
    b = b.action_execute("Compose Tree", "ddb.compose", data)
    return b


# ---------------------------------------------------------------------------
# Card factories
# ---------------------------------------------------------------------------

def memory_card(
    title: str,
    content: str,
    confidence: float = 0.0,
    evidence_count: int = 0,
    **kwargs: Any,
) -> AdaptiveCard:
    """Create a long-term memory card."""
    b = CardBuilder().card_type(CardType.MEMORY).title(title)
    b = b.fact("Confidence", f"{confidence:.2f}")
    b = b.fact("Evidence Count", str(evidence_count))
    b = b.text(content)
    b = _apply_common(b, kwargs)
    b = _add_standard_actions(b)
    return b.build()


def task_card(
    title: str,
    description: str,
    status: str = "pending",
    assignee: str | None = None,
    **kwargs: Any,
) -> AdaptiveCard:
    """Create an actionable task card."""
    b = CardBuilder().card_type(CardType.TASK).title(title)
    b = b.fact("Status", status)
    if assignee:
        b = b.fact("Assignee", assignee)
    b = b.text(description)
    b = _apply_common(b, kwargs)
    b = _add_standard_actions(b)
    b = b.action_execute("Mark Complete", "ddb.task.complete", {"status": "completed"})
    return b.build()


def observation_card(
    title: str,
    observed_state: str,
    perspective: str = "observer",
    verification_level: str = "none",
    **kwargs: Any,
) -> AdaptiveCard:
    """Create a raw observation card from the O/O/S triad."""
    b = CardBuilder().card_type(CardType.OBSERVATION).title(title)
    b = b.fact("Perspective", perspective)
    b = b.fact("Verification", verification_level)
    b = b.text(observed_state)
    b = _apply_common(b, kwargs)
    b = _add_standard_actions(b)
    return b.build()


def pattern_card(
    title: str,
    pattern_description: str,
    evidence_cards: list[str] | None = None,
    confidence: float = 0.0,
    **kwargs: Any,
) -> AdaptiveCard:
    """Create a synthesized pattern card ("remember forward")."""
    b = CardBuilder().card_type(CardType.PATTERN).title(title)
    b = b.fact("Confidence", f"{confidence:.2f}")
    b = b.fact("Evidence Cards", str(len(evidence_cards or [])))
    b = b.text(pattern_description)
    if evidence_cards:
        b = b.container([
            {"type": "TextBlock", "text": f"Evidence: {', '.join(evidence_cards)}", "wrap": True}
        ])
    b = _apply_common(b, kwargs)
    b = _add_standard_actions(b)
    return b.build()


def index_card(
    title: str,
    referenced_cards: list[str] | None = None,
    cross_refs: dict[str, str] | None = None,
    **kwargs: Any,
) -> AdaptiveCard:
    """Create an index-of-indexes cross-reference card."""
    b = CardBuilder().card_type(CardType.INDEX).title(title)
    refs = referenced_cards or []
    b = b.fact("Referenced Cards", str(len(refs)))
    if cross_refs:
        for k, v in cross_refs.items():
            b = b.fact(k, v)
    if refs:
        b = b.container([
            {"type": "TextBlock", "text": f"Index: {', '.join(refs)}", "wrap": True}
        ])
    b = _apply_common(b, kwargs)
    b = _add_standard_actions(b)
    return b.build()
