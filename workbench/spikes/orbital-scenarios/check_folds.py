#!/usr/bin/env python3
"""Check state and observable results across all schedules of small folds."""

import unittest

from folds import outcomes, probe


class FoldChecks(unittest.TestCase):
    def test_every_order_and_binary_grouping_agrees(self):
        result = probe()["completion_only"]
        self.assertEqual(result["schedules"], 1680)
        self.assertEqual(result["fixpoints"], 1)
        self.assertEqual(result["result"], ((("x", 22), ("y", 20)), ("a", "b", "c", "d", "e"), ()))

    def test_protected_inputs_defer_whole_transactions(self):
        result = probe()["protected_y"]
        self.assertEqual(result["schedules"], 2)
        self.assertEqual(result["fixpoints"], 1)
        self.assertEqual(result["result"], ((("x", 24), ("y", 20)), ("a", "e"), ("b", "c", "d")))

    def test_equal_object_state_is_insufficient_for_equal_fixpoints(self):
        result = probe()["return_intermediate_value"]
        self.assertEqual(result["fixpoints"], 6)
        self.assertEqual({example[0] for example in result["examples"]}, {6})

    def test_no_eligible_work_preserves_state_and_reports_deferral(self):
        self.assertEqual(list(outcomes({"x": 7}, [("t", {"x": 2})], {"x"})),
                         [((("x", 7),), (), ("t",))])


if __name__ == "__main__":
    unittest.main()
