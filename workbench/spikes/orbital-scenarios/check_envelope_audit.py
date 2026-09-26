#!/usr/bin/env python3
"""Check adversarial obligations, including deliberately unsafe controls."""

import unittest

from envelope_audit import (CurrentGroupFIFO, LogicalIndex, admission_state_search,
                            fixed_source_histories, gate_histories, index_histories,
                            max_predicate_comparison, multiple_bound_histories,
                            own_constraint_histories, retention_histories)


class EnvelopeAuditTests(unittest.TestCase):
    def test_missing_authority_and_missing_bound_are_detected(self):
        histories = {h["name"]: h for h in index_histories()}
        for name in ("unknown_destination_omitted", "predicate_read_bound_omitted"):
            self.assertFalse(histories[name]["serial_valid"])
            self.assertEqual(histories[name]["replay_errors"][0]["expected"], ["row/a"])
        for name in ("unknown_destination_covered", "predicate_read_bound_honored"):
            self.assertTrue(histories[name]["serial_valid"])
        self.assertEqual(histories["predicate_read_bound_honored"]["writer_position"], 21)

    def test_unknown_effects_change_read_wait_and_metadata_not_exclusive_rows(self):
        histories = {h["name"]: h for h in index_histories()}
        exact, unknown = [histories["predicate_locality_" + kind] for kind in ("exact", "unknown")]
        self.assertEqual(exact["exclusive_row_count"], unknown["exclusive_row_count"])
        self.assertEqual((exact["predicate_authority_count"], unknown["predicate_authority_count"]), (1, 2))
        self.assertFalse(exact["unnecessary_query_wait"])
        self.assertTrue(unknown["unnecessary_query_wait"])
        for history in (exact, unknown):
            resolutions = [event["owner"] for event in history["events"] if event["event"] == "resolve"]
            self.assertEqual(resolutions, ["local_writer", "WAN"])
            self.assertTrue(history["serial_valid"])

    def test_serial_history_is_not_a_unique_constraint_oracle(self):
        missing, visible = own_constraint_histories()
        self.assertTrue(missing["serial_valid"])
        self.assertFalse(missing["constraint_valid"])
        self.assertTrue(visible["constraint_valid"])
        self.assertFalse(visible["accepted"])

    def test_dynamic_unique_check_waits_for_lower_position(self):
        histories = [h for h in index_histories() if h["name"].startswith("unique_")]
        self.assertEqual(len(histories), 4)
        for history in histories:
            waits = [event for event in history["events"] if event["event"] == "wait"]
            self.assertEqual(len(waits), 1)
            self.assertEqual(waits[0]["owner"], history["business_rejected"])
            self.assertEqual(waits[0]["blockers"], [history["accepted"]])

    def test_envelope_rejects_an_uncovered_new_identifier(self):
        db = LogicalIndex({"row/a": 0})
        db.begin("T", ["row/a"], 10)
        with self.assertRaisesRegex(AssertionError, "escaped"):
            db.finish("T", {"new/identifier": 7})
        self.assertIn("T", db.pending)
        db.finish("T", {})

    def test_local_fifo_needs_no_global_age_and_keeps_disjoint_work_enabled(self):
        gates = CurrentGroupFIFO({})
        self.assertTrue(gates.request("holder", ["eu/x"]))
        self.assertFalse(gates.request("z-first", ["eu/x", "eu/y"]))
        self.assertFalse(gates.request("a-later", ["eu/y"]))
        self.assertTrue(gates.request("disjoint", ["eu/z"]))
        gates.release_all("holder")
        self.assertFalse(gates.request("a-later", ["eu/y"]))
        self.assertTrue(gates.request("z-first", ["eu/x", "eu/y"]))
        gates.release_all("z-first")
        self.assertTrue(gates.request("a-later", ["eu/y"]))

    def test_future_fairness_claim_is_an_intentional_deadlock_control(self):
        histories = {h["name"]: h for h in gate_histories()}
        self.assertTrue(histories["future_fairness_claim_unsafe"]["deadlocked_before_execution"])
        self.assertFalse(histories["future_fairness_claim_absent"]["deadlocked_before_execution"])
        self.assertTrue(histories["range_owns_absent_identifier"]["outside_writer_passed"])

    def test_finite_admission_has_no_incomplete_terminal_state(self):
        searches = admission_state_search()
        self.assertEqual(len(searches), 3)
        self.assertTrue(all(search["incomplete_terminal_states"] == 0 for search in searches))
        self.assertTrue(all(search["local_fifo_searches"] == 1 for search in searches))

    def test_multiple_bounds_and_late_source_preserve_one_snapshot(self):
        histories = multiple_bound_histories()
        self.assertEqual(len(histories), 24)
        self.assertEqual(sum(h["reader_before_writer"] for h in histories), 18)
        earlier, later = fixed_source_histories()
        self.assertTrue(earlier["target_waited"])
        self.assertFalse(later["target_waited"])
        self.assertEqual((earlier["target_result"], later["target_result"]), (9, 2))

    def test_maximum_predicate_precision_and_distributed_negative_control(self):
        comparison = max_predicate_comparison()
        self.assertEqual(comparison["row_transitions"], 576)
        self.assertGreater(comparison["permitted_preserving_max"], 0)
        self.assertGreater(comparison["blocked_transitions"], 0)
        self.assertEqual(comparison["unprotected_distributed_gather_valid_serial_orders"], [])

    def test_expired_source_cannot_silently_become_a_latest_read(self):
        history = retention_histories()
        self.assertNotEqual(history["historical_value"], history["incorrect_latest_fallback"])


if __name__ == "__main__":
    unittest.main()
