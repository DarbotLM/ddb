"""AG-UI Event Emitter — SSE streaming helper for FastAPI endpoints."""

from __future__ import annotations

import asyncio
import json
from typing import AsyncIterator

from agui.models import AGUIEvent, AGUIEventType


class AGUIEventEmitter:
    """Manages an AG-UI SSE event stream.

    Use with FastAPI's StreamingResponse::

        emitter = AGUIEventEmitter()

        async def generate():
            emitter.run_started(run_id, thread_id)
            async for event in emitter.stream():
                yield event

            # From another coroutine:
            emitter.text_message("Hello from DDB")
            emitter.finish()

        return StreamingResponse(generate(), media_type="text/event-stream")
    """

    def __init__(self) -> None:
        self._queue: asyncio.Queue[AGUIEvent | None] = asyncio.Queue()
        self._finished = False

    # -- Lifecycle -----------------------------------------------------------

    def run_started(
        self, run_id: str, thread_id: str, parent_run_id: str | None = None
    ) -> None:
        self._put(AGUIEvent(
            type=AGUIEventType.RUN_STARTED,
            run_id=run_id,
            thread_id=thread_id,
            parent_run_id=parent_run_id,
        ))

    def run_finished(self, run_id: str, thread_id: str) -> None:
        self._put(AGUIEvent(
            type=AGUIEventType.RUN_FINISHED,
            run_id=run_id,
            thread_id=thread_id,
        ))
        self._finished = True
        self._queue.put_nowait(None)  # sentinel

    def run_error(self, message: str) -> None:
        self._put(AGUIEvent(type=AGUIEventType.RUN_ERROR, message=message))
        self._finished = True
        self._queue.put_nowait(None)

    def step_started(self, step_name: str) -> None:
        self._put(AGUIEvent(type=AGUIEventType.STEP_STARTED, step_name=step_name))

    def step_finished(self, step_name: str) -> None:
        self._put(AGUIEvent(type=AGUIEventType.STEP_FINISHED, step_name=step_name))

    # -- Text messages -------------------------------------------------------

    def text_message(self, content: str, role: str = "assistant") -> None:
        """Emit a complete text message as Start→Content→End."""
        from uuid import uuid4
        from agui.models import AGUIRole
        msg_id = str(uuid4())
        r = AGUIRole(role)

        self._put(AGUIEvent(type=AGUIEventType.TEXT_MESSAGE_START, message_id=msg_id, role=r))
        self._put(AGUIEvent(type=AGUIEventType.TEXT_MESSAGE_CONTENT, message_id=msg_id, delta=content))
        self._put(AGUIEvent(type=AGUIEventType.TEXT_MESSAGE_END, message_id=msg_id))

    def text_message_chunk(self, msg_id: str, delta: str) -> None:
        """Emit a streaming text chunk."""
        self._put(AGUIEvent(type=AGUIEventType.TEXT_MESSAGE_CONTENT, message_id=msg_id, delta=delta))

    # -- Tool calls ----------------------------------------------------------

    def tool_call(
        self, tool_name: str, args: dict, result: str
    ) -> None:
        """Emit a complete tool call as Start→Args→End→Result."""
        from uuid import uuid4
        tc_id = str(uuid4())
        msg_id = str(uuid4())

        self._put(AGUIEvent(
            type=AGUIEventType.TOOL_CALL_START,
            tool_call_id=tc_id, tool_call_name=tool_name, message_id=msg_id,
        ))
        self._put(AGUIEvent(
            type=AGUIEventType.TOOL_CALL_ARGS,
            tool_call_id=tc_id, delta=json.dumps(args, default=str),
        ))
        self._put(AGUIEvent(type=AGUIEventType.TOOL_CALL_END, tool_call_id=tc_id))
        self._put(AGUIEvent(
            type=AGUIEventType.TOOL_CALL_RESULT,
            tool_call_id=tc_id, message_id=str(uuid4()), content=result,
        ))

    # -- State ---------------------------------------------------------------

    def state_snapshot(self, state: dict) -> None:
        self._put(AGUIEvent(type=AGUIEventType.STATE_SNAPSHOT, state=state))

    def state_delta(self, patches: list[dict]) -> None:
        """Emit RFC 6902 JSON Patch operations."""
        self._put(AGUIEvent(type=AGUIEventType.STATE_DELTA, state={"patches": patches}))

    # -- Reasoning (O/O/S) --------------------------------------------------

    def reasoning(self, perspective: str, content: str) -> None:
        """Emit a reasoning block for an O/O/S thought."""
        from uuid import uuid4
        msg_id = str(uuid4())
        prefix = {"observer": "👁", "orchestrator": "🎯", "synthesizer": "⚡"}.get(perspective, "")

        self._put(AGUIEvent(type=AGUIEventType.REASONING_START, message_id=msg_id))
        self._put(AGUIEvent(type=AGUIEventType.REASONING_MESSAGE_START, message_id=msg_id))
        self._put(AGUIEvent(
            type=AGUIEventType.REASONING_MESSAGE_CONTENT,
            message_id=msg_id, delta=f"{prefix} [{perspective}] {content}",
        ))
        self._put(AGUIEvent(type=AGUIEventType.REASONING_MESSAGE_END, message_id=msg_id))
        self._put(AGUIEvent(type=AGUIEventType.REASONING_END, message_id=msg_id))

    # -- Custom events -------------------------------------------------------

    def custom(self, name: str, value: dict) -> None:
        self._put(AGUIEvent(type=AGUIEventType.CUSTOM, name=name, value=value))

    # -- Stream --------------------------------------------------------------

    async def stream(self) -> AsyncIterator[str]:
        """Yield SSE-formatted event strings until finished."""
        while True:
            event = await self._queue.get()
            if event is None:
                break
            yield event.to_sse()

    def _put(self, event: AGUIEvent) -> None:
        if not self._finished:
            self._queue.put_nowait(event)
