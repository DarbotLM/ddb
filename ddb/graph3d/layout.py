"""LayoutEngine: force-directed and semantic layout for 3D knowledge graphs."""

from __future__ import annotations

import math
import random
from typing import Any

from graph3d.models import Node3D, Edge3D


class LayoutEngine:
    """Force-directed spring-charge layout in 3D space.

    Nodes repel each other (charge), connected nodes attract (spring).
    Runs for a fixed number of iterations with configurable constants.

    Usage::

        engine = LayoutEngine()
        nodes = engine.run_layout(nodes, edges, iterations=50)
    """

    def __init__(
        self,
        spring_constant: float = 0.1,
        charge_constant: float = 500.0,
        damping: float = 0.85,
        min_distance: float = 0.1,
        max_displacement: float = 20.0,
    ) -> None:
        self.spring_k = spring_constant
        self.charge_k = charge_constant
        self.damping = damping
        self.min_dist = min_distance
        self.max_disp = max_displacement

    def run_layout(
        self,
        nodes: list[Node3D],
        edges: list[Edge3D],
        iterations: int = 50,
    ) -> list[Node3D]:
        """Run force-directed layout and return nodes with updated positions.

        Modifies x/y/z on copies of the input nodes.
        """
        if not nodes:
            return nodes

        # Work on mutable dicts to avoid Pydantic copy overhead per iteration
        state: list[dict[str, Any]] = [
            {"id": n.id, "x": n.x, "y": n.y, "z": n.z, "vx": 0.0, "vy": 0.0, "vz": 0.0}
            for n in nodes
        ]
        id_to_idx = {s["id"]: i for i, s in enumerate(state)}

        # Build edge lookup
        edge_pairs = []
        for e in edges:
            if e.from_id in id_to_idx and e.to_id in id_to_idx:
                edge_pairs.append((id_to_idx[e.from_id], id_to_idx[e.to_id], e.weight))

        for _iteration in range(iterations):
            forces = [{"fx": 0.0, "fy": 0.0, "fz": 0.0} for _ in state]

            # Repulsive forces between all node pairs (O(n^2), fine for <1000 nodes)
            for i in range(len(state)):
                for j in range(i + 1, len(state)):
                    dx = state[i]["x"] - state[j]["x"]
                    dy = state[i]["y"] - state[j]["y"]
                    dz = state[i]["z"] - state[j]["z"]
                    dist = max(math.sqrt(dx**2 + dy**2 + dz**2), self.min_dist)
                    force = self.charge_k / (dist**2)
                    fx = force * dx / dist
                    fy = force * dy / dist
                    fz = force * dz / dist
                    forces[i]["fx"] += fx
                    forces[i]["fy"] += fy
                    forces[i]["fz"] += fz
                    forces[j]["fx"] -= fx
                    forces[j]["fy"] -= fy
                    forces[j]["fz"] -= fz

            # Attractive spring forces along edges
            for i_idx, j_idx, weight in edge_pairs:
                dx = state[j_idx]["x"] - state[i_idx]["x"]
                dy = state[j_idx]["y"] - state[i_idx]["y"]
                dz = state[j_idx]["z"] - state[i_idx]["z"]
                dist = max(math.sqrt(dx**2 + dy**2 + dz**2), self.min_dist)
                force = self.spring_k * dist * weight
                fx = force * dx / dist
                fy = force * dy / dist
                fz = force * dz / dist
                forces[i_idx]["fx"] += fx
                forces[i_idx]["fy"] += fy
                forces[i_idx]["fz"] += fz
                forces[j_idx]["fx"] -= fx
                forces[j_idx]["fy"] -= fy
                forces[j_idx]["fz"] -= fz

            # Apply forces with damping + clamp displacement
            for i, s in enumerate(state):
                s["vx"] = (s["vx"] + forces[i]["fx"]) * self.damping
                s["vy"] = (s["vy"] + forces[i]["fy"]) * self.damping
                s["vz"] = (s["vz"] + forces[i]["fz"]) * self.damping
                # Clamp
                speed = math.sqrt(s["vx"]**2 + s["vy"]**2 + s["vz"]**2)
                if speed > self.max_disp:
                    scale = self.max_disp / speed
                    s["vx"] *= scale
                    s["vy"] *= scale
                    s["vz"] *= scale
                s["x"] += s["vx"]
                s["y"] += s["vy"]
                s["z"] += s["vz"]

        # Return updated Node3D objects
        id_to_new = {s["id"]: (s["x"], s["y"], s["z"]) for s in state}
        return [
            n.model_copy(update={"x": id_to_new[n.id][0], "y": id_to_new[n.id][1], "z": id_to_new[n.id][2]})
            for n in nodes
        ]

    @staticmethod
    def jitter(nodes: list[Node3D], magnitude: float = 1.0) -> list[Node3D]:
        """Add small random perturbation to break symmetry before layout."""
        return [
            n.model_copy(update={
                "x": n.x + random.uniform(-magnitude, magnitude),
                "y": n.y + random.uniform(-magnitude, magnitude),
                "z": n.z + random.uniform(-magnitude, magnitude),
            })
            for n in nodes
        ]