"""CoordSystem: assigns spatial (x, y, z) coordinates to DDB entities.

Coordinate system:
  - Zones:    outer sphere, radius=100, Fibonacci-sphere distribution
  - Agents:   within zone, radius=50 from zone center
  - Cards:    within agent, radius=10 from agent center
  - Patterns: centroid of their evidence cards plus radial offset
  - Triples:  centroid of source cards plus small radial offset
"""

from __future__ import annotations

import math
from typing import Any

from graph3d.models import Node3D, NodePosition


_GOLDEN_RATIO = (1 + math.sqrt(5)) / 2


def _fibonacci_sphere(n: int, radius: float = 1.0) -> list[tuple[float, float, float]]:
    """Return n evenly distributed points on a sphere of given radius."""
    pts: list[tuple[float, float, float]] = []
    for i in range(n):
        theta = 2 * math.pi * i / _GOLDEN_RATIO
        phi = math.acos(1 - 2 * (i + 0.5) / n)
        x = radius * math.sin(phi) * math.cos(theta)
        y = radius * math.sin(phi) * math.sin(theta)
        z = radius * math.cos(phi)
        pts.append((x, y, z))
    return pts


def _sphere_point(index: int, total: int, center: tuple[float, float, float], radius: float) -> tuple[float, float, float]:
    """Place point at index on a Fibonacci sphere around center."""
    if total == 0:
        return center
    pts = _fibonacci_sphere(max(total, 1), radius)
    px, py, pz = pts[index % len(pts)]
    return (center[0] + px, center[1] + py, center[2] + pz)


def _centroid(positions: list[tuple[float, float, float]]) -> tuple[float, float, float]:
    if not positions:
        return (0.0, 0.0, 0.0)
    n = len(positions)
    return (
        sum(p[0] for p in positions) / n,
        sum(p[1] for p in positions) / n,
        sum(p[2] for p in positions) / n,
    )


class CoordSystem:
    """Assigns and tracks spatial coordinates for all DDB graph entities.

    Usage::

        cs = CoordSystem()
        cs.assign_zones(["engineering", "product", "ops"])
        cs.assign_agent("agent-1", zone="engineering")
        cs.assign_card("card-abc", agent_id="agent-1")
        positions = cs.all_positions()
    """

    ZONE_RADIUS = 100.0
    AGENT_RADIUS = 50.0
    CARD_RADIUS = 10.0
    PATTERN_OFFSET = 15.0
    TRIPLE_OFFSET = 5.0

    def __init__(self) -> None:
        self._positions: dict[str, tuple[float, float, float]] = {}
        self._node_types: dict[str, str] = {}
        # Track membership for incremental placement
        self._zones: list[str] = []
        self._agents_in_zone: dict[str, list[str]] = {}
        self._cards_in_agent: dict[str, list[str]] = {}

    # -- zone assignment -----------------------------------------------------

    def assign_zones(self, zone_names: list[str]) -> None:
        """Place all zones on an outer Fibonacci sphere."""
        self._zones = list(zone_names)
        n = max(len(self._zones), 1)
        pts = _fibonacci_sphere(n, self.ZONE_RADIUS)
        for i, name in enumerate(self._zones):
            self._positions[name] = pts[i]
            self._node_types[name] = "zone"
            self._agents_in_zone.setdefault(name, [])

    def assign_agent(self, agent_id: str, zone: str | None = None) -> None:
        """Place an agent within its zone's sphere."""
        zone_center: tuple[float, float, float] = (0.0, 0.0, 0.0)
        if zone and zone in self._positions:
            zone_center = self._positions[zone]
            agents = self._agents_in_zone.setdefault(zone, [])
            if agent_id not in agents:
                agents.append(agent_id)
            idx = agents.index(agent_id)
            total = len(agents)
        else:
            idx, total = 0, 1

        self._positions[agent_id] = _sphere_point(idx, total, zone_center, self.AGENT_RADIUS)
        self._node_types[agent_id] = "agent"
        self._cards_in_agent.setdefault(agent_id, [])

    def assign_card(self, card_id: str, agent_id: str | None = None) -> None:
        """Place a card within its agent's sphere."""
        agent_center: tuple[float, float, float] = (0.0, 0.0, 0.0)
        if agent_id and agent_id in self._positions:
            agent_center = self._positions[agent_id]
            cards = self._cards_in_agent.setdefault(agent_id, [])
            if card_id not in cards:
                cards.append(card_id)
            idx = cards.index(card_id)
            total = len(cards)
        else:
            idx, total = 0, 1

        self._positions[card_id] = _sphere_point(idx, total, agent_center, self.CARD_RADIUS)
        self._node_types[card_id] = "card"

    def assign_pattern(self, pattern_id: str, evidence_card_ids: list[str]) -> None:
        """Place a pattern at the centroid of its evidence cards + offset."""
        evidence_pts = [self._positions[c] for c in evidence_card_ids if c in self._positions]
        cx, cy, cz = _centroid(evidence_pts)
        # Offset outward slightly
        mag = math.sqrt(cx**2 + cy**2 + cz**2) or 1.0
        scale = (mag + self.PATTERN_OFFSET) / mag
        self._positions[pattern_id] = (cx * scale, cy * scale, cz * scale)
        self._node_types[pattern_id] = "pattern"

    def assign_triple(self, triple_id: str, source_card_ids: list[str]) -> None:
        """Place a triple near its source cards."""
        source_pts = [self._positions[c] for c in source_card_ids if c in self._positions]
        cx, cy, cz = _centroid(source_pts)
        mag = math.sqrt(cx**2 + cy**2 + cz**2) or 1.0
        scale = (mag + self.TRIPLE_OFFSET) / mag
        self._positions[triple_id] = (cx * scale, cy * scale, cz * scale)
        self._node_types[triple_id] = "triple"

    def get_position(self, node_id: str) -> tuple[float, float, float] | None:
        return self._positions.get(node_id)

    def all_positions(self) -> list[NodePosition]:
        """Return all assigned positions as NodePosition records."""
        return [
            NodePosition(
                node_id=node_id,
                node_type=self._node_types.get(node_id, "unknown"),
                x=x,
                y=y,
                z=z,
            )
            for node_id, (x, y, z) in self._positions.items()
        ]

    def apply_to_nodes(self, nodes: list[Node3D]) -> list[Node3D]:
        """Update Node3D x/y/z from computed positions. Returns updated list."""
        result: list[Node3D] = []
        for node in nodes:
            pos = self._positions.get(node.id)
            if pos:
                result.append(node.model_copy(update={"x": pos[0], "y": pos[1], "z": pos[2]}))
            else:
                result.append(node)
        return result