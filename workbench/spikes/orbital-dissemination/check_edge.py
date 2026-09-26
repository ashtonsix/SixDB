#!/usr/bin/env python3
import unittest

from edge_study import (RECIPIENTS, interconnect_break_even, retrieval, route, run, tariff)


class EdgeChecks(unittest.TestCase):
    def test_directed_prices(self):
        self.assertGreater(tariff("a0x", "edge"), tariff("edge", "a0x"))

    def test_tree_coverage_without_duplicate_or_cycle(self):
        for origin in ("edge", "a0x"):
            for policy in ("direct", "regional", "edge_relay", "cheap_relay"):
                plan = route(origin, policy)
                seen, todo = {origin}, [origin]
                while todo:
                    for child in plan.get(todo.pop(), ()):
                        self.assertNotIn(child, seen)
                        seen.add(child)
                        todo.append(child)
                self.assertTrue(set(RECIPIENTS) <= seen)

    def test_small_healthy_all_policies_and_origins(self):
        for origin in ("edge", "a0x"):
            for policy in ("direct", "regional", "edge_relay", "cheap_relay"):
                r = run(dict(origin=origin, policy=policy, count=3, rate=100, size=256))
                self.assertEqual(r["completed"], 3)
                self.assertEqual(r["completed_recipient_pairs"], 24)

    def test_origin_and_route_change_economic_result(self):
        def price(origin, policy):
            return run(dict(origin=origin, policy=policy, count=3, rate=100))["counters"]["price"]
        self.assertLess(price("a0x", "edge_relay"), price("a0x", "direct"))
        self.assertLess(price("edge", "edge_relay"), price("a0x", "edge_relay"))

    def test_hashes_and_responses_are_actual_extra_work(self):
        base = dict(origin="a0x", policy="edge_relay", count=3, rate=100)
        r, h, reply = run(base), run(dict(base, direct_hashes=True)), run(dict(base, response_size=65536))
        self.assertEqual(h["completed"], 3)
        self.assertEqual(reply["completed"], 3)
        self.assertGreater(h["counters"]["wire_bytes"], r["counters"]["wire_bytes"])
        self.assertGreater(reply["counters"]["price"], r["counters"]["price"])
        self.assertGreater(reply["p99_us"], r["p99_us"])

    def test_no_magic_origin_crash_recovery(self):
        r = run(dict(origin="edge", policy="edge_relay", count=20, rate=200,
                     faults=[dict(kind="crash", node="edge", at_us=5000, until_us=90000)]))
        self.assertGreater(r["unfinished"], 0)
        self.assertGreater(r["counters"].get("lost_packets", 0), 0)

    def test_break_even_and_misses(self):
        self.assertAlmostEqual(interconnect_break_even(200, .09, .02), 2857.1428571428573)
        self.assertIsNone(interconnect_break_even(200, .01, .02))
        self.assertLess(retrieval(128, 0, 4e-7, 0, 2e-8, .01)["saved"], 0)
        self.assertGreater(retrieval(128, .9, 4e-7, 0, 2e-8, .01)["saved"], 0)
        self.assertLess(retrieval(1048576, .9, 4e-7, 0, 2e-8, .01)["saved"], 0)


if __name__ == "__main__":
    unittest.main()
