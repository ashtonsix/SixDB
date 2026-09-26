#!/usr/bin/env python3
"""Independent result and resource checks for the focused exchange probe."""

from collections import Counter
from itertools import product
from math import ceil
from pathlib import Path
import random
import unittest

from exchange_probe import (
    Offer, Row, algebra_examples, count_join, filter_probe_side, join_exchange,
    queue_probe, reference_join, run,
)


class ExchangeChecks(unittest.TestCase):
    def test_random_exact_bags(self):
        rng = random.Random(240926)
        for _ in range(100):
            left = [Row(i, rng.choice([None, 0, 0, 1, 2, 3]), rng.randrange(4), rng.choice([16, 64, 256]))
                    for i in range(rng.randrange(25))]
            right = [Row(i, rng.choice([None, 0, 0, 1, 2, 3]), rng.randrange(4), rng.choice([16, 64, 256]))
                     for i in range(rng.randrange(25))]
            # Independent oracle enumerates the Cartesian product; no hash table,
            # routing, salting or semijoin helper participates in expected output.
            expected = Counter((l.occurrence, r.occurrence) for l, r in product(left, right)
                               if l.key is not None and r.key is not None and l.key == r.key)
            for strategy in ("repartition", "broadcast", "semijoin", "salt", "grid"):
                stats, actual = join_exchange(left, right, strategy)
                self.assertEqual(actual, expected, strategy)
                self.assertEqual(stats["completed_input_rows"], len(left) + len(right))
                self.assertEqual(stats["output_rows"], len(list(expected.elements())))
                self.assertEqual(stats["actual_input_wire_bytes"], stats["planned_data_wire_bytes"] + stats["planned_summary_wire_bytes"])
            self.assertEqual(count_join(left, right)["count"], expected.total())

    def test_duplicate_values_are_bag_occurrences(self):
        left = [Row(i, 7, i % 4) for i in range(3)]
        right = [Row(i, 7, i % 4) for i in range(5)]
        for strategy in ("repartition", "broadcast", "semijoin", "salt", "grid"):
            _, result = join_exchange(left, right, strategy, hot_keys=frozenset({7}))
            self.assertEqual(result.total(), 15)
            self.assertTrue(all(count == 1 for count in result.values()))

    def test_wrong_independent_salts_lose_pairs(self):
        left = [Row(i, 0, i % 4) for i in range(4)]
        right = [Row(i, 0, i % 4) for i in range(4)]
        wrong = [(l.occurrence, r.occurrence) for l, r in product(left, right)
                 if l.key == r.key and l.occurrence % 4 == r.occurrence % 4]
        self.assertEqual(len(wrong), 4)
        self.assertEqual(reference_join(left, right).total(), 16)

    def test_grid_trades_input_replication_for_build_state(self):
        left = [Row(i, 0, i % 4) for i in range(32)]
        right = [Row(i, 0, i % 4) for i in range(32)]
        salt, a = join_exchange(left, right, "salt")
        grid, b = join_exchange(left, right, "grid")
        self.assertEqual(a, b)
        self.assertEqual(grid["max_build_bytes"] * 2, salt["max_build_bytes"])
        self.assertEqual(grid["planned_left_copies"], 2 * salt["planned_left_copies"])
        self.assertEqual(grid["planned_right_copies"] * 2, salt["planned_right_copies"])

    def test_memory_refusal_counts_all_inputs(self):
        left, right = [Row(0, 0, 0)], [Row(0, 0, 0, 256)]
        stats, output = join_exchange(left, right, "broadcast", build_limit=287)
        self.assertFalse(output)
        self.assertEqual(stats["status"], "plan_rejected_build_memory")
        self.assertEqual(stats["actual_input_wire_bytes"], 0)
        self.assertEqual(stats["not_admitted_input_rows"], 2)
        self.assertEqual(stats["completed_input_rows"], 0)
        complete, output = join_exchange(left, right, "broadcast", build_limit=288)
        self.assertEqual(complete["status"], "complete")
        self.assertEqual(output.total(), 1)

    def test_no_false_colocation(self):
        with self.assertRaises(ValueError):
            join_exchange([Row(0, 1, 0)], [Row(0, 1, 1)], "colocated")
        stats, output = join_exchange([Row(0, 1, 1)], [Row(0, 1, 1)], "colocated")
        self.assertEqual(stats["actual_input_wire_bytes"], 0)
        self.assertEqual(output, Counter({(0, 0): 1}))

    def test_outer_and_anti_filter_need_absence_branch(self):
        left = [Row(0, 1, 0), Row(1, 2, 0), Row(2, None, 0)]
        right = [Row(0, 1, 0), Row(1, 1, 0), Row(2, None, 0)]
        for kind in ("left", "anti"):
            expected = reference_join(left, right, kind)
            self.assertNotEqual(filter_probe_side(left, right, kind, False), expected)
            self.assertEqual(filter_probe_side(left, right, kind, True), expected)
        self.assertEqual(reference_join(left, right).total(), 2)

    def test_local_row_topk_with_total_order(self):
        rng = random.Random(20260926)
        for _ in range(100):
            rows = [(rng.randrange(5), i) for i in range(rng.randrange(60))]
            key = lambda row: (-row[0], row[1])
            for k in (0, 1, 3, 20):
                shards = [rows[shard::4] for shard in range(4)]
                candidate = [row for shard in shards for row in sorted(shard, key=key)[:k]]
                self.assertEqual(sorted(candidate, key=key)[:k], sorted(rows, key=key)[:k])

    def test_invalid_algebra_examples(self):
        example = algebra_examples()
        self.assertEqual(example["grouped_sum_top1"]["true_winner"], "B")
        self.assertNotIn("B", example["grouped_sum_top1"]["local_candidates"])
        self.assertNotEqual(*example["float_regrouping"].values())
        self.assertNotEqual(example["replay_sum"]["once"], example["replay_sum"]["duplicate_message"])
        self.assertEqual(example["replay_sum"]["once"], example["replay_sum"]["deduplicated_delivery"])
        self.assertTrue(example["cyclic_semijoin"]["every_pairwise_semijoin_preserves_every_row"])
        self.assertEqual(example["cyclic_semijoin"]["true_triangle_rows"], 0)
        self.assertEqual(example["top1_with_ties"]["true_rows"], 3)
        self.assertEqual(example["top1_with_ties"]["bounded_local_top1_rows"], 2)

    def test_queue_no_fault_complete(self):
        offers = [Offer(i, 2 * i, tuple(range(4))) for i in range(20)]
        for policy in ("fifo", "per_destination_shared", "per_destination_reserved"):
            stats = queue_probe(offers, policy, 0, 100)
            self.assertEqual(stats["complete_parents"], 20)
            self.assertEqual(stats["not_admitted_parents"], 0)
            self.assertEqual(stats["nic_bytes"], 20 * 4 * 64)
            self.assertEqual(stats["queued_bytes"], 0)

    def test_per_destination_is_not_memory_isolation(self):
        offers = [Offer(i, i, (0,) if i < 16 else (1 + i % 3,)) for i in range(64)]
        shared = queue_probe(offers, "per_destination_shared", 100, 80)
        reserved = queue_probe(offers, "per_destination_reserved", 100, 80)
        self.assertEqual(shared["healthy_only_complete"], 0)
        self.assertEqual(reserved["healthy_only_complete"], 48)
        self.assertEqual(reserved["not_admitted_parents"], 12)
        self.assertEqual(reserved["accepted_unfinished_parents"], 4)

    def test_dynamic_fanout_atomic_admission(self):
        offers = [Offer(0, 0, (0,)), Offer(1, 1, (0, 1, 2, 3)), Offer(2, 2, (1,))]
        stats = queue_probe(offers, "per_destination_reserved", 100, 10, per_destination_bytes=64)
        self.assertEqual(stats["not_admitted_parents"], 1)
        self.assertEqual(stats["not_admitted_children"], 4)
        self.assertEqual(stats["delivered_children"], 1)
        self.assertEqual(stats["queued_children"], 1)
        self.assertIsNone(stats["all_offered_p99_ticks"])

    def test_empty_fanout_completes_but_future_offer_does_not(self):
        offers = [Offer(0, 0, (0,)), Offer(1, 1, ()), Offer(2, 100, ())]
        for policy in ("fifo", "per_destination_shared", "per_destination_reserved"):
            result = queue_probe(offers, policy, 100, 10, total_bytes=64)
            self.assertEqual(result["offered_parents"], 2)
            self.assertEqual(result["complete_parents"], 1)
            self.assertEqual(result["accepted_unfinished_parents"], 1)

    def test_random_queue_conservation(self):
        rng = random.Random(92)
        for _ in range(100):
            offers = [Offer(i, rng.randrange(40), tuple(rng.sample(range(4), rng.randrange(1, 5))), rng.choice([32, 64, 128]))
                      for i in range(rng.randrange(100))]
            for policy in ("fifo", "per_destination_shared", "per_destination_reserved"):
                result = queue_probe(offers, policy, rng.randrange(60), 60)
                self.assertEqual(result["offered_parents"], len(offers))
                self.assertEqual(result["offered_parents"], result["complete_parents"] + result["not_admitted_parents"] + result["accepted_unfinished_parents"])
                self.assertEqual(result["offered_children"], result["delivered_children"] + result["not_admitted_children"] + result["queued_children"])
                self.assertLessEqual(result["peak_buffer_bytes"], result["total_buffer_bytes"])
                if result["complete_parents"] < ceil(0.99 * result["offered_parents"]):
                    self.assertIsNone(result["all_offered_p99_ticks"])

    def test_selected_evidence_reproduces(self):
        import json
        path = Path(__file__).with_name("evidence") / "exchanges.json"
        if path.exists():
            retained = json.loads(path.read_text())
            fresh = json.loads(json.dumps(run()))
            # Interpreter version is provenance, not a logical result.
            retained.pop("python")
            fresh.pop("python")
            self.assertEqual(fresh, retained)


if __name__ == "__main__":
    unittest.main()
