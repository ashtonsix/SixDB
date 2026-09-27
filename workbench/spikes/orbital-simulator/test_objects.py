"""Observers use independent value/transcript properties, never actor decisions."""

import unittest

from objects import (META_BYTES, PAGE_BYTES, extension_world, lease_world,
                     run_extensions, run_objects)


from object_oracles import check_observations, check_verification, events


class ObjectTests(unittest.TestCase):
    def test_preparation_policies_preserve_late_snapshot_values(self):
        for policy, capacity in (("demand", 1), ("window", 2), ("full", 4)):
            with self.subTest(policy=policy):
                world = run_objects(policy=policy, resident_pages=capacity)
                check_observations(world)
                self.assertEqual([e["value"] for e in events(world, "view_observed")],
                                 [10, 20, 30, 40])
                self.assertEqual(len(events(world, "complete", "R")), 1)
                self.assertFalse(events(world, "read_refused"))

    def test_latest_version_negative_control_is_detected(self):
        world = run_objects(policy="demand", resident_pages=1, unsafe_latest=True)
        self.assertEqual([e["value"] for e in events(world, "view_observed")],
                         [10, 99, 30, 40])
        with self.assertRaises(AssertionError):
            check_observations(world)

    def test_full_preparation_refuses_when_projection_exceeds_budget(self):
        world = run_objects(policy="full", resident_pages=2)
        self.assertEqual(len(events(world, "read_refused")), 4)
        self.assertFalse(events(world, "view_observed"))
        self.assertEqual(len(events(world, "refused", "R")), 1)

    def test_first_old_view_opened_after_write_reconstructs_old_values(self):
        world = run_objects(late_open=True, resident_pages=1)
        check_observations(world)
        write = events(world, "object_write", "W")[0]
        observations = events(world, "view_observed")
        self.assertTrue(all(e["time"] > write["time"] for e in observations))
        self.assertEqual([e["value"] for e in observations], [10, 20, 30, 40])

    def test_restart_discovers_retained_history_through_storage(self):
        world = run_objects(resident_pages=1)
        world.crash("store")
        world.restart("store")
        world.inject("reader", "start", {"name": "reopened", "steps": [
            {"kind": "read", "id": "old-after-restart", "cut": 10, "page": 1},
            {"kind": "read", "id": "new-after-restart", "cut": 30, "page": 1}]},
            at=world.kernel.now + 1)
        world.run(until=20_000_000)
        check_observations(world)
        self.assertEqual(len(events(world, "object_reopened")), 1)
        self.assertEqual(events(world, "view_observed", "old-after-restart")[0]["value"], 20)
        self.assertEqual(events(world, "view_observed", "new-after-restart")[0]["value"], 99)
        self.assertEqual(len(events(world, "complete", "reopened")), 1)

    def test_other_backend_users_can_refuse_a_fitting_view(self):
        # 2 KiB remain for small control/disk records, less than one 4 KiB page.
        budget = META_BYTES + 2 * PAGE_BYTES
        world = run_objects(pressure_bytes=budget - 2048)
        refused = events(world, "read_refused")
        self.assertEqual(len(refused), 4)
        self.assertEqual(len(events(world, "refused", "R")), 1)
        self.assertTrue(any(e["label"].startswith("view/")
                            for e in events(world, "memory_refused")))
        self.assertLessEqual(world.hosts["h"].peak, budget)
        self.assertEqual(world.hosts["h"].used, 0)

    def test_window_reduces_batches_without_reducing_required_values(self):
        demand = run_objects(policy="demand", resident_pages=2, scan=True)
        window = run_objects(policy="window", resident_pages=2, scan=True)
        full = run_objects(policy="full", resident_pages=4, scan=True)
        for world in (demand, window, full):
            check_observations(world)
        self.assertEqual([len(events(w, "page_batch")) for w in (demand, window, full)],
                         [4, 2, 1])
        self.assertEqual([len(events(w, "page_materialized")) for w in (demand, window, full)],
                         [4, 4, 4])

    def test_backend_borrow_survives_owner_release_and_callback_entry(self):
        world = lease_world()
        world.run(until=100_000)
        probes = {e["phase"]: e["acquired"] for e in events(world, "lease_probe")}
        self.assertEqual(probes, {"after_owner_release": False,
                                  "inside_completion": False, "after_completion": True})
        self.assertEqual(len(events(world, "complete", "lease")), 1)

    def test_alternative_event_orders_preserve_observations(self):
        for seed in range(5):
            world = run_objects(seed=seed, ordering="shuffle", resident_pages=1)
            check_observations(world)
            self.assertEqual(len(events(world, "complete", "R")), 1)


class ExtensionTests(unittest.TestCase):
    def test_equal_final_values_do_not_hide_transcript_mismatch(self):
        world = run_extensions()
        check_verification(world)
        transcripts = events(world, "extension_transcript", "T")
        self.assertEqual([e["transcript"]["result"] for e in transcripts], ["OK", "OK"])
        self.assertNotEqual(transcripts[0]["transcript"], transcripts[1]["transcript"])
        self.assertFalse(events(world, "published", "T"))
        self.assertEqual(len(events(world, "aborted", "T")), 1)
        self.assertEqual(len(events(world, "context_registered", "T")), 1)
        history = world.durable("h", "store")["history"]
        self.assertEqual([w["tx"] for w in history["writes"]], ["U"])

    def test_independent_write_publishes_before_slow_required_checker(self):
        world = run_extensions()
        published = events(world, "published", "U")[0]
        slow = next(e for e in events(world, "extension_transcript") if e["checker"] == "check-b")
        self.assertLess(published["time"], slow["time"])
        self.assertGreater(published["position"], 10)
        self.assertEqual(published["value"], 7)

    def test_missing_checker_is_neither_match_nor_mismatch(self):
        world = extension_world(slow_work=100_000_000)
        world.run(until=2_000_000)
        self.assertFalse(events(world, "verification_decision", "T"))
        self.assertFalse(events(world, "published", "T"))
        self.assertEqual(len(events(world, "published", "U")), 1)
        self.assertTrue(world.explain("T"))

    def test_matching_transcripts_publish_actual_effect(self):
        world = run_extensions(mismatch=False)
        check_verification(world)
        self.assertEqual(len(events(world, "published", "T")), 1)
        history = world.durable("h", "store")["history"]
        writes = {w["tx"]: w for w in history["writes"]}
        self.assertEqual(writes["T"]["value"], 1)
        self.assertEqual(writes["T"]["position"], 10)
        self.assertEqual(writes["U"]["value"], 7)

    def test_final_only_negative_control_is_detected(self):
        world = run_extensions(unsafe_final_only=True)
        self.assertEqual(len(events(world, "published", "T")), 1)
        with self.assertRaises(AssertionError):
            check_verification(world)

    def test_shuffle_does_not_change_mismatch_outcome(self):
        for seed in range(5):
            world = run_extensions(seed=seed, ordering="shuffle")
            check_verification(world)
            self.assertEqual(len(events(world, "aborted", "T")), 1)
            self.assertEqual(len(events(world, "published", "U")), 1)


if __name__ == "__main__":
    unittest.main()
