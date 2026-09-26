#!/usr/bin/env python3
"""Counterexamples for work shaping, credits and physical interference."""
import unittest

from mixed_study import run_mixed


class MixedCases(unittest.TestCase):
    def test_chunking_preserves_logical_work_and_read_coverage(self):
        large = run_mixed(dict(count=80, rate=20000))
        small = run_mixed(dict(count=80, rate=20000, chunk_bytes=8192))
        self.assertEqual(large["background_read"]["completed"], 4)
        self.assertEqual(small["background_read"]["completed"], 4)
        self.assertEqual(large["work"]["background_scan_bytes"], small["work"]["background_scan_bytes"])
        self.assertEqual(large["work"]["background_compute_input_bytes"], small["work"]["background_compute_input_bytes"])
        self.assertLess(small["foreground_effect"]["p99_us"], large["foreground_effect"]["p99_us"])

    def test_separate_core_pool_keeps_same_total_application_cpu_slots(self):
        result = run_mixed(dict(count=20, placement="cores"))
        self.assertIn("effect:background_core", result["resources"])
        self.assertNotIn("background:tx", result["resources"])
        self.assertEqual(result["foreground_effect"]["completed"], 20)

    def test_recomputation_is_charged(self):
        result = run_mixed(dict(count=40, compute_copies=2))
        self.assertEqual(result["work"]["background_compute_input_bytes"],
                         2 * result["logical_background_bytes"])

    def test_refused_reads_stay_in_offered_denominator(self):
        result = run_mixed(dict(count=80, background_credit_bytes=1))
        self.assertEqual(result["background_read"]["offered"], 4)
        self.assertEqual(result["background_read"]["refused"], 4)
        self.assertEqual(result["background_read"]["completed"], 0)
        self.assertEqual(result["background_read"]["deadline_fraction"], 0)

    def test_cancellation_does_not_refund_active_nonpreemptive_work(self):
        result = run_mixed(dict(count=40, cancel_after_us=50))
        self.assertEqual(result["work"]["background_cancelled"], 2)
        self.assertEqual(result["background_read"]["completed"], 0)
        self.assertGreater(result["work"]["background_computed_after_cancel_bytes"], 0)
        for item in result["cancellation"]:
            self.assertGreater(item["retired_us"], item["cancel_us"])
            self.assertGreater(item["reserved_query_bytes"], 0)
        self.assertEqual(result["application_bytes_at_drain"]["background"], 0)

    def test_foreground_effect_and_client_delivery_are_different_endpoints(self):
        result = run_mixed(dict(count=40, background_rate=0))
        self.assertEqual(result["foreground_effect"]["completed"], 40)
        self.assertEqual(result["client_response"]["completed"], 40)
        self.assertGreater(result["client_response"]["p50_us"], result["foreground_effect"]["p50_us"])

    def test_wire_attribution_includes_acknowledgements_and_retries(self):
        result = run_mixed(dict(count=80))
        totals = result["completed_tx_wire_bytes_by_class"]
        self.assertAlmostEqual(sum(totals.values()), result["counters"]["wire_bytes"])
        self.assertGreater(totals["background"], result["work"]["background_output_offered_bytes"])

    def test_reserving_control_credits_does_not_preempt_active_compute(self):
        plain = run_mixed(dict(count=80))
        reserved = run_mixed(dict(count=80, reserve_bytes=65536))
        self.assertAlmostEqual(plain["foreground_effect"]["p99_us"], reserved["foreground_effect"]["p99_us"])


if __name__ == "__main__":
    unittest.main()
