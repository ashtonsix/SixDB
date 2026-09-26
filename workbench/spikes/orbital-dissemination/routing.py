"""Small graph planners for the dissemination spike; no queue or tail model.

Costs are arbitrary additive planner weights. latency_us is a separate,
nonnegative, deterministic traversal duration. Parallel directed edges are
supported; input order breaks ties. See ALGORITHMS.md for composition limits.
"""

from __future__ import annotations

from collections import Counter
from dataclasses import dataclass, replace
from hashlib import blake2b
from heapq import heappop, heappush
from math import inf, isfinite, log
from random import Random
from typing import Iterable, Mapping


@dataclass(frozen=True)
class Edge:
    src: str
    dst: str
    cost: float
    latency_us: float = 0.0
    tag: str = ""


@dataclass(frozen=True)
class TreeMetrics:
    total_cost: float
    max_depth: int
    max_fanout: int
    max_arrival_us: float
    arrival_us: dict[str, float]


@dataclass(frozen=True)
class _Arc:
    src: int
    dst: int
    cost: float
    original: int
    previous: int = -1


def _inputs(nodes: Iterable[str], edges: Iterable[Edge]):
    nodes, edges = tuple(nodes), tuple(edges)
    if not nodes or len(set(nodes)) != len(nodes):
        raise ValueError("nodes must be nonempty and unique")
    if any(not isinstance(node, str) for node in nodes):
        raise ValueError("node identifiers must be strings")
    node_set = set(nodes)
    for edge in edges:
        if edge.src not in node_set or edge.dst not in node_set:
            raise ValueError("edge endpoint absent from nodes")
        if not isfinite(edge.cost):
            raise ValueError("edge costs must be finite")
        if not isfinite(edge.latency_us) or edge.latency_us < 0:
            raise ValueError("latencies must be finite and nonnegative")
    return nodes, edges


def _origins(nodes: tuple[str, ...], ready_us: Mapping[str, float]):
    if not ready_us or set(ready_us) - set(nodes):
        raise ValueError("origins must be a nonempty subset of nodes")
    if any(not isfinite(t) or t < 0 for t in ready_us.values()):
        raise ValueError("origin release times must be finite and nonnegative")


def _edmonds(nodes: tuple[int, ...], arcs: tuple[_Arc, ...], root: int):
    incoming: dict[int, int] = {}
    for index, arc in enumerate(arcs):
        if arc.dst == root or arc.src == arc.dst:
            continue
        old = incoming.get(arc.dst)
        if old is None or (arc.cost, arc.original) < (
            arcs[old].cost, arcs[old].original
        ):
            incoming[arc.dst] = index
    if len(incoming) != len(nodes) - 1:
        raise ValueError("no rooted spanning arborescence")

    cycle: list[int] = []
    finished = {root}
    for start in nodes:
        order: dict[int, int] = {}
        current = start
        while current not in finished and current not in order:
            order[current] = len(order)
            current = arcs[incoming[current]].src
        if current in order:
            cycle = list(order)[order[current]:]
            break
        finished.update(order)
    if not cycle:
        return tuple(incoming[node] for node in nodes if node != root)

    # Contract one cycle. Its selected incoming costs are already paid;
    # replacing the edge into v therefore has reduced cost w(u,v)-w(in(v)).
    members = set(cycle)
    contracted = max(nodes) + 1
    reduced: list[_Arc] = []
    for index, arc in enumerate(arcs):
        src = contracted if arc.src in members else arc.src
        dst = contracted if arc.dst in members else arc.dst
        if src == dst:
            continue
        cost = arc.cost
        if arc.dst in members and arc.src not in members:
            cost -= arcs[incoming[arc.dst]].cost
        reduced.append(_Arc(src, dst, cost, arc.original, index))
    result = _edmonds(
        tuple(node for node in nodes if node not in members) + (contracted,),
        tuple(reduced), root,
    )
    expanded = [reduced[index].previous for index in result]
    entering = next(arcs[index].dst for index in expanded
                    if arcs[index].dst in members and arcs[index].src not in members)
    expanded.extend(incoming[node] for node in cycle if node != entering)
    return tuple(expanded)


def minimum_arborescence(
    nodes: Iterable[str], edges: Iterable[Edge], root: str
) -> tuple[Edge, ...]:
    """Minimize sum(cost) for a directed tree reaching every node from root.

    This simple recursive Chu–Liu/Edmonds implementation is for modest spike
    graphs, not production-scale routing. Infeasible inputs raise ValueError.
    Negative finite costs, parallel edges and zero costs are supported.
    """
    nodes, edges = _inputs(nodes, edges)
    if root not in nodes:
        raise ValueError("root absent from nodes")
    ids = {node: index for index, node in enumerate(nodes)}
    arcs = tuple(_Arc(ids[e.src], ids[e.dst], e.cost, i)
                 for i, e in enumerate(edges))
    selected = _edmonds(tuple(range(len(nodes))), arcs, ids[root])
    return tuple(edges[arcs[index].original] for index in selected)


def minimum_cost_forest(
    nodes: Iterable[str], edges: Iterable[Edge], origins: Iterable[str]
) -> tuple[Edge, ...]:
    """Minimum edge-cost forest when every origin is already available.

    Origins have mandatory zero-cost edges from a synthetic root and no
    other incoming edge. Release times are deliberately not part of cost.
    """
    nodes, edges = _inputs(nodes, edges)
    origins = tuple(origins)
    if len(set(origins)) != len(origins):
        raise ValueError("duplicate origins")
    _origins(nodes, {node: 0.0 for node in origins})
    synthetic = "__synthetic_origin__"
    while synthetic in nodes:
        synthetic += "_"
    expanded = tuple(e for e in edges if e.dst not in origins) + tuple(
        Edge(synthetic, node, 0) for node in origins
    )
    return tuple(e for e in minimum_arborescence(nodes + (synthetic,), expanded,
                                                synthetic) if e.src != synthetic)


def shortest_path_forest(
    nodes: Iterable[str], edges: Iterable[Edge], ready_us: Mapping[str, float]
) -> tuple[Edge, ...]:
    """Earliest deterministic arrival paths from origins with release times.

    Equivalent to Dijkstra with a synthetic root->origin edge of ready_us.
    A late origin can receive the same usable message before producing it.
    No serialization between siblings or shared-resource delay is charged.
    """
    nodes, edges = _inputs(nodes, edges)
    _origins(nodes, ready_us)
    outgoing: dict[str, list[tuple[int, Edge]]] = {node: [] for node in nodes}
    for index, edge in enumerate(edges):
        outgoing[edge.src].append((index, edge))
    distance = {node: ready_us.get(node, inf) for node in nodes}
    parent: dict[str, int] = {}
    queue: list[tuple[float, str]] = []
    for node, ready in ready_us.items():
        heappush(queue, (ready, node))
    while queue:
        arrival, node = heappop(queue)
        if arrival != distance[node]:
            continue
        for index, edge in outgoing[node]:
            candidate = arrival + edge.latency_us
            if candidate < distance[edge.dst]:
                distance[edge.dst] = candidate
                parent[edge.dst] = index
                heappush(queue, (candidate, edge.dst))
    if any(distance[node] == inf for node in nodes):
        raise ValueError("some nodes are unreachable from origins")
    return tuple(edges[parent[node]] for node in nodes if node in parent)


def tree_metrics(
    nodes: Iterable[str], tree: Iterable[Edge], ready_us: Mapping[str, float]
) -> TreeMetrics:
    """Score forest structure and unloaded paths; these are not percentiles."""
    nodes, tree = _inputs(nodes, tree)
    _origins(nodes, ready_us)
    incoming: dict[str, Edge] = {}
    for edge in tree:
        if edge.dst in incoming:
            raise ValueError("multiple incoming edges")
        incoming[edge.dst] = edge
    arrival: dict[str, float] = {}
    depth: dict[str, int] = {}
    visiting: set[str] = set()

    def visit(node: str):
        if node in arrival:
            return
        if node in visiting:
            raise ValueError("tree contains a cycle")
        visiting.add(node)
        if node in incoming:
            edge = incoming[node]
            visit(edge.src)
            arrival[node] = min(ready_us.get(node, inf),
                                arrival[edge.src] + edge.latency_us)
            depth[node] = depth[edge.src] + 1
        elif node in ready_us:
            arrival[node] = ready_us[node]
            depth[node] = 0
        else:
            raise ValueError("forest has an unavailable root")
        visiting.remove(node)

    for node in nodes:
        visit(node)
    return TreeMetrics(sum(edge.cost for edge in tree), max(depth.values()),
                       max(Counter(edge.src for edge in tree).values(), default=0),
                       max(arrival.values()), arrival)


def tree_family(
    nodes: Iterable[str], edges: Iterable[Edge], root: str, *, count: int = 4,
    seed: int = 0, jitter: float = 0.1, reuse_penalty: float = 0.25,
) -> tuple[tuple[Edge, ...], ...]:
    """Generate up to count distinct cost-perturbed trees, without guarantees.

    Jitter and previously selected-edge penalties are in units of
    max(1,abs(edge.cost)); scores are discarded and original edges returned.
    This promotes edge diversity, not failure-domain or resource diversity.
    """
    nodes, edges = _inputs(nodes, edges)
    if not isinstance(count, int) or count < 1:
        raise ValueError("count must be a positive integer")
    if any(not isfinite(x) or x < 0 for x in (jitter, reuse_penalty)):
        raise ValueError("jitter and reuse penalty must be finite and nonnegative")
    rng = Random(seed)
    uses: Counter[int] = Counter()
    signatures: set[tuple[int, ...]] = set()
    family = []
    for attempt in range(count * 8):
        weighted = tuple(replace(edge, cost=edge.cost + (0 if attempt == 0 else
            max(1.0, abs(edge.cost)) * (
                rng.uniform(-jitter, jitter) + reuse_penalty * uses[index])))
            for index, edge in enumerate(edges))
        by_identity = {id(edge): i for i, edge in enumerate(weighted)}
        plan = minimum_arborescence(nodes, weighted, root)
        signature = tuple(sorted(by_identity[id(edge)] for edge in plan))
        uses.update(signature)
        if signature in signatures:
            continue
        signatures.add(signature)
        family.append(tuple(edges[index] for index in signature))
        if len(family) == count:
            break
    return tuple(family)


def sender_order(
    message_id: str, candidates: Mapping[str, float], *, salt: str = ""
) -> tuple[str, ...]:
    """Stable weighted hash ranking for routing, never execution authority.

    Same message, salt, candidate IDs and weights produce the same order.
    Adding/removing candidates preserves survivors' relative order. Weight is
    an eligible capacity share; -log(U)/weight is an exponential race.
    """
    if not candidates or any(not isfinite(w) or w <= 0 for w in candidates.values()):
        raise ValueError("sender weights must be finite and positive")

    def score(node: str):
        digest = blake2b(digest_size=8, person=b"orbital-route")
        for part in (salt, message_id, node):
            data = part.encode("utf-8")
            digest.update(len(data).to_bytes(8, "big"))
            digest.update(data)
        # 53 bits avoid rounding U to 1.0 at the top of the uint64 range.
        bits = int.from_bytes(digest.digest(), "big") >> 11
        uniform = (bits + 1) / ((1 << 53) + 1)
        return -log(uniform) / candidates[node], node

    return tuple(sorted(candidates, key=score))


def merge_frontiers(frontiers: Iterable[Mapping[str, int]]) -> dict[str, int]:
    """Merge already certified contiguous stream prefixes by componentwise max.

    The caller must establish each input's durability/contiguity evidence.
    This helper neither discovers sequence holes nor combines raw receipts.
    """
    merged: dict[str, int] = {}
    for frontier in frontiers:
        for stream, position in frontier.items():
            if not isinstance(position, int) or isinstance(position, bool) or position < 0:
                raise ValueError("frontier positions must be nonnegative integers")
            merged[stream] = max(merged.get(stream, 0), position)
    return merged
