"""AG-UI protocol data models.

Follows the AG-UI specification (docs.ag-ui.com) for interop with
CopilotKit, Microsoft Agent Framework, Pydantic AI, and other AG-UI hosts.
"""

from __future__ import annotations

from datetime import datetime, timezone
from enum import Enum
from typing import Any, Literal
from uuid import uuid4

from pydantic import BaseModel, Field


# ---------------------------------------------------------------------------
# AG-UI Event Types (complete reference)
# ---------------------------------------------------------------------------

class AGUIEventType(str, Enum):
    # Lifecycle
    RUN_STARTED = "RunStarted"
    RUN_FINISHED = "RunFinished"
    RUN_ERROR = "RunError"
    STEP_STARTED = "StepStarted"
    STEP_FINISHED = "StepFinished"

    # Text messages
    TEXT_MESSAGE_START = "TextMessageStart"
    TEXT_MESSAGE_CONTENT = "TextMessageContent"
    TEXT_MESSAGE_END = "TextMessageEnd"

    # Tool calls
    TOOL_CALL_START = "ToolCallStart"
    TOOL_CALL_ARGS = "ToolCallArgs"
    TOOL_CALL_END = "ToolCallEnd"
    TOOL_CALL_RESULT = "ToolCallResult"

    # State management
    STATE_SNAPSHOT = "StateSnapshot"
    STATE_DELTA = "StateDelta"
    MESSAGES_SNAPSHOT = "MessagesSnapshot"

    # Reasoning (O/O/S triad maps here)
    REASONING_START = "ReasoningStart"
    REASONING_MESSAGE_START = "ReasoningMessageStart"
    REASONING_MESSAGE_CONTENT = "ReasoningMessageContent"
    REASONING_MESSAGE_END = "ReasoningMessageEnd"
    REASONING_END = "ReasoningEnd"

    # Custom / raw
    CUSTOM = "Custom"
    RAW = "Raw"


# ---------------------------------------------------------------------------
# AG-UI Message roles
# ---------------------------------------------------------------------------

class AGUIRole(str, Enum):
    USER = "user"
    ASSISTANT = "assistant"
    SYSTEM = "system"
    TOOL = "tool"
    DEVELOPER = "developer"
    ACTIVITY = "activity"
    REASONING = "reasoning"


# ---------------------------------------------------------------------------
# AG-UI Message model
# ---------------------------------------------------------------------------

class AGUIMessage(BaseModel):
    """A single message in an AG-UI conversation thread."""
    message_id: str = Field(default_factory=lambda: str(uuid4()), alias="messageId")
    role: AGUIRole
    content: str | None = None
    tool_call_id: str | None = Field(default=None, alias="toolCallId")
    tool_call_name: str | None = Field(default=None, alias="toolCallName")
    timestamp: datetime = Field(default_factory=lambda: datetime.now(timezone.utc))

    model_config = {"populate_by_name": True}


# ---------------------------------------------------------------------------
# AG-UI Tool definition
# ---------------------------------------------------------------------------

class AGUITool(BaseModel):
    """AG-UI tool definition."""
    name: str
    description: str
    parameters: dict[str, Any] = Field(default_factory=dict)


# ---------------------------------------------------------------------------
# AG-UI Context
# ---------------------------------------------------------------------------

class AGUIContext(BaseModel):
    description: str
    value: str


# ---------------------------------------------------------------------------
# RunAgentInput — the input schema for an AG-UI agent run
# ---------------------------------------------------------------------------

class RunAgentInput(BaseModel):
    """Input to start an AG-UI agent run.

    This is the standard AG-UI schema that hosts send to agent backends.
    DDB sessions accept this format directly.
    """
    thread_id: str = Field(alias="threadId")
    run_id: str = Field(alias="runId")
    parent_run_id: str | None = Field(default=None, alias="parentRunId")
    state: dict[str, Any] = Field(default_factory=dict)
    messages: list[AGUIMessage] = Field(default_factory=list)
    tools: list[AGUITool] = Field(default_factory=list)
    context: list[AGUIContext] = Field(default_factory=list)
    forwarded_props: dict[str, Any] = Field(default_factory=dict, alias="forwardedProps")

    model_config = {"populate_by_name": True}


# ---------------------------------------------------------------------------
# AG-UI Event — the base event format emitted via SSE
# ---------------------------------------------------------------------------

class AGUIEvent(BaseModel):
    """A single AG-UI event emitted via Server-Sent Events."""
    type: AGUIEventType
    timestamp: datetime = Field(default_factory=lambda: datetime.now(timezone.utc))
    raw_event: dict[str, Any] | None = Field(default=None, alias="rawEvent")

    # Lifecycle fields
    run_id: str | None = Field(default=None, alias="runId")
    thread_id: str | None = Field(default=None, alias="threadId")
    parent_run_id: str | None = Field(default=None, alias="parentRunId")

    # Message fields
    message_id: str | None = Field(default=None, alias="messageId")
    role: AGUIRole | None = None
    delta: str | None = None  # for content/args streaming

    # Tool call fields
    tool_call_id: str | None = Field(default=None, alias="toolCallId")
    tool_call_name: str | None = Field(default=None, alias="toolCallName")
    content: str | None = None  # for tool result

    # Step fields
    step_name: str | None = Field(default=None, alias="stepName")

    # State fields
    state: dict[str, Any] | None = None
    messages: list[AGUIMessage] | None = None

    # Error
    message: str | None = None  # for RunError

    # Custom
    name: str | None = None  # for Custom event
    value: Any = None  # for Custom event

    model_config = {"populate_by_name": True}

    def to_sse(self) -> str:
        """Serialize to Server-Sent Events format."""
        data = self.model_dump(by_alias=True, exclude_none=True)
        import json
        return f"event: {self.type.value}\ndata: {json.dumps(data, default=str)}\n\n"
