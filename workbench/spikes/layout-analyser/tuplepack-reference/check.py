#!/usr/bin/env python3
"""Independent combinatorial, bit-set and arithmetic checks. No CPU benchmarks."""

import copy
import csv
from fractions import Fraction
import itertools
import math
import unittest

import analyse as a


DENSE = "b2-A0s0-B0s1-C1s0-D1s3"
CLUSTER = "b3-A0s0-B1s0-C0s1-D2s0"
WEIGHTS = {"schema_version": 1, "id": "check", "weights": {"R_AC": 1}}


def cost_document(rows):
    return {"schema_version": 1, "kind": "synthetic",
            "context": {"id": "check", "contract_id": a.CONTRACT,
                        "provenance": "authored arithmetic fixture, not measured",
                        "statistic": "synthetic numbers"},
            "measurements": rows}


def row(candidate, operation, recipe, run, prepare):
    return dict(candidate_id=candidate, operation_id=operation, recipe_id=recipe,
                run_ns=run, prepare_ns=prepare)


def independent_count(byte_count):
    # Choose which labelled codes belong in each byte. For k codes of total
    # width w, k! orders and stars-and-bars placements in the 8-w free bits.
    ways = {}
    for mask in range(16):
        selected = [width for i, width in enumerate((1, 7, 3, 5)) if mask & (1 << i)]
        k, width = len(selected), sum(selected)
        ways[mask] = math.factorial(k) * math.comb(8 - width + k, k) if width <= 8 else 0
    dp = {0: 1}
    for _ in range(byte_count):
        following = {}
        for assigned, count in dp.items():
            for subset, arrangements in ways.items():
                if not subset & assigned:
                    key = assigned | subset
                    following[key] = following.get(key, 0) + count * arrangements
        dp = following
    return dp[15]


class Enumeration(unittest.TestCase):
    def test_diverse_subset_and_tsv_roundtrip(self):
        subset = a.diverse_subset()
        self.assertEqual(len(subset), 28)
        self.assertEqual(subset, a.diverse_subset())
        self.assertEqual(len(set(subset)), 28)
        self.assertEqual(sum(c.startswith("b2-") for c in subset), 8)
        exported = list(csv.DictReader(a.tsv(subset).splitlines(), delimiter="\t"))
        self.assertEqual([r["id"] for r in exported], subset)
        for r in exported:
            layout = a.Layout(int(r["unit_bytes"]), tuple((int(r[f"{n}_offset"]), int(r[f"{n}_shift"])) for n in a.NAMES))
            self.assertEqual(layout.id, r["id"])
        self.assertFalse(a.catalogue(subset)["complete_exhaustive_universe"])
        self.assertEqual(len(a.diverse_subset(16)), 24)

    def test_complete_and_unique(self):
        candidates = a.layouts()
        self.assertEqual(len({layout.id for layout in candidates}), len(candidates))
        for size, expected in ((2, 8), (3, 2808)):
            actual = sum(layout.unit_bytes == size for layout in candidates)
            self.assertEqual(actual, expected)
            self.assertEqual(actual, independent_count(size))
        for layout in candidates:
            bits = []
            for width, (offset, shift) in zip((1, 7, 3, 5), layout.positions):
                self.assertTrue(0 <= offset < layout.unit_bytes)
                self.assertTrue(0 <= shift <= 8 - width)
                bits += [offset * 8 + shift + bit for bit in range(width)]
            self.assertEqual(len(set(bits)), 16)
        self.assertEqual(sum(a.describe(l)["unused_byte_count"] > 0 for l in candidates), 24)

    def test_physical_order_retained(self):
        universe = set(a.layouts())
        for layout in universe:
            reversed_bytes = a.Layout(layout.unit_bytes,
                                     tuple((layout.unit_bytes - 1 - b, s) for b, s in layout.positions))
            self.assertIn(reversed_bytes, universe)
            self.assertNotEqual(layout.id, reversed_bytes.id)

    def test_every_operation_against_bit_sets(self):
        for layout in a.layouts():
            for op, selected in a.MAPS.items():
                bits = {8 * layout.positions[c][0] + layout.positions[c][1] + i
                        for c in selected for i in range(a.WIDTHS[c])}
                masks = [sum(1 << bit for bit in range(8) if 8 * offset + bit in bits)
                         for offset in range(layout.unit_bytes)]
                touched = {bit // 8 for bit in bits}
                f = a.features(layout, op)
                self.assertEqual(f["selected_masks"], masks)
                self.assertEqual(f["selected_byte_count"], len(touched))
                self.assertEqual({offset + i for offset, size in f["selected_byte_spans"]
                                  for i in range(size)}, touched)
                if op.startswith("W"):
                    old = {offset for offset in touched
                           if any(8 * offset + bit not in bits for bit in range(8))}
                    self.assertEqual(set(f["preservation_byte_offsets"]), old)
                    self.assertEqual(f["preserve_masks"], [255 - mask for mask in masks])
                    # Independent per-bit writes, including arbitrary padding,
                    # agree with exported masks for six adversarial byte patterns.
                    for offset in touched:
                        selected_bits = [bit for bit in range(8) if 8 * offset + bit in bits]
                        for old_byte in (0, 1, 85, 170, 254, 255):
                            expected = old_byte
                            for bit in selected_bits:
                                expected = (expected & ~(1 << bit)) | (0xa6 & (1 << bit))
                            actual = (old_byte & f["preserve_masks"][offset]) | (0xa6 & masks[offset])
                            self.assertEqual(actual, expected)

    def test_order_and_preservation_witnesses(self):
        by_id = {layout.id: layout for layout in a.layouts()}
        self.assertNotEqual(a.features(by_id[DENSE], "R_0")["ordered_offsets"],
                            a.features(by_id[DENSE], "R_1")["ordered_offsets"])
        self.assertEqual(a.features(by_id[DENSE], "W_all")["preservation_byte_count"], 0)
        self.assertEqual(a.features(by_id[CLUSTER], "W_all")["preservation_byte_count"], 3)
        clustered = a.features(by_id[CLUSTER], "W_AC")
        self.assertEqual(clustered["selected_masks"], [15, 0, 0])
        self.assertEqual(clustered["preservation_bits_in_selected_bytes"], 4)


class Ranking(unittest.TestCase):
    def test_preparation_changes_winner_and_zero_weights_do_not_prepare(self):
        costs = cost_document([row(DENSE, "R_AC", "direct", 6, 0),
                               row(CLUSTER, "R_AC", "specialized", 1, 100)])
        workload = dict(WEIGHTS, weights={"R_AC": 3, "W_all": 0})
        low = a.rank(costs, workload, 1, [DENSE, CLUSTER])
        high = a.rank(costs, workload, 100, [DENSE, CLUSTER])
        self.assertEqual(low["ranked_plans"][0]["candidate_id"], DENSE)
        self.assertEqual(high["ranked_plans"][0]["candidate_id"], CLUSTER)
        self.assertEqual(high["ranked_plans"][0]["amortized_preparation_ns_per_invocation"], 1)
        self.assertEqual(high["heuristics"]["density_first"]["regret_ns"], 400)
        self.assertFalse(high["universe"]["complete_exhaustive_universe"])
        self.assertEqual(high["kind"], "synthetic")

    def test_full_universe_against_cartesian_recipe_oracle(self):
        rows, expected = [], {}
        weights = {"schema_version": 1, "id": "2:1", "weights": {"R_AC": 2, "W_AC": 1}}
        for i, layout in enumerate(a.layouts()):
            choices = []
            for j, op in enumerate(("R_AC", "W_AC")):
                options = ((1 + (i + j) % 11, (i % 7) * 3), (1 + (i + j) % 3, 200))
                choices.append(options)
                for recipe, (run, prepare) in enumerate(options):
                    rows.append(row(layout.id, op, str(recipe), run, prepare))
            expected[layout.id] = min(
                Fraction(100, 3) * (2 * read[0] + write[0]) + read[1] + write[1]
                for read, write in itertools.product(*choices))
        result = a.rank(cost_document(rows), weights, 100, top=2816)
        ordered = sorted(expected, key=lambda c: (expected[c], c))
        self.assertEqual([p["candidate_id"] for p in result["ranked_plans"]], ordered)
        self.assertEqual([p["total_ns"] for p in result["ranked_plans"]], [expected[c] for c in ordered])
        self.assertTrue(result["universe"]["complete_exhaustive_universe"])

    def test_heuristic_ties_and_zero_optimum(self):
        dense = [l.id for l in a.layouts() if l.unit_bytes == 2]
        costs = cost_document([row(c, "R_AC", "x", 0 if i else 10, 0) for i, c in enumerate(dense)])
        result = a.rank(costs, WEIGHTS, 1, dense)
        h = result["heuristics"]["density_first"]
        self.assertEqual(h["candidate_id"], min(dense))
        self.assertEqual(h["tied_candidate_count"], 8)
        self.assertEqual(h["regret_ns_range_over_structural_ties"], [0, 10])
        self.assertIsNone(h["relative_regret"])

    def test_reject_missing_duplicate_invalid_and_mismatched_costs(self):
        costs = cost_document([row(DENSE, "R_AC", "direct", 6, 0)])
        with self.assertRaisesRegex(ValueError, "missing"):
            a.rank(costs, WEIGHTS, 1)
        for bad in (-1, float("nan"), float("inf"), True, "1"):
            changed = copy.deepcopy(costs)
            changed["measurements"][0]["run_ns"] = bad
            with self.assertRaises(ValueError):
                a.rank(changed, WEIGHTS, 1, [DENSE])
        duplicate = copy.deepcopy(costs)
        duplicate["measurements"] *= 2
        with self.assertRaisesRegex(ValueError, "duplicate cost"):
            a.rank(duplicate, WEIGHTS, 1, [DENSE])
        changed = copy.deepcopy(costs)
        changed["context"]["contract_id"] = "other"
        with self.assertRaisesRegex(ValueError, "contract_id"):
            a.rank(changed, WEIGHTS, 1, [DENSE])
        with self.assertRaisesRegex(ValueError, "positive weight"):
            a.rank(costs, dict(WEIGHTS, weights={"R_AC": 0}), 1, [DENSE])


class Migration(unittest.TestCase):
    def fixture(self):
        costs = cost_document([row(DENSE, "R_AC", "resident", 6, 900),
                               row(CLUSTER, "R_AC", "direct", 4, 0),
                               row(CLUSTER, "R_AC", "prepared", 1, 100)])
        migration = {"schema_version": 1, "kind": "synthetic", "context_id": "check",
                     "provenance": "authored arithmetic", "scope": "one whole synthetic segment",
                     "current_candidate": DENSE, "current_recipes": {"R_AC": "resident"},
                     "migration_costs": [{"candidate_id": CLUSTER, "total_ns": 100}]}
        return costs, migration

    def test_break_even_changes_recipe_and_sinks_old_preparation(self):
        costs, migration = self.fixture()
        before = a.rank(costs, WEIGHTS, 40, [DENSE, CLUSTER], migration)["migration"]
        after = a.rank(costs, WEIGHTS, 41, [DENSE, CLUSTER], migration)["migration"]
        target = after["targets"][0]
        self.assertEqual(before["decision_at_horizon"], "stay")
        self.assertEqual(after["stay_total_ns"], 246)
        self.assertEqual(after["decision_at_horizon"], CLUSTER)
        self.assertEqual(target["first_strictly_better_integer_horizon"], 41)
        self.assertEqual(target["recipes_at_first_win"], {"R_AC": "prepared"})
        self.assertEqual(target["saving_vs_stay_ns"], 5)
        # Exhaustively check the threshold independently for this finite fixture.
        first = next(n for n in range(1, 1000) if 100 + min(4*n, n+100) < 6*n)
        self.assertEqual(first, target["first_strictly_better_integer_horizon"])

    def test_never_and_bad_metadata(self):
        costs, migration = self.fixture()
        for r in costs["measurements"][1:]:
            r["run_ns"] = 6
        result = a.rank(costs, WEIGHTS, 10000, [DENSE, CLUSTER], migration)["migration"]
        self.assertIsNone(result["targets"][0]["first_strictly_better_integer_horizon"])
        for key, bad in (("context_id", "other"), ("kind", "measured"), ("migration_costs", [])):
            changed = dict(migration, **{key: bad})
            with self.assertRaises(ValueError):
                a.rank(costs, WEIGHTS, 100, [DENSE, CLUSTER], changed)

    def test_many_small_break_even_cases_against_direct_search(self):
        for run1, prep1, run2, prep2, move in itertools.product((1, 3, 7), (0, 5), (2, 6), (0, 12), (0, 9)):
            costs = {(CLUSTER, "R_AC"): [a.Cost("x", Fraction(run1), Fraction(prep1)),
                                          a.Cost("y", Fraction(run2), Fraction(prep2))]}
            actual = a.first_migration_win(CLUSTER, costs, {"R_AC": Fraction(1)}, Fraction(6), Fraction(move))
            expected = next((n for n in range(1, 100) if move + min(run1*n+prep1, run2*n+prep2) < 6*n), None)
            self.assertEqual(actual, expected)


if __name__ == "__main__":
    unittest.main(verbosity=2)
