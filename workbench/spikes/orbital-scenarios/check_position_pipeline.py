#!/usr/bin/env python3
"""Independent semantic checks for exact-position admission pipelining."""

from itertools import permutations
import unittest

from position_pipeline import database, finish, finish_read, prepared, run_probes


class PositionPipelineChecks(unittest.TestCase):
    def test_authored_histories_and_unsafe_provisional_counterexample(self):
        result = run_probes()
        self.assertEqual(len(result["scenarios"]), 7)
        for case in result["scenarios"][:-1]:
            self.assertTrue(case["serial_check"])
        self.assertTrue(result["scenarios"][-1]["unsafe_variant_rejected"])

    def test_admission_cannot_release_after_only_one_exact_publication(self):
        db, gates = database({"eu/x": 0, "us/y": 0})
        gates.priority["A"] = (0, "A")
        self.assertTrue(gates.request("A", ["eu/x"]))
        self.assertTrue(gates.request("A", ["us/y"]))
        first = db.begin_fixed("A", (10, "A"), ["eu/x", "us/y"], gates.grants, "A")
        db.reserve_position(first, ["eu/x"])
        self.assertFalse(db.admission_releasable(first))
        db.reserve_position(first, ["us/y"])
        db.fix_position(first)
        db.publish_position(first, ["eu/x"])
        self.assertFalse(db.admission_releasable(first))
        # Deliberately violate the caller's release rule: the next reservation
        # independently rejects the still-incomplete predecessor publication.
        gates.release("A", ["eu/x"])
        gates.priority["B"] = (1, "B")
        gates.request("B", ["eu/x"])
        second = db.begin_fixed("B", (11, "B"), ["eu/x"], gates.grants, "B")
        with self.assertRaisesRegex(AssertionError, "exact at every authority"):
            db.reserve_position(second, ["eu/x"])
        db.publish_position(first, ["us/y"])
        self.assertTrue(db.admission_releasable(first))
        db.reserve_position(second, ["eu/x"])
        self.assertGreater(db.reservation_bounds["B"][("eu/x",)], first.c)

    def test_three_blind_versions_accept_every_installation_order(self):
        for order in permutations(range(3)):
            with self.subTest(order=order):
                db, gates = database({"eu/x": 0})
                attempts = [prepared(db, gates, f"writer-{index}", ["eu/x"], index + 1)
                            for index in range(3)]
                for index, attempt in enumerate(attempts):
                    attempt.writes["eu/x"] = index + 1
                    db.seal_values(attempt)
                    db.decide(attempt, True)
                waiting = []
                for step, index in enumerate(order):
                    db.install(attempts[index], ["eu/x"])
                    reader = db.begin(f"reader-{step}", (100, f"reader-{step}"))
                    captured = db.capture(reader, "eu/x", wait=True)
                    self.assertEqual(captured, 2 in order[:step + 1])
                    if captured:
                        self.assertEqual(reader.reads["eu/x"]["eu/x"], 3)
                        finish_read(db, reader)
                    else:
                        waiting.append(reader)
                for reader in waiting:
                    self.assertTrue(db.capture(reader, "eu/x", wait=True))
                    finish_read(db, reader)
                self.assertEqual(db.check_serial(), {"eu/x": 3})
                self.assertEqual(len(db.versions["eu/x"]), 4)

    def test_ordinary_local_shortcut_cannot_renew_past_unseen_pending_predecessor(self):
        db, gates = database({"eu/x": 0})
        prior = prepared(db, gates, "prior", ["eu/x"], 10)
        local = db.begin("local", (1, "local"))
        self.assertTrue(db.capture(local, "eu/x"))
        local.writes["eu/x"] = 1
        self.assertTrue(db.promise(local, ["eu/x"]))
        self.assertGreater(db.choose(local), prior.c)
        self.assertFalse(db.renew(local, "eu/x"))
        self.assertEqual(local.rejection_reason, "renew_promise")
        db.decide(local, False)
        db.release(local, ["eu/x"])
        finish(db, prior, {"eu/x": 5})
        self.assertEqual(db.check_serial(), {"eu/x": 5})

    def test_ordinary_blind_shortcut_can_pass_pending_older_version(self):
        db, gates = database({"eu/x": 0})
        prior = prepared(db, gates, "prior", ["eu/x"], 10)
        local = db.begin("blind", (1, "blind"))
        local.writes["eu/x"] = 7
        self.assertTrue(db.promise(local, ["eu/x"]))
        self.assertGreater(db.choose(local), prior.c)
        db.decide(local, True)
        db.install(local, ["eu/x"])
        finish(db, prior, {"eu/x": 3})
        self.assertEqual(db.check_serial(), {"eu/x": 7})

    def test_resolved_no_write_and_abort_do_not_lower_allocation_head(self):
        for commit in (True, False):
            with self.subTest(commit=commit):
                db, gates = database({"eu/x": 0})
                prior = prepared(db, gates, "prior", ["eu/x"], 100)
                if commit:
                    finish(db, prior, {})
                else:
                    db.decide(prior, False)
                    db.release(prior, ["eu/x"])
                later = prepared(db, gates, "later", ["eu/x"], 1)
                self.assertGreater(later.c, prior.c)
                finish(db, later, {"eu/x": 1})
                self.assertEqual(db.check_serial(), {"eu/x": 1})

    def test_exact_replay(self):
        self.assertEqual(run_probes(), run_probes())


if __name__ == "__main__":
    unittest.main()
