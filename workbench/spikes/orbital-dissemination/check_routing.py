#!/usr/bin/env python3
"""Exact small-graph checks and authored planner counterexamples."""

from itertools import product
from math import inf, isclose
from random import Random

from routing import (
    Edge, merge_frontiers, minimum_arborescence, minimum_cost_forest,
    sender_order, shortest_path_forest, tree_family, tree_metrics,
)


def oracle(nodes, edges, root):
    """Enumerate parent choices and independently test directed reachability."""
    children = [node for node in nodes if node != root]
    choices = [[edge for edge in edges if edge.dst == node and edge.src != node]
               for node in children]
    best = inf
    for candidate in product(*choices):
        reached = {root}
        while True:
            next_reached = reached | {e.dst for e in candidate if e.src in reached}
            if next_reached == reached:
                break
            reached = next_reached
        if len(reached) == len(nodes):
            best = min(best, sum(edge.cost for edge in candidate))
    return best


def check_graph(nodes, edges, root):
    expected = oracle(nodes, edges, root)
    try:
        actual = minimum_arborescence(nodes, edges, root)
    except ValueError:
        assert expected == inf, (nodes, edges, expected)
        return
    metrics = tree_metrics(nodes, actual, {root: 0})
    assert len(actual) == len(nodes) - 1
    assert isclose(metrics.total_cost, expected), (nodes, edges, actual, expected)
    assert actual == minimum_arborescence(nodes, edges, root)


def exact_graphs():
    cases = 0
    # Exhaustive costs exercise ties, negative costs, cycles and every root.
    nodes = ("a", "b", "c")
    pairs = tuple((u, v) for u in nodes for v in nodes if u != v)
    for weights in product((-1, 0, 1), repeat=len(pairs)):
        edges = tuple(Edge(u, v, w) for (u, v), w in zip(pairs, weights))
        for root in nodes:
            check_graph(nodes, edges, root)
            cases += 1
    # Exhaustive four-node topology includes unavailable roots and closed cycles.
    nodes = ("r", "a", "b", "c")
    pairs = tuple((u, v) for u in nodes for v in nodes if u != v)
    for mask in range(1 << len(pairs)):
        edges = tuple(Edge(u, v, (index * 7) % 5 - 2)
                      for index, (u, v) in enumerate(pairs) if mask & (1 << index))
        check_graph(nodes, edges, "r")
        cases += 1
    # Larger dense graphs exercise repeated/nested contractions and parallel arcs.
    rng = Random(1943)
    for _ in range(100):
        nodes = tuple(str(i) for i in range(rng.randrange(2, 7)))
        edges = [Edge(u, v, rng.randrange(-7, 8), rng.randrange(0, 20))
                 for u in nodes for v in nodes if rng.random() < 0.8]
        if edges:
            for edge in rng.sample(edges, min(3, len(edges))):
                edges.append(Edge(edge.src, edge.dst, edge.cost - 0.5,
                                  edge.latency_us, "parallel"))
        check_graph(nodes, edges, nodes[0])
        cases += 1
    check_graph(("only",), (), "only")
    return cases + 1


def shortest_paths():
    rng = Random(723)
    for _ in range(100):
        nodes = tuple(str(i) for i in range(6))
        edges = tuple(Edge(u, v, rng.randrange(0, 50), rng.randrange(0, 20))
                      for u in nodes for v in nodes if u != v)
        ready = {"0": 0, "2": 10, "4": 30}
        # Bellman-Ford relaxation is an independent distance oracle.
        expected = {node: ready.get(node, inf) for node in nodes}
        for _ in range(len(nodes) - 1):
            for edge in edges:
                expected[edge.dst] = min(expected[edge.dst],
                                         expected[edge.src] + edge.latency_us)
        actual = shortest_path_forest(nodes, edges, ready)
        assert tree_metrics(nodes, actual, ready).arrival_us == expected


def counterexamples():
    nodes = ("r",) + tuple(str(i) for i in range(1, 7))
    chain = tuple(Edge(nodes[i], nodes[i + 1], 1, 1) for i in range(6))
    shortcuts = tuple(Edge("r", node, 1.1, 1.1) for node in nodes[2:])
    edges = chain + shortcuts
    cheap = tree_metrics(nodes, minimum_arborescence(nodes, edges, "r"), {"r": 0})
    quick = tree_metrics(nodes, shortest_path_forest(nodes, edges, {"r": 0}), {"r": 0})
    assert (cheap.total_cost, cheap.max_depth, cheap.max_arrival_us) == (6, 6, 6)
    assert isclose(quick.total_cost, 6.5)
    assert (quick.max_depth, quick.max_fanout, quick.max_arrival_us) == (1, 6, 1.1)

    # Fast follower at 0, slower follower at 35, leader after ACK at 55 us.
    nodes = ("F", "S", "L", "C", "D")
    ready = {"F": 0, "S": 35, "L": 55}
    edges = (Edge("F", "C", 12, 12), Edge("S", "C", 4, 4),
             Edge("L", "C", 1, 1), Edge("F", "D", 80, 80),
             Edge("S", "D", 5, 5), Edge("L", "D", 8, 8))
    cheap = tree_metrics(nodes, minimum_cost_forest(nodes, edges, ready), ready)
    quick = tree_metrics(nodes, shortest_path_forest(nodes, edges, ready), ready)
    assert (cheap.arrival_us["C"], cheap.arrival_us["D"]) == (56, 40)
    assert (quick.arrival_us["C"], quick.arrival_us["D"]) == (12, 40)
    # Synthetic-root construction agrees with an explicit oracle graph.
    augmented = edges + tuple(Edge("X", source, 0) for source in ready)
    assert isclose(cheap.total_cost, oracle(nodes + ("X",), augmented, "X"))

    # In-arborescence is an out-arborescence on the reversed graph.
    inbound = (Edge("a", "w", 8), Edge("b", "w", 8),
               Edge("a", "b", 1), Edge("b", "a", 1))
    reversed_edges = tuple(Edge(e.dst, e.src, e.cost) for e in inbound)
    chosen = minimum_arborescence(("w", "a", "b"), reversed_edges, "w")
    assert sum(e.cost for e in chosen) == 9


def families_and_senders():
    nodes = ("r", "a", "b", "c", "d")
    edges = tuple(Edge(u, v, 1, 1, u + v)
                  for u in nodes for v in nodes if u != v)
    family = tree_family(nodes, edges, "r", count=4, seed=18)
    assert len(family) == 4
    assert family == tree_family(nodes, edges, "r", count=4, seed=18)
    for tree in family:
        assert tree_metrics(nodes, tree, {"r": 0}).total_cost == 4
    assert len(tree_family(("r", "a"), (Edge("r", "a", 1),), "r", count=4)) == 1

    candidates = {"a": 1, "b": 2, "c": 3, "d": 4}
    wins = dict.fromkeys(candidates, 0)
    for index in range(10000):
        message = str(index)
        order = sender_order(message, candidates, salt="route-v1")
        wins[order[0]] += 1
        for remove in candidates:
            remaining = {k: v for k, v in candidates.items() if k != remove}
            assert sender_order(message, remaining, salt="route-v1") == tuple(
                node for node in order if node != remove)
    for node, weight in candidates.items():
        assert abs(wins[node] / 10000 - weight / 10) < 0.02, wins
    assert sender_order("ab", {"c": 1}) == ("c",)
    assert sender_order("m", candidates) == sender_order("m", dict(reversed(
        tuple(candidates.items()))))


def frontiers_and_validation():
    maps = [{"p": 3, "q": 9}, {"p": 12}, {"q": 8, "r": 0}]
    expected = {"p": 12, "q": 9, "r": 0}
    assert merge_frontiers(maps) == expected
    assert merge_frontiers(maps + maps) == expected
    assert merge_frontiers(reversed(maps)) == expected
    assert merge_frontiers([merge_frontiers(maps[:2]), maps[2]]) == expected
    calls = [
        lambda: minimum_arborescence((), (), "r"),
        lambda: minimum_arborescence(("r", "r"), (), "r"),
        lambda: minimum_arborescence(("r",), (Edge("r", "x", 0),), "r"),
        lambda: minimum_arborescence(("r",), (Edge("r", "r", float("nan")),), "r"),
        lambda: shortest_path_forest(("r",), (), {"r": -1}),
        lambda: shortest_path_forest(("r", "a"), (), {"r": 0}),
        lambda: minimum_cost_forest(("r",), (), ("r", "r")),
        lambda: tree_metrics(("r", "a"), (Edge("a", "a", 0),), {"r": 0}),
        lambda: tree_family(("r",), (), "r", count=0),
        lambda: sender_order("m", {"a": 0}),
        lambda: merge_frontiers([{"p": -1}]),
        lambda: merge_frontiers([{"p": True}]),
    ]
    for call in calls:
        try:
            call()
        except ValueError:
            continue
        raise AssertionError("invalid input accepted")


if __name__ == "__main__":
    count = exact_graphs()
    shortest_paths()
    counterexamples()
    families_and_senders()
    frontiers_and_validation()
    print(f"routing checks passed: {count} exact graph cases; 100 distance oracles; "
          "chain/readiness/inbound contrasts; families; 10000 sender rankings; "
          "frontier algebra and invalid inputs")
