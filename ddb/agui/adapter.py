"""Bidirectional adapter: DDB ↔ AG-UI event conversion."""

from __future__ import annotations

from typing import Any
from uuid import uuid4

from agui.models import AGUIEvent, AGUIEventType, AGUIMessage, AGUIRole


def to_agui_events(ddb_turns: list[dict[str, Any]]) -> list[AGUIEvent]:
    """Convert DDB turns into a flat list of AG-UI events.

    Each turn becomes TextMessageStart → TextMessageContent → TextMessageEnd.
    """
    events: list[AGUIEvent] = []
    for turn in ddb_turns:
        msg_id = str(uuid4())
        role_str = turn.get("role", "assistant")
        role = AGUIRole(role_str) if role_str in [r.value for r in AGUIRole] else AGUIRole.ASSISTANT

        events.append(AGUIEvent(
            type=AGUIEventType.TEXT_MESSAGE_START,
            message_id=msg_id,
            role=role,
        ))
        events.append(AGUIEvent(
            type=AGUIEventType.TEXT_MESSAGE_CONTENT,
            message_id=msg_id,
            delta=turn.get("content", ""),
        ))
        events.append(AGUIEvent(
            type=AGUIEventType.TEXT_MESSAGE_END,
            message_id=msg_id,
        ))

    return events


def from_agui_messages(messages: list[AGUIMessage]) -> list[dict[str, Any]]:
    """Convert AG-UI messages into DDB turn dicts ready for insert_turn().

    This is the ingest path: AG-UI host sends messages, we store as DDB turns.
    """
    turns: list[dict[str, Any]] = []
    for i, msg in enumerate(messages):
        turn: dict[str, Any] = {
            "conversation_id": "",  # caller must set thread_id
            "turn_number": i + 1,
            "role": msg.role.value,
            "content": msg.content or "",
        }
        if msg.tool_call_name:
            turn["tools_used"] = [msg.tool_call_name]
        turns.append(turn)
    return turns


def card_to_agui_message(card_json: dict[str, Any]) -> AGUIMessage:
    """Convert a DDB adaptive card JSON into an AG-UI assistant message.

    The card JSON is embedded as the message content so AG-UI hosts
    can render it (or pass it through to their UI layer).
    """
    import json
    return AGUIMessage(
        role=AGUIRole.ASSISTANT,
        content=json.dumps(card_json, default=str),
    )


def thought_to_agui_message(thought: dict[str, Any]) -> AGUIMessage:
    """Convert a DDB O/O/S thought into an AG-UI reasoning message."""
    import json
    perspective = thought.get("perspective", "observer")
    prefix = {"observer": "👁", "orchestrator": "🎯", "synthesizer": "⚡"}.get(perspective, "")

    parts = [f"{prefix} [{perspective}] {thought.get('thought', '')}"]
    if thought.get("observations"):
        obs = json.loads(thought["observations"]) if isinstance(thought["observations"], str) else thought["observations"]
        parts.append(f"Observed: {', '.join(obs)}")
    if thought.get("verification_level"):
        parts.append(f"Verification: {thought['verification_level']}")

    return AGUIMessage(
        role=AGUIRole.REASONING,
        content="\n".join(parts),
    )
