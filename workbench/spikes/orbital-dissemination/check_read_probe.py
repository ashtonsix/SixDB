#!/usr/bin/env python3
"""Counterexamples for read coverage, harmful fanout and relay placement."""

import unittest

from read_probe import Coverage, Recipient, Worker, fanout_tail, hedge_work, read_split, relay_analysis


class ReadCases(unittest.TestCase):
    def test_slow_snapshot_replica_can_hurt_equal_split(self):
        workers = [Worker(str(i), 0, 1000) for i in range(3)]
        workers.append(Worker("lagging", 400, 1000))
        result = read_split(1_000_000, workers)
        self.assertEqual(result["equal_partition_us"], 650)
        self.assertAlmostEqual(result["divisible_work_lower_bound_us"], 1000 / 3)
        self.assertEqual(result["bound_allocation_bytes"]["lagging"], 0)
        self.assertAlmostEqual(sum(result["bound_allocation_bytes"].values()), 1_000_000)

    def test_replication_does_not_reduce_total_scan_work(self):
        result = read_split(1_000_000, [Worker(str(i), 0, 1000) for i in range(4)])
        self.assertAlmostEqual(result["divisible_work_lower_bound_us"], 250)
        self.assertEqual(result["partition_scan_bytes"], 1_000_000)
        self.assertEqual(result["all_replicas_full_scan_bytes"], 4_000_000)

    def test_single_partition_slo_is_not_scatter_slo(self):
        result = fanout_tail(128, .99, .999)
        self.assertLess(result["independent_all_success_probability"], .28)
        self.assertGreater(result["required_independent_single_success_probability"], .99999)
        self.assertAlmostEqual(fanout_tail(1, .99, .999)["independent_all_success_probability"], .99)

    def test_harmful_hedge_is_still_charged(self):
        result = hedge_work(100, 100, 0, 0)
        self.assertEqual(result["completion_us"], 100)
        self.assertEqual(result["cpu_us"], 200)
        self.assertFalse(hedge_work(100, 100, 100, 0)["launched"])

    def test_cancellation_does_not_reclaim_past_or_inflight_work(self):
        instant = hedge_work(1000, 100, 50, 0)
        late = hedge_work(1000, 100, 50, 200)
        self.assertEqual(instant["completion_us"], 150)
        self.assertEqual(late["completion_us"], 150)
        self.assertEqual(instant["cpu_us"], 250)
        self.assertEqual(late["cpu_us"], 450)

    def test_expensive_hint_can_save_cpu_and_worsen_completion(self):
        result = relay_analysis([
            Recipient("near", 60, 20, 20, 12500, 1),
            Recipient("public", 60, 20, 20, 125, 100),
        ], 20, 10, 8, 1, 4096)
        near, public = result["rows"]
        self.assertLess(result["shared_analysis_cpu_us"], result["local_analysis_cpu_us"])
        self.assertLess(near["wait_for_hint_finish_us"], near["local_analysis_finish_us"])
        self.assertGreater(public["wait_for_hint_finish_us"], public["local_analysis_finish_us"])
        self.assertEqual(public["hint_weighted_byte_cost"], near["hint_weighted_byte_cost"] * 100)

    def test_duplicate_is_not_missing_partition_coverage(self):
        coverage = Coverage("cut", "plan", {"a", "b"})
        self.assertTrue(coverage.accept("cut", "plan", "a", "one"))
        for _ in range(10):
            self.assertFalse(coverage.accept("cut", "plan", "a", "one"))
        self.assertFalse(coverage.complete)
        self.assertTrue(coverage.accept("cut", "plan", "b", "two"))
        self.assertTrue(coverage.complete)

    def test_stale_cut_unknown_chunk_and_disagreement_cannot_complete(self):
        coverage = Coverage("cut", "plan", {"a", "b"})
        coverage.accept("cut", "plan", "a", "one")
        for item in [
            ("old-cut", "plan", "b", "two"),
            ("cut", "old-plan", "b", "two"),
            ("cut", "plan", "new-member-chunk", "three"),
            ("cut", "plan", "a", "different"),
        ]:
            with self.assertRaises(ValueError):
                coverage.accept(*item)
        self.assertFalse(coverage.complete)


if __name__ == "__main__":
    unittest.main()
