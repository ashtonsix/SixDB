#!/usr/bin/env python3
"""Counterexamples for component reservation lifetime and bounded selection."""

import copy
import itertools
import json
from pathlib import Path
import unittest

from batch import BatchModel, independent_set
from model import canonical, run
from scenarios import base, discovery, local_queue, part, reservation_cycle, transaction, workload


def batch(scenario, mode="batch-hold", delay=1200, period=500, solver="age", fast=True):
    s = copy.deepcopy(scenario)
    s["policy"].update({"yield": mode, "batch_us": period, "arbitration_us": delay,
                         "solver": solver, "fast_retries": fast})
    return s


class BatchChecks(unittest.TestCase):
    def test_reset_can_invalidate_every_returning_verdict(self):
        r = run(batch(reservation_cycle(), "batch-reset"))
        self.assertEqual(r["summary"]["completed"], 0)
        self.assertGreater(r["summary"]["counts"]["verdict_stale"], 0)
        self.assertNotIn("verdict_apply", r["summary"]["counts"])

    def test_stable_round_outlives_slow_arbitration(self):
        r = run(batch(reservation_cycle()))
        self.assertEqual(r["summary"]["completed"], 3)
        self.assertGreater(r["summary"]["counts"]["batch_tick_deferred"], 0)
        self.assertEqual(r["summary"]["counts"]["discarded_work"], 1)

    def test_delayed_verdict_cannot_invalidate_new_generations(self):
        m = BatchModel(batch(reservation_cycle()))
        while not m.waiting_rounds:
            self.assertTrue(m.step())
        m.invalidate("T2", ["a"], "independent decision")
        while not m.counts["verdict_stale"]:
            self.assertTrue(m.step())
        self.assertEqual(m.transactions["T2"].parts["a"].generation, 1)
        self.assertEqual(m.counts["verdict_apply"], 0)

    def test_multiple_victims_do_not_restart_a_dependent_early(self):
        m = BatchModel(batch(discovery()))
        while m.transactions["T1"].parts["read-B"].state != "prepared":
            self.assertTrue(m.step())
        m.invalidate("T1", ["read-A", "read-B"], "two conflicting parts")
        self.assertEqual(m.transactions["T1"].parts["read-A"].state, "ready")
        self.assertEqual(m.transactions["T1"].parts["read-B"].state, "hidden")
        m.check()

    def test_irrevocable_c2_wins_over_selection(self):
        graph = {"old": {"committing"}, "committing": {"old"}}
        chosen, _ = independent_set(graph, graph, lambda x: (x != "old", x),
                                    dict.fromkeys(graph, 1), {"committing"}, "bounded")
        self.assertEqual(chosen, {"committing"})

    def test_components_merge_through_transactions_across_shards(self):
        m = BatchModel(batch(reservation_cycle()))
        while not m.fences:
            self.assertTrue(m.step())
        self.assertEqual(len(m.fences), 1)
        fence = next(iter(m.fences.values()))
        self.assertEqual(fence["members"], ["T1", "T2", "T3"])
        self.assertEqual({s for s, _ in fence["scope"]}, {"A", "B"})

    def test_acyclic_conflict_scope_is_not_a_deadlock(self):
        # Three-node conflict scope, but all granted-lock waits run toward B.
        m = BatchModel(batch(base("acyclic scope", [
            transaction("B", [part("p", "A", "x")]),
            transaction("A", [part("p", "A", "x")], arrival=10),
            transaction("C", [part("p", "A", "x")], arrival=10),
        ])))
        while m.time < 100:
            m.step()
        self.assertFalse(m.cycles())

    def test_batch_considers_all_retries_beyond_ordinary_capacity(self):
        s = base("all retries", [transaction(f"t{i}", [part("p", "A", "x")]) for i in range(10)])
        m = BatchModel(batch(s))
        # Make all ten known retry requests as an authored state fixture.
        m.events.clear()
        for tx in m.transactions.values():
            tx.state = "preparing"
            tx.known = {"p"}
            tx.parts["p"].state = "ready"
            tx.parts["p"].attempts = 1
        m.scenario["shards"]["A"]["capacity"] = 1
        m.collect()
        self.assertTrue(all(t.parts["p"].attempts == 2 for t in m.transactions.values()))
        self.assertEqual(next(iter(m.fences.values()))["members"], [f"t{i}" for i in range(10)])

    def test_bounded_solver_matches_exhaustive_small_graphs(self):
        # Anchored MIS, with deterministic oldest vertex 0 always retained.
        vertices = list(range(5))
        pairs = list(itertools.combinations(vertices, 2))
        for mask in range(0, 1 << len(pairs), 7):
            graph = {v: set() for v in vertices}
            for i, (a, b) in enumerate(pairs):
                if mask & (1 << i):
                    graph[a].add(b)
                    graph[b].add(a)
            chosen, visits = independent_set(vertices, graph, lambda x: x, dict.fromkeys(vertices, 1), set(), "bounded")
            legal = [set(subset) for size in range(6) for subset in itertools.combinations(vertices, size)
                     if 0 in subset and not any(b in graph[a] for a, b in itertools.combinations(subset, 2))]
            self.assertEqual(len(chosen), max(map(len, legal)))
            self.assertLessEqual(visits, 20001)

    def test_large_clique_uses_bounded_work(self):
        vertices = list(range(256))
        graph = {v: set(vertices) - {v} for v in vertices}
        chosen, visits = independent_set(vertices, graph, lambda x: x, dict.fromkeys(vertices, 1), set(), "bounded")
        self.assertEqual(chosen, {0})
        self.assertLessEqual(visits, 20001)

    def test_local_component_avoids_remote_round_trip(self):
        remote = batch(local_queue())
        local = copy.deepcopy(remote)
        local["policy"]["local_solver"] = True
        a, b = run(remote), run(local)
        self.assertEqual(b["summary"]["completed"], 40)
        self.assertLess(b["summary"]["time_us"], a["summary"]["time_us"])
        self.assertGreater(b["summary"]["counts"]["local_component_solves"], 0)

    def test_exact_replay_and_seeded_safety(self):
        for mode, fast, seed in itertools.product(("batch-reset", "batch-hold"), (False, True), range(4)):
            s = batch(workload(count=16, seed=seed), mode, fast=fast)
            s["horizon_us"] = 6000
            a = run(s)
            self.assertEqual(canonical(a), canonical(run(s)))

    def test_initial_comparison_traces_are_unchanged(self):
        from scenarios import presets
        old = json.loads((Path(__file__).parent / "evidence/comparison.json").read_text())
        for row in old["comparisons"]:
            s = presets()[row["scenario"]]
            s["policy"]["yield"] = row["policy"]
            r = run(s)
            self.assertEqual(r["trace_sha256"], row["trace_sha256"])


if __name__ == "__main__":
    unittest.main()
