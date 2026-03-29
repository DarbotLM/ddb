"""Fluent builder for DDB Adaptive Cards."""

from __future__ import annotations

from datetime import datetime
from typing import Any
from uuid import uuid4

from cards.schema import (
    AdaptiveAction,
    AdaptiveCard,
    AdaptiveCardBody,
    AdaptiveContainer,
    AdaptiveFact,
    AdaptiveFactSet,
    AdaptiveImage,
    AdaptiveTextBlock,
    CardType,
    DDBLink,
    DDBMeta,
    LinkType,
)
from cards.validator import compute_hash


class CardBuilder:
    """Fluent API for constructing Adaptive Cards."""

    def __init__(self) -> None:
        self._card_type: CardType = CardType.MEMORY
        self._body: list[AdaptiveCardBody] = []
        self._actions: list[AdaptiveAction] = []
        self._pending_facts: list[AdaptiveFact] = []
        self._tags: list[str] = []
        self._links: list[DDBLink] = []
        self._zone: str | None = None
        self._agent_id: str | None = None
        self._parent_card_id: str | None = None
        self._expires_at: datetime | None = None
        self._id: str = str(uuid4())
        self._entity_id: str | None = None
        self._entity_kind: str | None = None
        self._manifest_id: str | None = None
        self._view_id: str | None = None
        self._inspector_for: str | None = None
        self._projection_source: str | None = None
        self._spatial: dict[str, Any] = {}
        self._view_hints: dict[str, Any] = {}

    def card_type(self, ct: CardType) -> "CardBuilder":
        self._card_type = ct
        return self

    def id(self, card_id: str) -> "CardBuilder":
        self._id = card_id
        return self

    def title(self, text: str) -> "CardBuilder":
        self._body.append(AdaptiveTextBlock(text=text, weight="Bolder", size="Medium"))
        return self

    def text(self, text: str) -> "CardBuilder":
        self._body.append(AdaptiveTextBlock(text=text, wrap=True))
        return self

    def fact(self, title: str, value: str) -> "CardBuilder":
        self._pending_facts.append(AdaptiveFact(title=title, value=value))
        return self

    def image(self, url: str, alt: str | None = None) -> "CardBuilder":
        self._body.append(AdaptiveImage(url=url, altText=alt))
        return self

    def container(self, items: list[dict[str, Any]]) -> "CardBuilder":
        self._body.append(AdaptiveContainer(items=items))
        return self

    def action_execute(self, title: str, verb: str, data: dict[str, Any] | None = None) -> "CardBuilder":
        self._actions.append(AdaptiveAction(type="Action.Execute", title=title, verb=verb, data=data))
        return self

    def action_url(self, title: str, url: str) -> "CardBuilder":
        self._actions.append(AdaptiveAction(type="Action.OpenUrl", title=title, url=url))
        return self

    def agent(self, agent_id: str) -> "CardBuilder":
        self._agent_id = agent_id
        return self

    def zone(self, zone: str) -> "CardBuilder":
        self._zone = zone
        return self

    def tag(self, *tags: str) -> "CardBuilder":
        self._tags.extend(tags)
        return self

    def parent(self, card_id: str) -> "CardBuilder":
        self._parent_card_id = card_id
        return self

    def expires(self, dt: datetime) -> "CardBuilder":
        self._expires_at = dt
        return self

    def link(self, link_type: LinkType, uri: str, metadata: dict[str, Any] | None = None) -> "CardBuilder":
        self._links.append(DDBLink(type=link_type, uri=uri, metadata=metadata))
        return self

    def entity(self, entity_id: str, entity_kind: str) -> "CardBuilder":
        self._entity_id = entity_id
        self._entity_kind = entity_kind
        return self

    def manifest(self, manifest_id: str, view_id: str | None = None) -> "CardBuilder":
        self._manifest_id = manifest_id
        self._view_id = view_id
        return self

    def inspector_for(self, target: str) -> "CardBuilder":
        self._inspector_for = target
        return self

    def projection_source(self, source: str) -> "CardBuilder":
        self._projection_source = source
        return self

    def spatial(self, **spatial: Any) -> "CardBuilder":
        self._spatial.update(spatial)
        return self

    def view_hint(self, **hints: Any) -> "CardBuilder":
        self._view_hints.update(hints)
        return self

    def build(self) -> AdaptiveCard:
        if self._pending_facts:
            self._body.append(AdaptiveFactSet(facts=list(self._pending_facts)))
            self._pending_facts.clear()

        meta = DDBMeta(
            id=self._id,
            card_type=self._card_type,
            zone=self._zone,
            agent_id=self._agent_id,
            tags=list(self._tags),
            parent_card_id=self._parent_card_id,
            expires_at=self._expires_at,
            links=list(self._links),
            entity_id=self._entity_id,
            entity_kind=self._entity_kind,
            manifest_id=self._manifest_id,
            view_id=self._view_id,
            inspector_for=self._inspector_for,
            projection_source=self._projection_source,
            spatial=dict(self._spatial),
            view_hints=dict(self._view_hints),
        )

        card = AdaptiveCard(
            **{"_ddb": meta},
            body=list(self._body),
            actions=list(self._actions),
        )
        card.ddb.hash = compute_hash(card)
        return card