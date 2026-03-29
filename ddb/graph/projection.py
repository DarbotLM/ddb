"""Projection layer from semantic DDB entities into 3DKG manifests."""

from __future__ import annotations

from typing import Any

from graph.manifest import Manifest, ManifestEdge, ManifestNode, SceneView, SpatialHint


class ManifestProjector:
    """Project cards, triples, patterns, events, and zones into a scene manifest."""

    @staticmethod
    def _node_id(prefix: str, value: str) -> str:
        return f"{prefix}:{value}"

    @staticmethod
    def _spatial(index: int, layer: str) -> SpatialHint:
        base_y = {
            "zones": 180.0,
            "cards": 60.0,
            "entities": 0.0,
            "events": -120.0,
            "patterns": 120.0,
        }.get(layer, 0.0)
        return SpatialHint(
            x=((index % 8) - 4) * 120.0,
            y=base_y,
            z=(index // 8) * 80.0,
            cluster=layer,
            weight=1.0,
        )

    def project(
        self,
        *,
        title: str,
        source_db_id: str,
        cards: list[dict[str, Any]] | None = None,
        triples: list[dict[str, Any]] | None = None,
        events: list[dict[str, Any]] | None = None,
        patterns: list[dict[str, Any]] | None = None,
    ) -> Manifest:
        manifest = Manifest(title=title, source_db_id=source_db_id)
        nodes: dict[str, ManifestNode] = {}
        edges: list[ManifestEdge] = []

        def ensure_node(*, node_id: str, entity_id: str, entity_kind: str, label: str, layer: str, node_type: str, inspector_card_id: str | None = None, metadata: dict[str, Any] | None = None) -> ManifestNode:
            if node_id not in nodes:
                nodes[node_id] = ManifestNode(
                    id=node_id,
                    entity_id=entity_id,
                    entity_kind=entity_kind,
                    label=label,
                    layer=layer,
                    node_type=node_type,
                    inspector_card_id=inspector_card_id,
                    spatial=self._spatial(len(nodes), layer),
                    metadata=metadata or {},
                )
            return nodes[node_id]

        for card in cards or []:
            card_id = str(card.get("id") or card.get("_key") or card.get("title") or len(nodes))
            zone = card.get("zone")
            card_node = ensure_node(
                node_id=self._node_id("card", card_id),
                entity_id=card_id,
                entity_kind="card",
                label=card.get("title") or card_id,
                layer="cards",
                node_type=card.get("card_type", "card"),
                inspector_card_id=card_id,
                metadata={
                    "card_type": card.get("card_type"),
                    "zone": zone,
                    "tags": card.get("tags"),
                },
            )
            if zone:
                zone_node = ensure_node(
                    node_id=self._node_id("zone", zone),
                    entity_id=zone,
                    entity_kind="zone",
                    label=zone,
                    layer="zones",
                    node_type="zone",
                    metadata={},
                )
                edges.append(
                    ManifestEdge(
                        source_node_id=zone_node.id,
                        target_node_id=card_node.id,
                        relation="contains",
                        edge_type="scope",
                        label="contains",
                    )
                )
            if card.get("parent_card_id"):
                parent_id = str(card["parent_card_id"])
                parent_node = ensure_node(
                    node_id=self._node_id("card", parent_id),
                    entity_id=parent_id,
                    entity_kind="card",
                    label=parent_id,
                    layer="cards",
                    node_type="card",
                    inspector_card_id=parent_id,
                    metadata={},
                )
                edges.append(
                    ManifestEdge(
                        source_node_id=parent_node.id,
                        target_node_id=card_node.id,
                        relation="parent_of",
                        edge_type="structure",
                        label="parent",
                    )
                )

        for triple in triples or []:
            subject = str(triple.get("subject", "subject"))
            predicate = str(triple.get("predicate", "related_to"))
            object_ = str(triple.get("object", "object"))
            source_card_id = triple.get("source_card_id")
            subject_node = ensure_node(
                node_id=self._node_id("entity", subject),
                entity_id=subject,
                entity_kind="entity",
                label=subject,
                layer="entities",
                node_type="entity",
                inspector_card_id=source_card_id,
                metadata={"origin": "triple"},
            )
            object_node = ensure_node(
                node_id=self._node_id("entity", object_),
                entity_id=object_,
                entity_kind="entity",
                label=object_,
                layer="entities",
                node_type="entity",
                inspector_card_id=source_card_id,
                metadata={"origin": "triple"},
            )
            edges.append(
                ManifestEdge(
                    source_node_id=subject_node.id,
                    target_node_id=object_node.id,
                    relation=predicate,
                    edge_type="semantic",
                    label=predicate,
                    metadata={
                        "confidence": triple.get("confidence", 1.0),
                        "graph_key": triple.get("graph_key"),
                        "source_card_id": source_card_id,
                    },
                )
            )
            if source_card_id:
                card_node_id = self._node_id("card", str(source_card_id))
                if card_node_id in nodes:
                    edges.append(
                        ManifestEdge(
                            source_node_id=card_node_id,
                            target_node_id=subject_node.id,
                            relation="evidence_for",
                            edge_type="provenance",
                            label="evidence",
                        )
                    )

        for event in events or []:
            event_id = str(event.get("id") or len(nodes))
            event_node = ensure_node(
                node_id=self._node_id("event", event_id),
                entity_id=event_id,
                entity_kind="event",
                label=event.get("event_type") or f"Event {event_id}",
                layer="events",
                node_type="event",
                metadata={
                    "source_agent": event.get("source_agent"),
                    "triad_status": event.get("triad_status"),
                },
            )
            if event.get("source_agent"):
                agent = str(event["source_agent"])
                agent_node = ensure_node(
                    node_id=self._node_id("agent", agent),
                    entity_id=agent,
                    entity_kind="agent",
                    label=agent,
                    layer="zones",
                    node_type="agent",
                    metadata={},
                )
                edges.append(
                    ManifestEdge(
                        source_node_id=agent_node.id,
                        target_node_id=event_node.id,
                        relation="emitted",
                        edge_type="provenance",
                        label="emitted",
                    )
                )

        for pattern in patterns or []:
            pattern_id = str(pattern.get("_key") or pattern.get("id") or len(nodes))
            ensure_node(
                node_id=self._node_id("pattern", pattern_id),
                entity_id=pattern_id,
                entity_kind="pattern",
                label=pattern.get("title") or pattern.get("description") or pattern_id,
                layer="patterns",
                node_type="pattern",
                metadata={
                    "confidence": pattern.get("confidence"),
                    "tags": pattern.get("tags"),
                },
            )

        manifest.nodes = list(nodes.values())
        manifest.edges = edges
        manifest.views = [
            SceneView(
                manifest_id=manifest.id,
                title="Default View",
                layout=manifest.layout,
                visible_layers=sorted({node.layer for node in manifest.nodes}),
            )
        ]
        return manifest

    def materialize_scene(self, manifest: Manifest) -> dict[str, Any]:
        return manifest.to_scene_json()