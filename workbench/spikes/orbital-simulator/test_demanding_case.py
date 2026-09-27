import unittest

from demanding_case import Case, run_case, valid_verification


class DemandingCaseTests(unittest.TestCase):
    def checked_run(self, **options):
        row, world = run_case(Case(point_count=8, delayed_checker_ns=600_000,
                                   until_ns=4_000_000, **options), seed=7)
        self.assertFalse(row["violations"])
        self.assertFalse(row["coverage"]["missing"])
        self.assertEqual(row["cohorts"]["source-point"]["completed"], 6)
        self.assertEqual(row["cohorts"]["independent-point"]["completed"], 2)
        return row, world

    def test_registered_context_and_private_late_queries_compose_with_both_bindings(self):
        for replicated in (False, True):
            with self.subTest(replicated=replicated):
                row, world = self.checked_run(replicated=replicated)
                self.assertEqual(row["cohorts"]["checked"]["completed"], 1)
                first = min(e["time"] for e in world.trace if e["kind"] == "checker_private_read" and e["actor"] == "checker_b")
                writes = [e for e in world.trace if e["kind"] == "traffic_response" and e["op"] in (2, 3, 5, 6) and e["time"] < first]
                self.assertEqual(len(writes), 4)
                commands = [e["request"]["kind"] for e in world.trace
                            if e["kind"] in ("traffic_transition", "replicated_transition")]
                self.assertIn("register-context", commands)
                self.assertNotIn("read", commands)
                for e in world.trace:
                    if e["kind"] == "checker_private_read":
                        self.assertEqual(e["value"], 100 + int(e["key"].split("k")[1]))

    def test_recovery_uses_persisted_context_job_reports_and_decision(self):
        for incident in ("checker-restart", "source-reset", "coordinator-verification-reset", "coordinator-outcome-reset"):
            with self.subTest(incident=incident):
                row, world = self.checked_run(incident=incident)
                self.assertEqual(row["cohorts"]["checked"]["completed"], 1)
                self.assertTrue(any(e["kind"] in ("process_crash", "power_loss") for e in world.trace))

    def test_missing_or_disagreeing_verifier_never_publishes_but_points_finish(self):
        for option in ("missing_checker", "mismatch_checker"):
            with self.subTest(option=option):
                row, world = self.checked_run(**{option:True})
                self.assertEqual(row["cohorts"]["checked"]["unfinished"], 1)
                self.assertFalse(any(e["kind"] == "durable_write" and e["key"] == "outcome/1" for e in world.trace))

    def test_tight_projection_budget_distinguishes_retention_from_streaming(self):
        retained, rw = self.checked_run(checker_memory_bytes=6000, retain_inputs=True)
        streamed, sw = self.checked_run(checker_memory_bytes=6000, retain_inputs=False)
        self.assertEqual(retained["cohorts"]["checked"]["unfinished"], 1)
        self.assertEqual(streamed["cohorts"]["checked"]["completed"], 1)
        self.assertTrue(any("capacity" in w["reason"] for w in rw.report()["waits"]))
        self.assertLessEqual(max(h["memory_peak"] for name,h in sw.report()["hosts"].items() if name.startswith("check_host")), 6000)

    def test_complete_report_required_not_just_matching_context(self):
        row, world = self.checked_run()
        evidence = next(e["value"] for e in world.trace if e["kind"] == "durable_write" and e["key"] == "verification/1")
        context = evidence["context"]
        self.assertTrue(valid_verification(evidence, context))
        incomplete = dict(context=context, reports={k:dict(context=context) for k in context["required"]})
        self.assertFalse(valid_verification(incomplete, context))

    def test_exact_replay_with_checker_restart(self):
        case = Case(point_count=4, delayed_checker_ns=400_000, until_ns=3_000_000, incident="checker-restart")
        row, world = run_case(case, seed=19)
        again, replay = run_case(case, seed=19, replay=world.decisions)
        replay.kernel.check_replay()
        self.assertEqual(row, again)
        self.assertEqual(world.report(), replay.report())


if __name__ == "__main__":
    unittest.main()
