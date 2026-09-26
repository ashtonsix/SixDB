#!/usr/bin/env python3
"""Independent arithmetic and finite-resource outcome checks for transport study."""
from itertools import product
from math import isclose
import unittest

from transport_study import analytic, framing, ideal_block, loss_distribution, run


class TransportChecks(unittest.TestCase):
    def test_boundary_wire_accounting_and_single_message_ledger(self):
        self.assertEqual(framing(1304, 1400), dict(packets=1, wire_bytes=1400))
        self.assertEqual(framing(1305, 1400), dict(packets=2, wire_bytes=1481))
        for size in (0, 64, 1304, 1305, 65536):
            for mtu in (512, 1400, 9000):
                result = run(dict(count=1, size=size, rate=100, mtu=mtu))
                self.assertEqual(result["completed"], 1)
                expected = framing(size, mtu)["wire_bytes"] + framing(24, mtu)["wire_bytes"]
                self.assertAlmostEqual(result["counters"]["wire_bytes"], expected, places=6)
                self.assertEqual(result["overflow_events"], 0)
                self.assertEqual(result["unacked_transfers"], 0)
                self.assertEqual(result["resource_outstanding_bytes"], 0)

    def test_markov_pmf_against_independent_enumeration(self):
        for n in range(1, 9):
            for p, stay in ((.05, .8), (.2, .8), (.2, .2)):
                expected = [0.0] * (n + 1)
                enter = p * (1 - stay) / (1 - p)
                for bits in product((0, 1), repeat=n):
                    probability = p if bits[0] else 1 - p
                    for old, new in zip(bits, bits[1:]):
                        q = stay if old else enter
                        probability *= q if new else 1 - q
                    expected[sum(bits)] += probability
                actual = loss_distribution(n, p, stay_bad=stay)
                for a, e in zip(actual, expected):
                    self.assertAlmostEqual(a, e, places=13)
                self.assertAlmostEqual(sum(actual), 1, places=13)
                self.assertAlmostEqual(sum(k * q for k, q in enumerate(actual)), n * p, places=13)

    def test_iid_success_and_whole_cut_counterexample(self):
        bare = ideal_block(p=.05)
        self.assertAlmostEqual(bare["success_first_attempt"], .95 ** 8)
        coded = ideal_block(parity=2, p=.05)
        self.assertGreater(coded["success_first_attempt"], .98)
        for parity in (0, 1, 2, 4):
            cut = ideal_block(parity=parity, p=.05, mode="common_cut")
            self.assertAlmostEqual(cut["success_first_attempt"], .95)
            self.assertAlmostEqual(cut["expected_wire_bytes"] / (8 + parity),
                                   ideal_block(p=.05, mode="common_cut")["expected_wire_bytes"] / 8)

    def test_retry_failure_is_not_hiding_offered_work(self):
        config = dict(count=4, size=4096, rate=100, mtu=1400, loss=1)
        result = run(config)
        self.assertEqual(result["offered"], 4)
        self.assertEqual(result["completed"], 0)
        self.assertEqual(result["unfinished"], 4)
        self.assertEqual(result["late_or_unfinished"], 4)
        self.assertEqual(result["counters"]["retry_messages"], 8)
        self.assertEqual(result["counters"]["retry_exhausted"], 4)
        self.assertAlmostEqual(result["counters"]["wire_bytes"],
                               12 * framing(4096, 1400)["wire_bytes"], places=6)
        self.assertEqual(result["pending_reassembly"], 0)

    def test_lost_ack_still_counts_logical_completion_and_duplicates(self):
        result = run(dict(count=4, size=4096, rate=100, mtu=1400, ack_loss=1))
        self.assertEqual(result["completed"], 4)
        self.assertEqual(result["unfinished"], 0)
        self.assertEqual(result["unacked_transfers"], 4)
        self.assertEqual(result["counters"]["duplicate_messages"], 8)
        self.assertEqual(result["counters"]["retry_exhausted"], 4)

    def test_finite_resources_and_parity_overload(self):
        result = run(dict(count=60, size=65536, rate=8000, mtu=512,
                          cpu_packet_us=4, queue_bytes=131072))
        self.assertEqual(result["completed"] + result["unfinished"], result["offered"])
        self.assertLessEqual(result["resource_peak_bytes"], 131072)
        self.assertGreater(result["overflow_events"], 0)
        counterexample = analytic()["parity_congestion_counterexample"]
        self.assertLess(counterexample["uncoded_utilization"], 1)
        self.assertGreater(counterexample["coded_utilization"], 1)
        self.assertTrue(isclose(counterexample["fill_time_us"], 3355.4432))

    def test_reproducibility(self):
        config = dict(count=20, size=4096, rate=2000, mtu=1400, loss=.02, seed=17)
        self.assertEqual(run(config), run(config))


if __name__ == "__main__":
    unittest.main()
