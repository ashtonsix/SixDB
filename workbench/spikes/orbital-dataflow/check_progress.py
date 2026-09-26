#!/usr/bin/env python3
"""Independent bag/least-fixed-point oracles and selected interrupted histories."""

from __future__ import annotations

from collections import Counter
import itertools
import random
import unittest

from progress_probe import (EpochInput, Packet, Progress, Retention, Store, add_counts,
                            coverage_and_progress, delta_join, difference, epoch_input,
                            frontier, join_histories, leq, naive_support_delete,
                            recovery_histories, recursive_bag_nontermination,
                            recursive_histories, retention_history, split_checkpoint_histories,
                            update_reachability, weighted_join)


def full_bag_join(left: Counter, right: Counter) -> Counter:
    # Expand positive base bags, then enumerate all pairs. No delta/index code.
    left_rows = [row for row, count in left.items() for _ in range(count)]
    right_rows = [row for row, count in right.items() for _ in range(count)]
    return Counter((key, lv, rv) for key, lv in left_rows
                   for other_key, rv in right_rows if key == other_key)


def full_absent(left: Counter, right: Counter) -> Counter:
    output = Counter()
    for row, count in left.items():
        if not any(other[0] == row[0] and multiplicity > 0
                   for other, multiplicity in right.items()):
            output[row] += count
    return +output


def least_fixed_point(edges: set[tuple[int, int]]) -> set[int]:
    # Repeated full relation scan, independent from adjacency/worklist maintenance.
    result = {0}
    while True:
        expanded = result | {target for source, target in edges if source in result}
        if expanded == result:
            return result
        result = expanded


class JoinChecks(unittest.TestCase):
    def test_all_small_signed_join_transitions(self):
        rows = [("k", "x"), ("k", "y")]
        bags = [Counter({row: count for row, count in zip(rows, counts) if count})
                for counts in itertools.product(range(2), repeat=2)]
        for left, right, next_left, next_right in itertools.product(bags, repeat=4):
            dl, dr = difference(next_left, left), difference(next_right, right)
            expected = full_bag_join(next_left, next_right)
            observed = add_counts(full_bag_join(left, right), delta_join(left, right, dl, dr))
            self.assertEqual(observed, expected)

    def test_random_weighted_join_transitions(self):
        rng = random.Random(17)
        rows = [(key, value) for key in "abc" for value in "pq"]
        for _ in range(1000):
            left, right, nl, nr = [Counter({row: rng.randrange(3) for row in rows}) for _ in range(4)]
            observed = add_counts(full_bag_join(left, right),
                                  delta_join(left, right, difference(nl, left), difference(nr, right)))
            self.assertEqual(observed, full_bag_join(nl, nr))

    def test_cross_term_cannot_be_omitted_or_counted_twice(self):
        dl, dr = Counter({("a", "left"): 1}), Counter({("a", "right"): 1})
        missing = add_counts(weighted_join(dl, Counter()), weighted_join(Counter(), dr))
        twice = add_counts(weighted_join(dl, dr), weighted_join(dl, dr))
        expected = full_bag_join(dl, dr)
        self.assertNotEqual(missing, expected)
        self.assertNotEqual(twice, expected)
        self.assertEqual(sum(expected.values()), 1)

    def test_incremental_store_matches_full_relations_with_deletes(self):
        rng, store = random.Random(31), Store()
        rows = [(key, value) for key in "ab" for value in "xy"]
        for epoch in range(1, 501):
            left, right = [Counter({row: rng.randrange(3) for row in rows}) for _ in range(2)]
            inputs = epoch_input(epoch, difference(left, store.left), difference(right, store.right))
            candidate = store.prepare(inputs)
            store.accept(candidate, checks_complete=True, authorised=True)
            self.assertEqual(store.joined, full_bag_join(left, right))
            self.assertEqual(store.absent, full_absent(left, right))

    def test_late_correction_retracts_absence_at_new_cut(self):
        history = join_histories()
        self.assertEqual(history[1]["unmatched"], [{"row": ["b", "order-b/event-9"], "weight": 1}])
        self.assertEqual(history[2]["unmatched_delta"], [{"row": ["b", "order-b/event-9"], "weight": -1}])
        self.assertEqual(history[3]["unmatched"], [{"row": ["a", "order-a"], "weight": 1}])
        self.assertEqual(history[-1]["joined"], [])

    def test_invalid_negative_base_multiplicity_rejected(self):
        with self.assertRaises(ValueError):
            Store().prepare(epoch_input(1, Counter({("a", "absent"): -1}), Counter()))


class CoverageChecks(unittest.TestCase):
    def test_24_packet_and_seal_orders_with_replay(self):
        left = Packet("L", 1, ((("a", "left"), 1),))
        right = Packet("R", 1, ((("a", "right"), 1),))
        for order in itertools.permutations(("left", "right", "seal_left", "seal_right")):
            inputs = EpochInput(1, {"L": "L", "R": "R"})
            for index, action in enumerate(order):
                if action == "left":
                    inputs.receive(left)
                    inputs.receive(left)
                elif action == "right":
                    inputs.receive(right)
                    inputs.receive(right)
                else:
                    inputs.seal("L" if action == "seal_left" else "R", 1)
                self.assertEqual(inputs.complete(), index == 3)
            candidate = Store().prepare(inputs)
            self.assertEqual(candidate.joined, Counter({("a", "left", "right"): 1}))
            self.assertEqual(inputs.duplicates, 2)

    def test_highest_sequence_and_empty_queue_do_not_close_coverage(self):
        stages = coverage_and_progress()["coverage"]
        self.assertEqual([stage["complete"] for stage in stages], [False, False, True])
        self.assertEqual(stages[-1]["unmatched"], [])

    def test_closed_epoch_cannot_silently_accept_late_input(self):
        inputs = EpochInput(1, {"L": "L", "R": "R"})
        inputs.seal("L", 0)
        inputs.seal("R", 0)
        with self.assertRaises(ValueError):
            inputs.receive(Packet("R", 1, ((("a", "late"), 1),)))
        with self.assertRaises(ValueError):
            inputs.seal("R", 1)

    def test_same_identity_different_contents_or_unplanned_lane_rejected(self):
        inputs = EpochInput(1, {"L": "L"})
        inputs.receive(Packet("L", 1, ((("a", "one"), 1),)))
        with self.assertRaises(ValueError):
            inputs.receive(Packet("L", 1, ((("a", "two"), 1),)))
        with self.assertRaises(ValueError):
            inputs.receive(Packet("unknown", 1, ((("a", "one"), 1),)))

    def test_dataflow_completion_does_not_grant_publication(self):
        gates = coverage_and_progress()["publication_gates"]
        self.assertEqual([gate["published"] for gate in gates], [False, False, False, True])

    def test_completed_epoch_does_not_retire_future_join_state(self):
        case = coverage_and_progress()["arrangement_eviction_negative_control"]
        self.assertEqual(case["correct_later_join"],
                         [{"row": ["a", "old-order", "new-payment"], "weight": 1}])
        self.assertEqual(case["later_join_after_frontier_based_eviction"], [])


class ProgressChecks(unittest.TestCase):
    def test_operator_capability_blocks_empty_queue_completion(self):
        states = coverage_and_progress()["loop_progress"]
        self.assertEqual([state["epoch_complete"] for state in states], [False, False, True])

    def test_frontier_retains_incomparable_times(self):
        self.assertEqual(frontier(((0, 2), (1, 0), (1, 2), (2, 2))), ((0, 2), (1, 0)))
        case = coverage_and_progress()["partial_order"]
        self.assertFalse(case["complete"])
        self.assertTrue(case["unsafe_lexicographic_min_complete"])

    def test_frontier_matches_all_times_for_every_small_point(self):
        points = list(itertools.product(range(3), repeat=2))
        for subset in itertools.combinations(points, 4):
            reduced = frontier(subset)
            for point in points:
                self.assertEqual(any(leq(time, point) for time in subset),
                                 any(leq(time, point) for time in reduced))

    def test_loop_iteration_can_finish_while_outer_epoch_is_incomplete(self):
        progress = Progress()
        progress.hold("next", "message", (3, 4))
        self.assertTrue(progress.complete_at((3, 3)))
        self.assertFalse(progress.outer_complete(3))
        with self.assertRaises(ValueError):
            progress.transfer("next", (("backwards", "message", (3, 2)),))

    def test_new_source_cannot_be_added_after_observing_closed_coverage(self):
        progress = Progress()
        self.assertTrue(progress.outer_complete(4))
        with self.assertRaises(ValueError):
            progress.hold("late-source", "operator", (1, 0))


class RecursiveChecks(unittest.TestCase):
    def test_local_counts_preserve_false_cyclic_support(self):
        edges = {(0, 1), (1, 2), (2, 1)}
        old = least_fixed_point(edges)
        self.assertEqual(naive_support_delete(edges, old, (0, 1)), {0, 1, 2})
        self.assertEqual(least_fixed_point(edges - {(0, 1)}), {0})
        self.assertEqual(update_reachability(edges, old, set(), {(0, 1)}).reachable, {0})

    def test_384_single_edge_changes_against_least_fixed_point(self):
        # Every directed graph on three nodes without self edges: exhaust
        # changes to each of the six modeled edges.
        possible = [(source, target) for source in range(3) for target in range(3) if source != target]
        for mask in range(1 << len(possible)):
            edges = {edge for index, edge in enumerate(possible) if mask & (1 << index)}
            old = least_fixed_point(edges)
            for edge in possible:
                added, removed = ({edge}, set()) if edge not in edges else (set(), {edge})
                result = update_reachability(edges, old, added, removed)
                self.assertEqual(result.reachable, least_fixed_point((edges - removed) | added))

    def test_random_batched_changes_with_self_loops(self):
        rng, edges = random.Random(53), set()
        possible = {(source, target) for source in range(12) for target in range(12)}
        state = {0}
        for _ in range(2000):
            removed = set(rng.sample(sorted(edges), min(len(edges), rng.randrange(5))))
            added = set(rng.sample(sorted(possible - edges), min(len(possible - edges), rng.randrange(5))))
            result = update_reachability(edges, state, added, removed)
            edges = (edges - removed) | added
            self.assertEqual(result.reachable, least_fixed_point(edges))
            state = result.reachable

    def test_partial_overdelete_cannot_be_published_as_completed_result(self):
        edges = {(0, 1), (0, 2), (1, 3), (2, 3), (3, 4)}
        old = least_fixed_point(edges)
        result = update_reachability(edges, old, set(), {(1, 3)})
        expected = least_fixed_point(edges - {(1, 3)})
        self.assertNotEqual(result.after_overdelete, expected)
        self.assertEqual(result.reachable, expected)
        # Crash before acceptance leaves old durable state; recomputation from
        # the same retained inputs produces the same candidate without exposure
        # of the private overdelete phase.
        restarted = update_reachability(edges, old, set(), {(1, 3)})
        self.assertEqual(restarted.reachable, expected)

    def test_incremental_wins_locally_but_loses_on_bridge_cut(self):
        cases = {row["case"]: row for row in recursive_histories()}
        leaf = cases["local leaf deletion"]
        bridge = cases["bridge deletion discards almost everything"]
        redundant = cases["redundant path deletion rederives a long suffix"]
        self.assertLess(leaf["incremental_edge_visits"], leaf["full_edge_visits"])
        self.assertGreater(bridge["incremental_edge_visits"], bridge["full_edge_visits"])
        self.assertGreater(redundant["incremental_edge_visits"], redundant["full_edge_visits"])

    def test_bag_recursion_round_limit_is_incomplete_not_a_fixpoint(self):
        case = recursive_bag_nontermination()
        self.assertFalse(case["completed"])
        self.assertEqual(case["remaining_work"], 1)
        self.assertEqual(case["iterations"][-1]["total_walks"], 9)
        self.assertEqual(case["set_reachability_nodes"], 3)


class RecoveryChecks(unittest.TestCase):
    def test_split_checkpoint_orders_duplicate_or_omit_output(self):
        emit_first, offset_first, _ = split_checkpoint_histories()
        self.assertEqual(emit_first["sink_effects_without_dedup"], 2)
        self.assertEqual(offset_first["sink_effects_without_dedup"], 0)
        self.assertEqual(emit_first["recovered_cut"], offset_first["recovered_cut"])

    def test_external_uncertainty_survives_an_identical_local_observation(self):
        histories = split_checkpoint_histories()[-1]["indistinguishable_histories"]
        self.assertEqual(histories[0]["local_observation"], histories[1]["local_observation"])
        self.assertEqual([history["sink_effects_before_retry"] for history in histories], [0, 1])
        self.assertEqual([history["sink_effects_after_blind_retry"] for history in histories], [1, 2])

    def test_four_crash_boundaries_preserve_exactly_one_internal_transition(self):
        for case in recovery_histories()[:4]:
            self.assertEqual(case["accepted_epoch"], 1)
            self.assertEqual(case["retained_output_records"], 1)
            self.assertEqual(case["sink_effects_with_sink_atomic_dedup"], 1)
            self.assertEqual(case["joined"], [{"row": ["a", "order", "payment"], "weight": 1}])

    def test_retry_returns_old_accepted_transition_and_rejects_conflicting_candidate(self):
        store = Store()
        original = store.prepare(epoch_input(1, Counter({("a", "order"): 1}), Counter()))
        competing = store.prepare(epoch_input(1, Counter({("b", "order"): 1}), Counter()))
        self.assertTrue(store.accept(original, checks_complete=True, authorised=True))
        self.assertFalse(store.accept(original, checks_complete=True, authorised=True))
        with self.assertRaises(ValueError):
            store.accept(competing, checks_complete=True, authorised=True)
        self.assertEqual(store.left, Counter({("a", "order"): 1}))

    def test_cancellation_and_checkpoint_release_only_their_own_pins(self):
        history = retention_history()
        self.assertTrue(history["over_capacity_offer_refused"])
        self.assertEqual([state["retained_bytes"] for state in history["states"]],
                         [5504, 5504, 5248, 4224, 128, 0])
        after_cancel = history["states"][1]["objects"]
        self.assertEqual(after_cancel["result@1"]["owners"], ["worker"])
        self.assertIn("committed-intent@1", after_cancel)

    def test_refused_retention_offer_neither_evicts_nor_changes_other_owners(self):
        retention = Retention(8)
        self.assertTrue(retention.add("old", 8, {"reader", "replay"}))
        before = retention.describe()
        self.assertFalse(retention.add("new", 1, {"writer"}))
        self.assertEqual(retention.describe(), before)
        retention.release("reader")
        self.assertEqual(retention.size(), 8)
        retention.release("replay")
        self.assertEqual(retention.size(), 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
