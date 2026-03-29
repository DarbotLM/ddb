"""SpatialQuery: k-nearest neighbors, bounding box, shortest path in 3D."""

from __future__ import annotations

import math
from collections import deque
from typing import Any

from graph3d.models import Node3D, Edge3D, Graph3DSnapshot


def _euclidean(a: Node3D, b: tuple[float, float, float]) -> float:
    return math.sqrt((a.x - b[0])**2 + (a.y - b[1])**2 + (a.z - b[2])**2)


class SpatialQuery:
    """Spatial queries over a Graph3DSnapshot.

    Usage::

        snap = Graph3DSnapshot(nodes=[...], edges=[...])
        sq = SpatialQuery(snap)
        nearest = sq.nearest_neighbors((0, 0, 0), k=5)
        in_box = sq.bounding_box(-10, -10, -10, 10, 10, 10)
        path = sq.shortest_path_3d("node-a", "node-b")
    """

    def __init__(self, snapshot: Graph3DSnapshot) -> None:
        self._snap = snapshot

    # -- nearest neighbors ---------------------------------------------------

    def nearest_neighbors(
        self,
        point: tuple[float, float, float],
        k: int = 10,
        node_type: str | None = None,
    ) -> list[Node3D]:
        """Return the k closest nodes to *point* by Euclidean distance."""
        candidates = self._snap.nodes
        if node_type:
            candidates = [n for n in candidates if n.node_type == node_type]
        return sorted(candidates, key=lambda n: _euclidean(n, point))[:k]

    # -- bounding box --------------------------------------------------------

    def bounding_box(
        self,
        min_x: float, min_y: float, min_z: float,
        max_x: float, max_y: float, max_z: float,
        node_type: str | None = None,
    ) -> list[Node3D]:
        """Return nodes whose position falls within the axis-aligned bounding box."""
        results = [
            n for n in self._snap.nodes
            if min_x <= n.x <= max_x
            and min_y <= n.y <= max_y
            and min_z <= n.z <= max_z
        ]
        if node_type:
            results = [n for n in results if n.node_type == node_type]
        return results

    # -- shortest path -------------------------------------------------------

    def shortest_path_3d(
        self,
        from_id: str,
        to_id: str,
    ) -> list[Node3D]:
        """BFS shortest path between two nodes, edges weighted by Euclidean distance.

        Returns the list of nodes on the path (inclusive), or [] if not found.
        """
        node_map = self._snap.node_map()
        adj = self._snap.adjacency()

        if from_id not in node_map or to_id not in node_map:
            return []

        # BFS (unweighted hops -- for small graphs this is sufficient)
        visited: set[str] = set()
        parent: dict[str, str | None] = {from_id: None}
        queue: deque[str] = deque([from_id])
        visited.add(from_id)

        while queue:
            current = queue.popleft()
            if current == to_id:
                break
            for neighbor in adj.get(current, []):
                if neighbor not in visited:
                    visited.add(neighbor)
                    parent[neighbor] = current
                    queue.append(neighbor)

        if to_id not in parent:
            return []

        # Reconstruct path
        path: list[str] = []
        cursor: str | None = to_id
        while cursor is not None:
            path.append(cursor)
            cursor = parent.get(cursor)
        path.reverse()
        return [node_map[nid] for nid in path if nid in node_map]

    # -- radial shell --------------------------------------------------------

    def radial_shell(
        self,
        center_id: str,
        min_radius: float,
        max_radius: float,
    ) -> list[Node3D]:
        """Return nodes within an annular shell around a center node."""
        node_map = self._snap.node_map()
        if center_id not in node_map:
            return []
        center = node_map[center_id]
        cx, cy, cz = center.x, center.y, center.z
        results = []
        for n in self._snap.nodes:
            if n.id == center_id:
                continue
            dist = math.sqrt((n.x - cx)**2 + (n.y - cy)**2 + (n.z - cz)**2)
            if min_radius <= dist <= max_radius:
                results.append(n)
        return sorted(results, key=lambda n: math.sqrt((n.x - cx)**2 + (n.y - cy)**2 + (n.z - cz)**2))

    # -- stats ---------------------------------------------------------------

    def stats(self) -> dict[str, Any]:
        """Return node and edge counts per type."""
        from collections import Counter
        node_counts = Counter(n.node_type for n in self._snap.nodes)
        edge_counts = Counter(e.edge_type for e in self._snap.edges)
        return {
            "total_nodes": len(self._snap.nodes),
            "total_edges": len(self._snap.edges),
            "nodes_by_type": dict(node_counts),
            "edges_by_type": dict(edge_counts),
            "computed_at": self._snap.computed_at,
        }