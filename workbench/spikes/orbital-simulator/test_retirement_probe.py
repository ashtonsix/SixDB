import unittest

from retirement_probe import BASE, Case, audit, borrow_case, run_case


class RetirementProbeTests(unittest.TestCase):
    def test_old_context_and_distinct_replay_root_survive_reclamation_and_reset(self):
        for root, version in (("reader", 2), ("replay", 4)):
            for seed in (1, 7, 19):
                with self.subTest(root=root, seed=seed):
                    row, world = run_case(Case(root=root), seed)
                    self.assertEqual(row["errors"], [])
                    self.assertEqual(row["coverage"]["missing"], [])
                    old = next(read for read in row["reads"] if read["root"] == f"roots/{root}")
                    self.assertEqual(old, dict(root=f"roots/{root}", version=version, incarnation=2))
                    self.assertEqual(row["reads"][-1], dict(root="head", version=4, incarnation=3))
                    self.assertEqual(row["report"]["counts"]["checkpoint_read_complete"], 1)
                    self.assertEqual(row["inventory"], ["data/checkpoint/4", "data/codec/raw-v1", "head"])
                    self.assertGreater(row["reclaimed_bytes"], len(BASE))
                    self.assertLessEqual(row["report"]["hosts"]["storage"]["memory_peak"], 8192)
                    reset = next(event["id"] for event in world.trace if event["kind"] == "power_loss")
                    accesses = [event for event in world.trace if event["kind"] == "delayed_first_access"]
                    self.assertEqual(len(accesses), 1)
                    self.assertGreater(accesses[0]["id"], reset)
                    # The initial factory loads no saved bytes, and the actual
                    # first old-version reconstruction occurs after device reset.
                    boots = [event for event in world.trace if event["kind"] == "retirement_boot"]
                    self.assertTrue(all(event["recovered_values"] == 0 for event in boots))

    def test_durable_checkpoint_survives_lost_callback_without_early_deletion(self):
        for root in ("reader", "replay"):
            row, world = run_case(Case(root=root, fault="checkpoint-reset"), 7)
            self.assertEqual(row["errors"], [])
            self.assertEqual(row["coverage"]["missing"], [])
            self.assertEqual(row["report"]["counts"]["power_loss"], 3)
            self.assertEqual(row["reads"][-1]["incarnation"], 4)
            self.assertGreater(row["report"]["counts"]["retired_service_completion"], 0)

    def test_deleting_either_live_root_dependency_is_detected_and_breaks_real_read(self):
        for root in ("reader", "replay"):
            row, world = run_case(Case(root=root, negative="ignore-root"), 19)
            self.assertTrue(any("retired live dependency" in error for error in row["errors"]))
            self.assertTrue(any("required reconstruction unavailable" in error for error in row["errors"]))
            self.assertNotIn("old_read_complete", row["report"]["counts"])
            self.assertEqual(row["coverage"]["missing"], [])

    def test_head_before_checkpoint_bytes_fails_after_power_loss(self):
        row, world = run_case(Case(negative="premature-checkpoint"), 1)
        self.assertIn("head published before matching checkpoint durable", row["errors"])
        self.assertIn("required reconstruction unavailable: data/checkpoint/4", row["errors"])
        self.assertTrue(any(event["kind"] == "durable_retire" and
                            event["key"] == "data/base/0" for event in world.trace))
        self.assertNotIn("data/checkpoint/4", row["inventory"])
        self.assertEqual(row["coverage"]["missing"], [])

    def test_paged_directory_progresses_where_whole_prefix_does_not_fit(self):
        page, _ = run_case(Case(), 7)
        whole, _ = run_case(Case(listing="whole-prefix"), 7)
        self.assertEqual(page["coverage"]["missing"], [])
        self.assertEqual(page["errors"], [])
        self.assertEqual(whole["errors"], [])  # Unfinished is not a safety violation.
        self.assertEqual(whole["coverage"]["missing"], ["collected-before-first-access"])
        self.assertEqual(whole["report"]["waits"][0]["operation"], "scan")
        self.assertEqual(whole["report"]["pending_events"], 0)
        self.assertNotIn("old_read_complete", whole["report"]["counts"])

    def test_both_backend_pins_outlive_owner_close_and_process_restart(self):
        for seed in (1, 7, 19):
            row, world = borrow_case(False, seed)
            self.assertEqual(row["errors"], [])
            self.assertEqual([probe["available"] for probe in row["probes"]], [False, False, True])
            self.assertEqual(row["report"]["counts"]["process_crash"], 1)
            self.assertEqual(row["report"]["counts"]["stale_completion"], 2)
            self.assertEqual(row["report"]["hosts"]["backend"]["memory_used"], 0)
            self.assertNotIn("backend_callback", row["report"]["counts"])

    def test_missing_second_pin_permits_reuse_while_second_backend_still_runs(self):
        row, _ = borrow_case(True, 7)
        self.assertIn("frame reuse violates backend lifetime: second-running", row["errors"])
        self.assertEqual([probe["available"] for probe in row["probes"]], [False, True, True])

    def test_oracle_rejects_matching_latest_bytes_as_an_old_root_read(self):
        _, world = run_case(Case(), 7)
        latest = next(event for event in world.trace if event["kind"] == "bytes_reconstructed")
        old = next(event for event in world.trace if event["kind"] == "bytes_reconstructed"
                   and event["root"] == "roots/reader")
        old.update(version=latest["version"], bytes=latest["bytes"])
        complete = next(event for event in world.trace if event["kind"] == "old_read_complete")
        complete["bytes"] = latest["bytes"]
        self.assertIn("reconstruction version differs from durable root", audit(world))

    def test_exact_replay(self):
        first, world = run_case(Case(fault="checkpoint-reset"), 19)
        second, _ = run_case(Case(fault="checkpoint-reset"), 19, replay=world.decisions)
        self.assertEqual(first, second)
        first, world = borrow_case(False, 19)
        second, _ = borrow_case(False, 19, replay=world.decisions)
        self.assertEqual(first, second)


if __name__ == "__main__":
    unittest.main()
