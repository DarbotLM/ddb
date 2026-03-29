"""Data models for the Observer/Orchestrator/Synthesizer triad."""

from __future__ import annotations

from datetime import datetime, timezone
from enum import Enum
from typing import Any

from pydantic import BaseModel, Field


class Perspective(str, Enum):
    OBSERVER = "observer"
    ORCHESTRATOR = "orchestrator"
    SYNTHESIZER = "synthesizer"


class VerificationLevel(str, Enum):
    NONE = "none"
    EXECUTION = "execution"
    ARTIFACT = "artifact"
    INGESTION = "ingestion"
    FUNCTION = "function"


class DDBEvent(BaseModel):
    """An incoming event to be processed by the triad engine."""
    event_type: str  # turn, tool_call, card_created, pattern_detected, ...
    source_agent: str | None = None
    payload: dict[str, Any] = Field(default_factory=dict)
    timestamp: datetime = Field(default_factory=lambda: datetime.now(timezone.utc))


class Observation(BaseModel):
    """Output from the Observer — measured state, no assumptions."""
    perspective: Perspective = Perspective.OBSERVER
    description: str
    evidence: list[str] = Field(default_factory=list)
    verification_level: VerificationLevel = VerificationLevel.NONE
    timestamp: datetime = Field(default_factory=lambda: datetime.now(timezone.utc))


class Correction(BaseModel):
    """Output from the Orchestrator — system-level pattern and correction design."""
    perspective: Perspective = Perspective.ORCHESTRATOR
    description: str
    affected_components: list[str] = Field(default_factory=list)
    proposed_action: str | None = None
    timestamp: datetime = Field(default_factory=lambda: datetime.now(timezone.utc))


class Pattern(BaseModel):
    """Output from the Synthesizer — cross-pattern identification for 'remember forward'."""
    perspective: Perspective = Perspective.SYNTHESIZER
    title: str
    description: str
    confidence: float = 0.0
    evidence_card_ids: list[str] = Field(default_factory=list)
    tags: list[str] = Field(default_factory=list)
    timestamp: datetime = Field(default_factory=lambda: datetime.now(timezone.utc))
