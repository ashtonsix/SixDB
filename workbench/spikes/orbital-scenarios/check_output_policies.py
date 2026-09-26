#!/usr/bin/env python3
"""Bounded checks for the two compute-first output candidates."""

from copy import deepcopy
import unittest

from check_comparison import drive_until, fixture, reports_case, transaction
from comparison import Simulation
from output_policies import OutputSimulation, POLICIES


def hot_case():
    return fixture("computed-hot", {"eu/x": 0, "us/y": 0}, [
        transaction(f"hot{index}", 0, ["eu/x", "us/y"], ["eu/x", "us/y"],
                    delay=8, group="hot") for index in range(4)], horizon=500)


class OutputPolicyChecks(unittest.TestCase):
    def run_case(self, case, policy):
        simulation = OutputSimulation(deepcopy(case), policy)
        result = simulation.run()
        self.assertTrue(result["serial_check"])
        simulation.store.check_serial()
        self.assertFalse(simulation.admission.grants)
        self.assertFalse(simulation.admission.waiting)
        return simulation, result

    def test_waiting_does_not_repair_already_stale_rmw(self):
        simulation, result = self.run_case(hot_case(), "output-wait")
        self.assertEqual(result["cohorts"]["hot"]["complete"], 1)
        self.assertEqual(result["cohorts"]["hot"]["failed"], 3)
        self.assertEqual(result["cohorts"]["hot"]["rejections"], {"source_changed": 3})
        self.assertEqual(result["cohorts"]["hot"]["program_executions"], 4)
        self.assertEqual(simulation.store.head(), {"eu/x": 1, "us/y": 1})
        self.assertGreater(result["write_admission"]["blocked_requests"], 0)

    def test_refresh_progress_uses_two_passes_but_one_attempt_budget(self):
        simulation, result = self.run_case(hot_case(), "output-refresh")
        self.assertEqual(result["cohorts"]["hot"]["complete"], 4)
        self.assertEqual(result["cohorts"]["hot"]["attempts"], 4)
        self.assertEqual(result["cohorts"]["hot"]["program_executions"], 8)
        self.assertEqual(result["counts"]["discovery_passes_discarded"], 4)
        self.assertEqual(result["counts"]["refresh_executions"], 4)
        self.assertEqual(result["counts"]["discovery_compute_units"], 20)
        self.assertEqual(result["counts"]["discarded_compute_units"], 20)
        self.assertEqual(result["counts"]["compute_units"], 40)
        self.assertEqual(result["counts"]["private_execution_delay_ticks"], 64)
        self.assertEqual(simulation.store.head(), {"eu/x": 4, "us/y": 4})
        for index in range(4):
            original = simulation.store.attempts[f"hot{index}#1/pass0"]
            refreshed = simulation.store.attempts[f"hot{index}#1/pass1"]
            self.assertEqual(original.decision, "abort")
            self.assertEqual(simulation.store.promises[original.owner], [])
            self.assertEqual(refreshed.decision, "commit")
            self.assertGreater(refreshed.s, original.s)

    def test_atomic_local_work_keeps_single_execution_and_shared_cost(self):
        case = fixture("atomic-local", {"eu/x": 0}, [
            transaction(f"local{index:02}", 0, ["eu/x"], ["eu/x"], group="local")
            for index in range(16)], capacity=8)
        baseline = Simulation(deepcopy(case), "snapshot-wait").run()
        for policy in POLICIES:
            with self.subTest(policy=policy):
                simulation, result = self.run_case(case, policy)
                self.assertEqual(result["cohorts"]["local"]["complete"], 16)
                self.assertEqual(result["cohorts"]["local"]["program_executions"], 16)
                self.assertEqual(result["counts"]["compute_units"], baseline["counts"]["compute_units"])
                self.assertEqual(result["counts"].get("refresh_executions", 0), 0)
                self.assertEqual(simulation.store.head(), {"eu/x": 16})

    def test_initial_private_computation_holds_no_output_gate(self):
        case = fixture("private-discovery", {"eu/x": 0, "us/y": 0, "eu/free": 0}, [
            transaction("writer", 0, [], ["eu/x", "us/y"], "blind", delay=40),
            transaction("free", 5, ["eu/free"], ["eu/free"]),
        ])
        for policy in POLICIES:
            with self.subTest(policy=policy):
                simulation = OutputSimulation(deepcopy(case), policy)
                drive_until(simulation, lambda: simulation.tx["free"].state == "complete")
                self.assertFalse(simulation.admission.grants)
                self.assertFalse(simulation.store.claims)
                self.assertEqual(simulation.tx["writer"].phase, "compute_delay")
                simulation.run()
                simulation.store.check_serial()
                admissions = [event for event in simulation.trace
                              if event["event"] == "computed_output_admission"]
                self.assertTrue(admissions)
                self.assertGreaterEqual(min(event["t"] for event in admissions), 40)

    def test_blocked_local_capture_releases_preliminary_output_gate(self):
        case = fixture("local-capture-gate", {"eu/source": 0, "us/remote": 0, "eu/out": 0}, [
            transaction("holder", 0, [], ["eu/source", "us/remote"], "blind", value=1),
            transaction("reader-writer", 4, ["eu/source"], ["eu/out"], "max"),
            transaction("independent", 6, [], ["eu/out"], "blind", value=5),
        ], link_delay=8)
        simulation = OutputSimulation(deepcopy(case), "output-wait")
        drive_until(simulation, lambda: any(event["event"] == "local_gate_released_for_capture"
                                            for event in simulation.trace))
        self.assertNotIn("eu/out", simulation.admission.grants)
        self.assertNotIn("eu/out", simulation.store.claims)
        simulation.run()
        simulation.store.check_serial()
        self.assertEqual(simulation.tx["independent"].state, "complete")
        self.assertLess(simulation.tx["independent"].completion,
                        simulation.tx["reader-writer"].completion)
        self.assertFalse(simulation.admission.grants)

    def test_changed_refreshed_outputs_release_original_gate_instead_of_expanding(self):
        class ChangedOutput(OutputSimulation):
            def compute_values(self, tx):
                super().compute_values(tx)
                if self.output_work[tx.spec["id"]].refreshing:
                    tx.writes = {"eu/new": 2}
                    tx.attempt.writes.clear()
                    tx.attempt.writes.update(tx.writes)

        case = fixture("changed-output", {"eu/source": 1, "eu/old": 0, "eu/new": 0}, [
            transaction("writer", 0, ["eu/source"], ["eu/old"], "max", delay=1)])
        simulation = ChangedOutput(case, "output-refresh")
        result = simulation.run()
        self.assertEqual(result["cohorts"]["writer"]["failed"], 1)
        self.assertEqual(result["cohorts"]["writer"]["attempts"], 1)
        self.assertEqual(result["cohorts"]["writer"]["rejections"], {"output_footprint_changed": 1})
        self.assertFalse(simulation.admission.grants)
        self.assertFalse(simulation.store.claims)
        self.assertEqual(simulation.admission.counters["granted_keys"], 1)
        self.assertEqual(simulation.store.head(), case["initial"])

    def test_reports_use_no_gates_or_discovery_rerun(self):
        case = fixture("read-only", {"eu/x": 1, "us/y": 2}, [
            transaction("report", 0, ["eu/x", "us/y"], [], "report", delay=5)])
        for policy in POLICIES:
            with self.subTest(policy=policy):
                simulation, result = self.run_case(case, policy)
                self.assertEqual(result["cohorts"]["report"]["program_executions"], 1)
                self.assertEqual(result["write_admission"].get("granted_keys", 0), 0)
                self.assertEqual(simulation.tx["report"].attempt.c, simulation.tx["report"].attempt.s)

    def test_serial_reports_and_exact_replay(self):
        case = reports_case(allow_old=True)
        for policy in POLICIES:
            with self.subTest(policy=policy):
                first, first_result = self.run_case(case, policy)
                second, second_result = self.run_case(case, policy)
                self.assertEqual(first_result, second_result)
                self.assertEqual(first.trace, second.trace)
                self.assertEqual(first.store.trace, second.store.trace)
                for decision in first.decisions:
                    if not decision["id"].startswith("report"):
                        continue
                    for index in range(4):
                        self.assertEqual(decision["reads"]["eu"][f"eu/{index}"],
                                         decision["reads"]["us"][f"us/{index}"])


if __name__ == "__main__":
    unittest.main()
