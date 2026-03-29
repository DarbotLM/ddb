"""Pydantic v2 models for DDB Adaptive Cards.

Each card is dual-purpose:
  - Agent view: raw AdaptiveCard JSON for schema-first queries
  - Human view: rendered as MCP App interactive HTML
  - 3DKG view: inspector payload bound to manifest/schema graph entities
"""

from __future__ import annotations

from datetime import datetime, timezone
from enum import Enum
from typing import Any, Literal
from uuid import uuid4

from pydantic import BaseModel, ConfigDict, Field

CARD_SCHEMA_VERSION = "v1.2.0"


class CardType(str, Enum):
    MEMORY = "memory"
    TASK = "task"
    OBSERVATION = "observation"
    PATTERN = "pattern"
    INDEX = "index"


class LinkType(str, Enum):
    GRAPH_NODE = "graph_node"
    MICRO_DB = "micro_db"
    AGENT = "agent"
    CARD = "card"
    EXTERNAL = "external"
    MANIFEST = "manifest"
    SCENE = "scene"


class DDBLink(BaseModel):
    model_config = ConfigDict(populate_by_name=True)

    type: LinkType
    uri: str
    metadata: dict[str, Any] | None = None


class DDBMeta(BaseModel):
    model_config = ConfigDict(populate_by_name=True)

    id: str = Field(default_factory=lambda: str(uuid4()))
    card_type: CardType
    zone: str | None = None
    agent_id: str | None = None
    tags: list[str] = Field(default_factory=list)
    parent_card_id: str | None = None
    hash: str | None = None
    schema_version: str = CARD_SCHEMA_VERSION
    expires_at: datetime | None = None
    links: list[DDBLink] = Field(default_factory=list)
    entity_id: str | None = None
    entity_kind: str | None = None
    manifest_id: str | None = None
    view_id: str | None = None
    inspector_for: str | None = None
    projection_source: str | None = None
    spatial: dict[str, Any] = Field(default_factory=dict)
    view_hints: dict[str, Any] = Field(default_factory=dict)


class AdaptiveTextBlock(BaseModel):
    model_config = ConfigDict(populate_by_name=True)

    type: Literal["TextBlock"] = "TextBlock"
    text: str
    weight: str | None = None
    size: str | None = None
    wrap: bool | None = None
    color: str | None = None


class AdaptiveFact(BaseModel):
    title: str
    value: str


class AdaptiveFactSet(BaseModel):
    model_config = ConfigDict(populate_by_name=True)

    type: Literal["FactSet"] = "FactSet"
    facts: list[AdaptiveFact]


class AdaptiveContainer(BaseModel):
    model_config = ConfigDict(populate_by_name=True)

    type: Literal["Container"] = "Container"
    items: list[dict[str, Any]] = Field(default_factory=list)
    style: str | None = None


class AdaptiveImage(BaseModel):
    model_config = ConfigDict(populate_by_name=True)

    type: Literal["Image"] = "Image"
    url: str
    altText: str | None = None
    size: str | None = None


AdaptiveCardBody = AdaptiveTextBlock | AdaptiveFactSet | AdaptiveContainer | AdaptiveImage


class AdaptiveAction(BaseModel):
    model_config = ConfigDict(populate_by_name=True)

    type: str = "Action.Execute"
    title: str
    verb: str | None = None
    data: dict[str, Any] | None = None
    url: str | None = None


class AdaptiveCard(BaseModel):
    """Full Adaptive Card with DDB extensions."""

    model_config = ConfigDict(populate_by_name=True)

    type: Literal["AdaptiveCard"] = "AdaptiveCard"
    schema_url: str = Field(
        default="http://adaptivecards.io/schemas/adaptive-card.json",
        alias="$schema",
    )
    version: str = "1.5"
    ddb: DDBMeta = Field(alias="_ddb")
    body: list[AdaptiveCardBody] = Field(default_factory=list)
    actions: list[AdaptiveAction] = Field(default_factory=list)

    def to_agent_json(self) -> dict[str, Any]:
        return self.model_dump(by_alias=True, exclude_none=True)

    def to_human_summary(self) -> str:
        lines: list[str] = []
        title = next(
            (b.text for b in self.body if isinstance(b, AdaptiveTextBlock) and b.weight == "Bolder"),
            self.ddb.card_type.value.title(),
        )
        lines.append(f"## {title}")
        lines.append(f"**Type:** {self.ddb.card_type.value} | **Zone:** {self.ddb.zone or '—'}")
        if self.ddb.manifest_id:
            lines.append(f"**Manifest:** {self.ddb.manifest_id}")
        if self.ddb.entity_kind or self.ddb.entity_id:
            lines.append(f"**Entity:** {self.ddb.entity_kind or 'entity'} / {self.ddb.entity_id or '—'}")
        if self.ddb.tags:
            lines.append(f"**Tags:** {', '.join(self.ddb.tags)}")
        for element in self.body:
            if isinstance(element, AdaptiveTextBlock) and element.weight != "Bolder":
                lines.append(f"\n{element.text}")
            elif isinstance(element, AdaptiveFactSet):
                for fact in element.facts:
                    lines.append(f"- **{fact.title}:** {fact.value}")
            elif isinstance(element, AdaptiveContainer):
                for item in element.items:
                    if item.get("type") == "TextBlock":
                        lines.append(f"\n{item.get('text', '')}")
        if self.actions:
            lines.append("\n**Actions:** " + ", ".join(a.title for a in self.actions))
        return "\n".join(lines)

    @classmethod
    def inspector_card(
        cls,
        *,
        title: str,
        content: str,
        card_type: CardType = CardType.MEMORY,
        entity_id: str | None = None,
        entity_kind: str | None = None,
        manifest_id: str | None = None,
        zone: str | None = None,
        tags: list[str] | None = None,
    ) -> "AdaptiveCard":
        now = datetime.now(timezone.utc)
        meta = DDBMeta(
            card_type=card_type,
            zone=zone,
            tags=tags or [],
            entity_id=entity_id,
            entity_kind=entity_kind,
            manifest_id=manifest_id,
            inspector_for=entity_id,
            view_hints={"inspector": True, "created_at": now.isoformat()},
        )
        return cls(
            **{"_ddb": meta},
            body=[
                AdaptiveTextBlock(text=title, weight="Bolder", size="Medium"),
                AdaptiveTextBlock(text=content, wrap=True),
            ],
            actions=[],
        )