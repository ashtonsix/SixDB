"""Observer checks and bounded adversarial histories over existing actor paths."""
from dataclasses import asdict
import json
from pathlib import Path
import tempfile
import unittest

from adversity import build_case, cases, evaluate, observe
from campaigns import Runner, all_work


class AdversityTests(unittest.TestCase):
    def run_case(self, case, seed=1):
        trial = build_case(case, seed)
        trial.world.run(until=trial.until)
        return trial, observe(trial)

    def complete(self, observation):
        self.assertEqual(observation.violations, [])
        for group in observation.cohorts.values():
            self.assertEqual(group["completed"], group["offered"], group)
            self.assertEqual(group["unfinished"], 0)
            self.assertEqual(group["refused"], 0)

    def test_healthy_and_transient_payload_paths_complete(self):
        for severity in (0, 1, 2):
            _, result = self.run_case(dict(family="payload", severity=severity))
            self.complete(result)
            self.assertEqual(result.metrics["chosen_prefix"], 4)
            self.assertEqual(result.metrics["historically_two_domain_bodies"], 4)
            self.assertEqual(result.metrics["currently_retained_bodies"], 4)

    def test_isolation_and_destruction_have_different_recovery_evidence(self):
        observed = {}
        for severity in (3, 4):
            trial, result = self.run_case(dict(family="payload", severity=severity))
            observed[severity] = result
            self.assertEqual(result.violations, [])
            self.assertEqual(result.metrics["chosen_prefix"], 4)
            self.assertEqual(result.metrics["live_witnesses"], 3)
            self.assertEqual(result.metrics["historically_two_domain_bodies"], 4)
            self.assertEqual(result.cohorts["publication"]["unfinished"], 4)
            self.assertEqual(result.cohorts["ordinary"]["completed"], 1)
        self.assertEqual(observed[3].metrics["currently_retained_bodies"], 4)
        self.assertEqual(observed[4].metrics["currently_retained_bodies"], 0)
        self.assertEqual(observed[4].metrics["chosen_bodies_without_durable_copy"], 4)
        self.assertEqual(observed[4].details["pending_semantic_triggers"], 0)
        self.assertEqual(sum(r["kind"] == "storage_destroyed"
                             for r in observed[4].details["incident_events"]), 3)

    def test_post_publication_loss_does_not_retroactively_invalidate_history(self):
        for severity in (5, 6):
            _, result = self.run_case(dict(family="payload", severity=severity))
            self.assertEqual(result.violations, [])
            self.assertEqual(result.cohorts["publication"]["completed"], 4)
            self.assertEqual(result.metrics["historically_two_domain_bodies"], 4)
            self.assertEqual(result.metrics["currently_retained_bodies"], 0)
            self.assertEqual(result.metrics["current_canonical_body_prefix"], 0)
            self.assertEqual(result.metrics["live_witnesses"], 3)
            self.assertEqual(result.metrics["current_materialized_state_lsn"], 4 if severity == 5 else 0)
            self.assertEqual(result.details["pending_semantic_triggers"], 0)

    def test_publication_waits_for_actual_body_path_restoration(self):
        trial = build_case(dict(family="payload", severity=2, heal_ns=1_200_000))
        trial.world.run(until=1_100_000)
        before = observe(trial)
        self.assertEqual(before.metrics["chosen_prefix"], 4)
        self.assertEqual(before.cohorts["publication"]["completed"], 0)
        trial.world.run(until=trial.until)
        after = observe(trial)
        self.complete(after)
        self.assertTrue(all(t > 1_200_000 for t in after.cohorts["publication"]["completions_ns"].values()))

    def test_ack_loss_can_leave_sender_unknown_after_publication(self):
        trial = build_case(dict(family="response", severity=1))
        trial.world.run(until=1_100_000)
        before = observe(trial)
        self.assertEqual(before.cohorts["publication"]["completed"], 4)
        self.assertEqual(before.cohorts["producer_knowledge"]["completed"], 0)
        trial.world.run(until=trial.until)
        self.complete(observe(trial))

    def test_restart_route_rollover_rejects_old_configuration(self):
        trial, result = self.run_case(dict(family="response", severity=3))
        self.complete(result)
        routes = [r for r in result.details["incident_events"] if r["kind"] == "route_changed"]
        for actor in ("producer", "consumer"):
            self.assertEqual([r["version"] for r in routes if r["actor"] == actor], [2])
        self.assertTrue(any(r["kind"] == "power_loss" for r in result.details["incident_events"]))
        self.assertTrue(any(r["kind"] == "consumer_restored" for r in trial.world.trace))

    def test_durable_finalization_pressure_does_not_imply_completion(self):
        _, result = self.run_case(dict(family="finalization", resource="durable", severity=1))
        self.assertEqual(result.violations, [])
        self.assertEqual(result.metrics["chosen_prefix"], 4)
        self.assertEqual(result.cohorts["publication"]["completed"], 0)
        self.assertEqual(result.cohorts["publication"]["unfinished"], 4)
        self.assertEqual(result.cohorts["ordinary"]["completed"], 1)
        self.assertTrue(any(w["reason"] == "durable application backpressure"
                            for w in result.details["model"]["waits"]))

    def test_temporary_service_pressure_drains_and_charges_collateral(self):
        _, baseline = self.run_case(dict(family="payload", severity=0))
        for resource in ("workers", "control", "disk_bytes"):
            _, result = self.run_case(dict(family="finalization", resource=resource, severity=1))
            self.complete(result)
            self.assertGreater(result.metrics["foreground_max_ns"], baseline.metrics["foreground_max_ns"])
            pauses = [r for r in result.details["incident_events"] if r["kind"] == "service_pause"]
            self.assertEqual([r["paused"] for r in pauses], [True, False])

    def test_scheduled_denominator_survives_actor_never_booting(self):
        trial = build_case(dict(family="payload", severity=0))
        trial.world.pause("source", "control")
        trial.world.run(until=trial.until)
        result = observe(trial)
        self.assertFalse(any(r["kind"] == "offered" and r.get("actor") == "producer"
                             for r in trial.world.trace))
        self.assertEqual(result.cohorts["publication"]["offered"], 4)
        self.assertEqual(result.cohorts["publication"]["unfinished"], 4)
        self.assertEqual(result.cohorts["publication"]["arrivals_ns"]["1"], 140_000)
        self.assertEqual(result.cohorts["publication"]["unfinished_age_ns"]["1"], trial.until - 140_000)

    def test_composed_materialization_and_publication_stay_distinct(self):
        trial = build_case(dict(family="composed", severity=1))
        trial.world.run(until=1_100_000)
        before = observe(trial)
        self.assertEqual(before.cohorts["private_result"]["completed"], 2)
        self.assertEqual(set(before.cohorts["publication"]["completions_ns"]), {"1"})
        self.assertEqual(before.cohorts["foreground"]["completed"], 20)
        trial.world.run(until=trial.until)
        self.complete(observe(trial))

    def test_composed_recovery_and_foreground_interference(self):
        _, baseline = self.run_case(dict(family="composed", severity=0))
        _, result = self.run_case(dict(family="composed", severity=3))
        self.complete(result)
        self.assertGreater(result.metrics["foreground_max_ns"], baseline.metrics["foreground_max_ns"])
        self.assertEqual(result.metrics["retained_outbox_commands"], 2)
        self.assertEqual(result.metrics["retained_public_versions"], 2)
        self.assertEqual(result.cohorts["foreground"]["offered"], 20)

    def test_existing_negative_protocol_control_is_rejected(self):
        _, result = self.run_case(dict(family="payload", negative="publish-too-early"))
        self.assertTrue(result.violations)
        record = Runner(evaluate).run(dict(family="payload", negative="publish-too-early"), 1)
        self.assertFalse(all_work(record))

    def test_exact_replay_matches_faults_cohorts_and_observations(self):
        case = dict(family="response", severity=3)
        first, observation = self.run_case(case, seed=7)
        repeated = build_case(case, seed=7, replay=first.world.decisions)
        repeated.world.run(until=repeated.until)
        repeated.world.kernel.check_replay()
        self.assertEqual(asdict(observe(repeated)), asdict(observation))
        self.assertEqual(repeated.world.trace, first.world.trace)

    def test_panel_matches_across_small_heldout_schedule_set(self):
        for seed in (0, 19):
            for case in cases():
                trial, result = self.run_case(case, seed)
                self.assertEqual(result.violations, [], (case, seed, result.violations))
                for group in result.cohorts.values():
                    self.assertEqual(group["offered"], group["completed"] + group["refused"] + group["unfinished"])
                self.assertEqual(result.details["pending_semantic_triggers"], 0)

    def test_runner_records_input_errors_and_saves_run_diagnostics(self):
        with tempfile.TemporaryDirectory() as temp:
            runner = Runner(evaluate, output=Path(temp) / "campaign")
            case = dict(family="response", severity=3, start_ns=9_000_000, heal_ns=10_000_000)
            record = runner.run(case, 3)
            # A cutoff before the authored future incident is an ordinary bounded
            # observation, not a fabricated crash or a simulator error.
            self.assertEqual(record["status"], "ok")
            bad = runner.run(dict(family="unsupported"), 3)
            self.assertEqual(bad["status"], "error")
            evidence = Path(temp) / "campaign" / (record["id"] + ".evidence")
            self.assertTrue((evidence / "trace.jsonl").is_file())
            saved = json.loads((evidence / "case.json").read_text())
            self.assertEqual(saved, dict(case=case, seed=3))

    def test_unknown_parameter_is_not_silently_ignored(self):
        with self.assertRaises(ValueError):
            build_case(dict(family="payload", unknowable_rate=10))


if __name__ == "__main__":
    unittest.main()
