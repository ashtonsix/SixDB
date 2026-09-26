#!/usr/bin/env python3
"""Application outcomes, progress limits and representation checks."""

from itertools import permutations
import unittest

from dynamic_sql import run_probes


class DynamicSQLChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.result = run_probes()
        cls.cases = {case["name"]: case for case in cls.result["scenarios"]}

    def test_every_application_history_replays_exact_reads_effects_and_results(self):
        self.assertEqual(len(self.cases), 13)
        self.assertTrue(all(case["serial_check"] for case in self.cases.values()))

    def test_index_ownership_changes_discovery_failure_not_sql_outcome(self):
        maintained = self.cases["merge_materialized_index"]
        derived = self.cases["merge_derived_index"]
        self.assertEqual(maintained["counts"]["footprint_failures"], 1)
        self.assertEqual(derived["counts"].get("footprint_failures", 0), 0)
        row_fields = {key: value for key, value in maintained["final"].items() if "/customer/" in key}
        self.assertEqual(row_fields, derived["final"])
        self.assertEqual(maintained["committed"][-1]["result"]["status"], "duplicate_or_old")
        self.assertEqual(maintained["committed"][-1]["writes"], {})

    def test_unique_violation_is_a_committed_business_result(self):
        case = self.cases["unique_business_rejection_after_ordered_read"]
        self.assertEqual(case["counts"]["business_rejections"], 1)
        self.assertEqual(case["counts"].get("footprint_failures", 0), 0)
        self.assertEqual(case["committed"][-1]["writes"], {})
        self.assertEqual(case["committed"][-1]["result"],
                         {"status": "unique_rejection", "email": 2, "owner": 1})

    def test_late_feed_replay_does_not_resurrect_deleted_row(self):
        case = self.cases["upsert_delete_reinsert_and_late_feed_replay"]
        replay = next(record for record in case["committed"] if record["logical"] == "late-replay")
        self.assertEqual(replay["writes"], {})
        self.assertEqual(replay["result"]["status"], "duplicate_or_old")
        self.assertEqual(case["final"]["eu/customer/2/seq"], 6)
        self.assertEqual(case["final"]["eu/customer/2/email"], 3)
        self.assertIsNone(case["final"]["us/email/2"])
        self.assertEqual(case["final"]["us/email/3"], 2)

    def test_growing_cascade_can_exhaust_finite_discovery_budget(self):
        case = self.cases["cascade_growing_children"]
        self.assertEqual(case["finite_four_attempt_outcome"], "failure")
        self.assertEqual(case["counts"]["footprint_failures"], 4)
        self.assertEqual(case["before_quiet"]["eu/parent/1"], 1)
        self.assertFalse(case["quiet_round_is_progress_guarantee"])
        returned = case["committed"][-1]["result"]
        self.assertEqual(returned["children"], [f"us/child/{number}" for number in (1, 3, 4, 5, 6)])
        self.assertEqual(case["final"]["us/child/2"], 2)

    def test_complete_envelopes_avoid_footprint_failure_but_exclude_inside_writers(self):
        cascade = self.cases["cascade_declared_family_domain"]
        copy = self.cases["insert_select_declared_destination_envelope"]
        for case in (cascade, copy):
            self.assertEqual(case["counts"].get("footprint_failures", 0), 0)
            self.assertEqual(case["counts"].get("discovery_passes", 0), 0)
            self.assertGreater(case["counts"]["blocked_admission_requests"], 0)
        self.assertTrue(cascade["nonmatching_inside_target_writer_excluded"])
        self.assertTrue(cascade["other_tenant_completed_during_delete"])
        self.assertTrue(copy["source_writer_completed_during_computation"])
        self.assertTrue(copy["outside_target_writer_completed"])
        self.assertEqual(copy["final"]["us/source/b"], 30)
        self.assertEqual(copy["final"]["us/dest/b"], 20)

    def test_row_owned_index_visibility_includes_absence_and_old_versions(self):
        case = self.cases["row_owned_versioned_index_outcome"]
        self.assertEqual(case["discovered_index_gate_requests"], 0)
        self.assertEqual(case["admitted_logical_keys_for_A"], ["eu/row/a"])
        self.assertTrue(case["matching_lookup_waited"])
        self.assertTrue(case["nonmatching_lookup_waited"])
        self.assertTrue(case["unrelated_primary_writer_completed_during_partial_install"])
        self.assertEqual(case["final"]["eu/row/b"], 11)
        self.assertEqual(case["returned"][0]["row_image"]["eu/row/b"], 10)
        self.assertEqual(case["returned"][0]["matches"], ["eu/row/a"])
        self.assertEqual(case["returned"][1]["matches"], [])

    def test_stale_root_cannot_implement_preserving_merge_in_any_serial_order(self):
        # Independent tiny reference operations: neither touches the other's
        # field. Both serial orders retain the correction; stale replacement
        # does not become valid merely because the root update is atomic.
        outcomes = []
        for order in permutations(("merge", "correction")):
            state = {"a": 0, "correction": 0}
            for operation in order:
                state["a" if operation == "merge" else "correction"] = 10 if operation == "merge" else 7
            outcomes.append(state)
        self.assertNotIn({"a": 10, "correction": 0}, outcomes)
        case = self.cases["generation_objects_preserve_correction_and_atomic_visibility"]
        self.assertIn(case["final_images"]["eu"], outcomes)
        self.assertTrue(case["partial_publication_reader_waited"])
        self.assertTrue(case["same_partition_point_writer_excluded"])

    def test_authoritative_snapshot_job_is_an_explicit_different_contract(self):
        case = self.cases["explicit_snapshot_job_authoritative_replacement"]
        self.assertEqual(case["final"]["eu/source/a"], 99)
        image = case["generations"][case["final"]["eu/dest_root"]]
        self.assertEqual(image, {"a": 10})
        self.assertEqual(case["committed"][-1]["result"]["status"], "authoritative_replacement")

    def test_ordered_feed_activation_deduplicates_without_split_checkpoint_visibility(self):
        case = self.cases["ordered_feed_idempotence_and_atomic_activation"]
        self.assertEqual(case["duplicate_events"], 1)
        self.assertEqual(case["events_before_cut_ignored"], 1)
        self.assertTrue(case["partial_activation_reader_waited"])
        self.assertEqual(case["published_cut"], 104)
        self.assertEqual(case["source_already_at"], 105)
        self.assertEqual(case["committed"][-1]["writes"], {})
        self.assertEqual(case["committed"][-1]["result"]["status"], "duplicate_activation")
        images = case["generations"]
        self.assertEqual(images[case["final"]["eu/dest_root"]], {"a": 12})
        self.assertEqual(images[case["final"]["us/dest_root"]], {"d": 4})

    def test_exact_replay(self):
        self.assertEqual(self.result, run_probes())


if __name__ == "__main__":
    unittest.main()
