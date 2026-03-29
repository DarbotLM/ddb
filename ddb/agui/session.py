"""AG-UI Session — maps DDB micro DB state to AG-UI thread/run lifecycle."""

from __future__ import annotations

from datetime import datetime, timezone
from typing import Any, AsyncIterator
from uuid import uuid4

from micro.engine import MicroDB
from agui.models import (
    AGUIEvent,
    AGUIEventType,
    AGUIMessage,
    AGUIRole,
    RunAgentInput,
)


class AGUISession:
    """An AG-UI compatible session backed by a DDB micro database.

    This maps the AG-UI thread/run model onto DDB's turns, cards, and
    thoughts so adaptive cards flow seamlessly into AG-UI hosts like
    CopilotKit, Microsoft Agent Framework, etc.

    Usage::

        session = AGUISession(micro_db)
        async for event in session.run(run_input):
            yield event.to_sse()
    """

    def __init__(self, micro_db: MicroDB) -> None:
        self.db = micro_db
        self._thread_id: str | None = None
        self._run_id: str | None = None

    @property
    def thread_id(self) -> str:
        return self._thread_id or ""

    @property
    def run_id(self) -> str:
        return self._run_id or ""

    # -- AG-UI RunAgent entry point ------------------------------------------

    async def run(self, input: RunAgentInput) -> AsyncIterator[AGUIEvent]:
        """Execute an AG-UI agent run, yielding events as SSE stream.

        Maps AG-UI messages → DDB turns, emits AG-UI events for each
        step of processing (including O/O/S triad reasoning).
        """
        self._thread_id = input.thread_id
        self._run_id = input.run_id

        # Store thread/run metadata
        await self.db.set_meta("agui_thread_id", input.thread_id)
        await self.db.set_meta("agui_run_id", input.run_id)
        if input.parent_run_id:
            await self.db.set_meta("agui_parent_run_id", input.parent_run_id)

        # RunStarted
        yield AGUIEvent(
            type=AGUIEventType.RUN_STARTED,
            run_id=input.run_id,
            thread_id=input.thread_id,
            parent_run_id=input.parent_run_id,
        )

        # Ingest AG-UI messages as DDB turns
        yield AGUIEvent(type=AGUIEventType.STEP_STARTED, step_name="ingest_messages")
        turn_number = 0
        for msg in input.messages:
            turn_number += 1
            await self.db.insert_turn({
                "conversation_id": input.thread_id,
                "turn_number": turn_number,
                "role": msg.role.value,
                "content": msg.content or "",
                "model": input.state.get("model"),
                "tools_used": [msg.tool_call_name] if msg.tool_call_name else None,
            })
        yield AGUIEvent(type=AGUIEventType.STEP_FINISHED, step_name="ingest_messages")

        # Emit state snapshot with DDB micro DB state
        yield await self._state_snapshot(input)

        # Emit messages snapshot
        yield AGUIEvent(
            type=AGUIEventType.MESSAGES_SNAPSHOT,
            messages=input.messages,
        )

        # RunFinished
        yield AGUIEvent(
            type=AGUIEventType.RUN_FINISHED,
            run_id=input.run_id,
            thread_id=input.thread_id,
        )

    # -- Convert DDB cards to AG-UI events -----------------------------------

    async def emit_card_as_events(self, card: dict[str, Any]) -> AsyncIterator[AGUIEvent]:
        """Emit a DDB adaptive card as a sequence of AG-UI text message events.

        This lets adaptive cards appear as rich messages in AG-UI hosts.
        """
        msg_id = str(uuid4())

        yield AGUIEvent(
            type=AGUIEventType.TEXT_MESSAGE_START,
            message_id=msg_id,
            role=AGUIRole.ASSISTANT,
        )

        # Emit card content as streaming chunks
        import json
        card_json = json.dumps(card, default=str)
        yield AGUIEvent(
            type=AGUIEventType.TEXT_MESSAGE_CONTENT,
            message_id=msg_id,
            delta=card_json,
        )

        yield AGUIEvent(
            type=AGUIEventType.TEXT_MESSAGE_END,
            message_id=msg_id,
        )

    # -- Convert DDB thoughts to AG-UI reasoning events ----------------------

    async def emit_thought_as_reasoning(
        self, thought: dict[str, Any]
    ) -> AsyncIterator[AGUIEvent]:
        """Emit a DDB O/O/S thought as AG-UI reasoning events.

        Maps:
        - observer → reasoning (factual observations)
        - orchestrator → reasoning (system analysis)
        - synthesizer → reasoning (pattern synthesis)
        """
        msg_id = str(uuid4())
        perspective = thought.get("perspective", "observer")
        prefix = {"observer": "👁", "orchestrator": "🎯", "synthesizer": "⚡"}.get(perspective, "")

        yield AGUIEvent(type=AGUIEventType.REASONING_START, message_id=msg_id)
        yield AGUIEvent(type=AGUIEventType.REASONING_MESSAGE_START, message_id=msg_id)

        content = f"{prefix} [{perspective}] {thought.get('thought', '')}"
        if thought.get("observations"):
            import json
            obs = json.loads(thought["observations"]) if isinstance(thought["observations"], str) else thought["observations"]
            content += f"\nObserved: {', '.join(obs)}"
        if thought.get("assumptions"):
            import json
            assumptions = json.loads(thought["assumptions"]) if isinstance(thought["assumptions"], str) else thought["assumptions"]
            if assumptions:
                content += f"\nAssumed: {', '.join(assumptions)}"

        yield AGUIEvent(
            type=AGUIEventType.REASONING_MESSAGE_CONTENT,
            message_id=msg_id,
            delta=content,
        )

        yield AGUIEvent(type=AGUIEventType.REASONING_MESSAGE_END, message_id=msg_id)
        yield AGUIEvent(type=AGUIEventType.REASONING_END, message_id=msg_id)

    # -- Convert DDB tool calls to AG-UI tool events -------------------------

    async def emit_tool_call(
        self,
        tool_name: str,
        args: dict[str, Any],
        result: str,
    ) -> AsyncIterator[AGUIEvent]:
        """Emit a tool call as AG-UI tool events."""
        import json
        tc_id = str(uuid4())
        msg_id = str(uuid4())

        yield AGUIEvent(
            type=AGUIEventType.TOOL_CALL_START,
            tool_call_id=tc_id,
            tool_call_name=tool_name,
            message_id=msg_id,
        )

        yield AGUIEvent(
            type=AGUIEventType.TOOL_CALL_ARGS,
            tool_call_id=tc_id,
            delta=json.dumps(args, default=str),
        )

        yield AGUIEvent(type=AGUIEventType.TOOL_CALL_END, tool_call_id=tc_id)

        yield AGUIEvent(
            type=AGUIEventType.TOOL_CALL_RESULT,
            tool_call_id=tc_id,
            message_id=str(uuid4()),
            content=result,
        )

    # -- State management ----------------------------------------------------

    async def _state_snapshot(self, input: RunAgentInput) -> AGUIEvent:
        """Build an AG-UI state snapshot from the DDB micro DB."""
        card_count = await self.db.execute("SELECT COUNT(*) as cnt FROM cards")
        turn_count = await self.db.execute("SELECT COUNT(*) as cnt FROM turns")
        thought_count = await self.db.execute("SELECT COUNT(*) as cnt FROM thoughts")

        state = {
            **input.state,
            "ddb": {
                "schema_version": await self.db.get_meta("schema_version"),
                "agent_id": await self.db.get_meta("agent_id"),
                "cards": card_count[0]["cnt"] if card_count else 0,
                "turns": turn_count[0]["cnt"] if turn_count else 0,
                "thoughts": thought_count[0]["cnt"] if thought_count else 0,
            },
        }

        return AGUIEvent(type=AGUIEventType.STATE_SNAPSHOT, state=state)

    # -- Convenience: full DDB → AG-UI event stream --------------------------

    async def replay_as_agui(self, conversation_id: str) -> AsyncIterator[AGUIEvent]:
        """Replay a DDB conversation as a full AG-UI event stream.

        Useful for restoring/resuming an agent session in an AG-UI host.
        """
        turns = await self.db.get_turns(conversation_id)
        run_id = await self.db.get_meta("agui_run_id") or str(uuid4())

        yield AGUIEvent(
            type=AGUIEventType.RUN_STARTED,
            run_id=run_id,
            thread_id=conversation_id,
        )

        for turn in turns:
            msg_id = str(uuid4())
            role = AGUIRole(turn["role"]) if turn["role"] in AGUIRole.__members__.values() else AGUIRole.ASSISTANT

            yield AGUIEvent(
                type=AGUIEventType.TEXT_MESSAGE_START,
                message_id=msg_id,
                role=role,
            )
            yield AGUIEvent(
                type=AGUIEventType.TEXT_MESSAGE_CONTENT,
                message_id=msg_id,
                delta=turn["content"],
            )
            yield AGUIEvent(
                type=AGUIEventType.TEXT_MESSAGE_END,
                message_id=msg_id,
            )

        # Replay thoughts as reasoning events
        thoughts = await self.db.get_thoughts()
        for thought in thoughts:
            async for event in self.emit_thought_as_reasoning(thought):
                yield event

        yield AGUIEvent(
            type=AGUIEventType.RUN_FINISHED,
            run_id=run_id,
            thread_id=conversation_id,
        )
