#!/usr/bin/env python3
"""Bounded transport and semantic checks for fixed execution positions."""

from copy import deepcopy
import unittest

from check_comparison import drive_until, fixture, transaction
from check_output_policies import hot_case
from comparison_inputs import cases
from fixed_simulation import FixedSimulation, POLICIES


class FixedSimulationChecks(unittest.TestCase):
    def run_case(self, case, policy):
        simulation = FixedSimulation(deepcopy(case), policy)
        result = simulation.run()
        self.assertTrue(result["serial_check"])
        if all(tx.state != "committed" for tx in simulation.tx.values()):
            simulation.store.check_serial()
        return simulation, result

    def test_hot_writes_finish_without_source_renewal_or_halved_budget(self):
        for policy, executions in (("fixed-known", 4), ("fixed-discovered", 8)):
            with self.subTest(policy=policy):
                simulation, result = self.run_case(hot_case(), policy)
                self.assertEqual(result["cohorts"]["hot"]["complete"], 4)
                self.assertEqual(result["cohorts"]["hot"]["attempts"], 4)
                self.assertEqual(result["cohorts"]["hot"]["program_executions"], executions)
                self.assertEqual(result["certification"].get("renew_calls", 0), 0)
                self.assertEqual(simulation.store.head(), {"eu/x": 4, "us/y": 4})
                self.assertFalse(simulation.store.claims)
                self.assertFalse(simulation.admission.grants)

    def test_partial_gates_do_not_create_read_blocking_promises(self):
        case = fixture("partial-fixed-gates", {"eu/x": 0, "us/y": 0}, [
            transaction("holder", 0, [], ["us/y"], "blind", coordinator="us", value=1, delay=40),
            transaction("both", 5, [], ["eu/x", "us/y"], "blind", value=2),
            transaction("reader", 20, ["eu/x"], [], "report"),
        ], link_delay=4)
        simulation = FixedSimulation(case, "fixed-known")
        drive_until(simulation, lambda: "both" in simulation.admission.waiting)
        self.assertEqual(simulation.admission.grants["eu/x"], "both")
        self.assertEqual(simulation.admission.grants["us/y"], "holder")
        self.assertNotIn("eu/x", simulation.store.claims)
        self.assertIsNone(simulation.tx["both"].attempt)
        drive_until(simulation, lambda: simulation.tx["reader"].state == "complete")
        self.assertEqual(simulation.tx["reader"].captured["eu/x"], {"eu/x": 0})
        self.assertNotIn("eu/x", simulation.store.claims)
        simulation.run()
        simulation.store.check_serial()

    def test_two_disjoint_output_writers_resolve_cross_reads_by_position(self):
        case = fixture("conditional-doctors", {"eu/a": 1, "us/b": 1}, [
            transaction("A", 0, ["us/b"], ["eu/a"], "conditional-zero",
                        condition_key="us/b", coordinator="eu"),
            transaction("B", 0, ["eu/a"], ["us/b"], "conditional-zero",
                        condition_key="eu/a", coordinator="us"),
        ], link_delay=3)
        for policy in POLICIES:
            with self.subTest(policy=policy):
                simulation, result = self.run_case(case, policy)
                self.assertTrue(all(tx.state == "complete" for tx in simulation.tx.values()))
                self.assertEqual(sum(simulation.store.head().values()), 1)
                self.assertEqual(sum(not record["writes"] for record in simulation.decisions), 1)
                self.assertGreater(result["certification"].get("capture_waits", 0), 0)
                self.assertEqual(result["certification"].get("unused_outputs_resolved", 0), 1)
                self.assertEqual(result["certification"].get("renew_calls", 0), 0)
                self.assertFalse(simulation.store.claims)
                self.assertFalse(simulation.admission.grants)
                for tx in simulation.tx.values():
                    self.assertEqual(simulation.store.resolved[tx.attempt.owner],
                                     simulation.store.coverage[tx.attempt.owner])

    def test_initial_discovery_no_write_finishes_as_read_only_despite_later_source_change(self):
        case = fixture("initial-no-write", {"eu/output": 9, "us/condition": 0}, [
            transaction("conditional", 0, ["us/condition"], ["eu/output"], "conditional-zero",
                        condition_key="us/condition", coordinator="eu", delay=30),
            transaction("source", 20, [], ["us/condition"], "blind", coordinator="us", value=1),
        ], link_delay=2)
        for policy in POLICIES:
            with self.subTest(policy=policy):
                simulation, result = self.run_case(case, policy)
                tx = simulation.tx["conditional"]
                self.assertEqual(tx.state, "complete")
                self.assertEqual(tx.writes, {})
                self.assertEqual(tx.attempt.s, tx.attempt.c)
                self.assertEqual(tx.captured["us/condition"], {"us/condition": 0})
                self.assertEqual(simulation.store.head(), {"eu/output": 9, "us/condition": 1})
                self.assertEqual(result["cohorts"]["conditional"]["program_executions"], 1)
                self.assertFalse(simulation.store.claims)
                self.assertFalse(simulation.admission.grants)
                if policy == "fixed-discovered":
                    self.assertNotIn(tx.attempt.owner, simulation.store.coverage)
                    self.assertEqual(simulation.store.promises[tx.attempt.owner], [])
                    self.assertEqual(simulation.output_work["conditional"].gate_keys, ())
                else:
                    self.assertEqual(simulation.store.resolved[tx.attempt.owner], {"eu/output"})

    def test_abort_before_values_resolves_full_coverage_and_late_reservation(self):
        case = fixture("abort-metadata", {"eu/x": 0, "us/y": 0}, [
            transaction("writer", 0, [], ["eu/x", "us/y"], "blind", value=9),
        ], link_delay=5)
        simulation = FixedSimulation(case, "fixed-known")
        drive_until(simulation, lambda: "eu/x" in simulation.store.claims)
        writer = simulation.tx["writer"]
        self.assertEqual(writer.writes, {})
        self.assertEqual(writer.attempt.writes, {})
        self.assertNotIn("us/y", simulation.store.claims)
        simulation.abort(writer, "forced_before_values")
        result = simulation.run()
        self.assertEqual(result["cohorts"]["writer"]["failed"], 1)
        self.assertFalse(simulation.store.claims)
        self.assertFalse(simulation.admission.grants)
        self.assertEqual(simulation.store.head(), case["initial"])
        self.assertEqual(simulation.store.coverage[writer.attempt.owner], {"eu/x", "us/y"})
        self.assertEqual(writer.attempt.counters["released_keys"], 2)
        # The US reservation was dispatched before abort and reaches US before
        # its abort message. It is granted and then explicitly released there.
        self.assertEqual(writer.attempt.counters["position_groups_reserved"], 2)
        before = dict(simulation.store.claims)
        simulation.reserve_position(writer, "us", ("us/y",))
        self.assertEqual(simulation.store.claims, before)

    def test_final_values_need_separate_delivery_and_acknowledgement(self):
        case = fixture("stage-final-values", {"eu/x": 0, "us/y": 0}, [
            transaction("writer", 0, [], ["eu/x", "us/y"], "blind", value=9),
        ], link_delay=5)
        simulation = FixedSimulation(case, "fixed-known")
        drive_until(simulation, lambda: "writer" in simulation.fixed_work
                    and set(simulation.fixed_work["writer"].staged) == {"eu"})
        writer = simulation.tx["writer"]
        self.assertEqual(writer.phase, "values_stage")
        self.assertIsNone(writer.attempt.decision)
        self.assertEqual(simulation.store.head(), case["initial"])
        self.assertEqual(simulation.decisions, [])
        result = simulation.run()
        events = simulation.trace
        sealed = next(event["t"] for event in events if event["event"] == "final_values_sealed")
        staged = next(event["t"] for event in events
                      if event["event"] == "final_values_staged" and event["owner"] == "us")
        acknowledged = next(event["t"] for event in events
                            if event["event"] == "final_values_acknowledged" and event["owner"] == "us")
        committed = next(event["t"] for event in events if event["event"] == "commit")
        self.assertGreaterEqual(staged - sealed, case["link_delay"])
        self.assertGreaterEqual(acknowledged - staged, case["link_delay"])
        self.assertGreaterEqual(committed, acknowledged)
        self.assertEqual(result["counts"]["final_value_groups_staged"], 2)
        self.assertEqual(result["counts"]["final_values_staged"], 2)
        self.assertGreaterEqual(result["counts"]["network_messages"], 10)
        simulation.store.check_serial()

    def test_capture_deadline_stays_visible_and_larger_budget_changes_outcome(self):
        for deadline in (20, 200):
            with self.subTest(deadline=deadline):
                case = fixture("fixed-read-deadline", {"eu/x": 0, "us/out": 0}, [
                    transaction("holder", 0, [], ["eu/x"], "blind", value=1, delay=70),
                    transaction("reader", 10, ["eu/x"], ["us/out"], "max", coordinator="us"),
                ], link_delay=2, read_wait_ticks=deadline)
                simulation, result = self.run_case(case, "fixed-known")
                reader = simulation.tx["reader"]
                self.assertEqual(reader.attempts, 1)
                self.assertGreater(reader.attempt.counters["capture_waits"], 0)
                if deadline == 20:
                    self.assertEqual(reader.state, "failed")
                    self.assertEqual(result["cohorts"]["reader"]["rejections"], {"read_wait_timeout": 1})
                    self.assertEqual(reader.attempt.writes, {})
                    self.assertEqual(simulation.store.head()["us/out"], 0)
                else:
                    self.assertEqual(reader.state, "complete")
                    self.assertEqual(simulation.store.head()["us/out"], 1)
                self.assertFalse(simulation.store.claims)
                self.assertFalse(simulation.admission.grants)

    def test_new_output_after_fixed_execution_aborts_without_growing_coverage(self):
        class GrowingOutput(FixedSimulation):
            def compute_values(self, tx):
                super().compute_values(tx)
                if self.is_fixed(tx):
                    tx.writes["eu/new"] = 7
                    tx.attempt.writes["eu/new"] = 7

        case = fixture("changed-final-footprint", {"eu/x": 0, "us/y": 0, "eu/new": 0}, [
            transaction("writer", 0, [], ["eu/x", "us/y"], "blind", value=9),
        ], link_delay=2)
        for policy in POLICIES:
            with self.subTest(policy=policy):
                simulation = GrowingOutput(deepcopy(case), policy)
                result = simulation.run()
                self.assertTrue(result["serial_check"])
                self.assertEqual(result["cohorts"]["writer"]["failed"], 1)
                self.assertEqual(result["cohorts"]["writer"]["rejections"],
                                 {"output_footprint_changed": 1})
                self.assertEqual(simulation.store.head(), case["initial"])
                self.assertFalse(simulation.store.claims)
                self.assertFalse(simulation.admission.grants)
                self.assertEqual(simulation.store.coverage[simulation.tx["writer"].attempt.owner],
                                 {"eu/x", "us/y"})

    def test_all_nineteen_shared_workloads_pass_serial_replay(self):
        offered = cases(seed=7, width=4)
        self.assertEqual(len(offered), 19)
        for case in offered:
            for policy in POLICIES:
                with self.subTest(case=case["name"], policy=policy):
                    simulation = FixedSimulation(deepcopy(case), policy)
                    result = simulation.run()
                    self.assertTrue(result["serial_check"])
                    self.assertEqual(result["certification"].get("renew_calls", 0), 0)
                    self.assertFalse(any(simulation.tx[owner].state in ("complete", "failed")
                                         for owner in simulation.admission.grants.values()))

    def test_exact_replay_includes_position_and_payload_messages(self):
        for policy in POLICIES:
            with self.subTest(policy=policy):
                first, first_result = self.run_case(hot_case(), policy)
                second, second_result = self.run_case(hot_case(), policy)
                self.assertEqual(first_result, second_result)
                self.assertEqual(first.trace, second.trace)
                self.assertEqual(first.store.trace, second.store.trace)


if __name__ == "__main__":
    unittest.main()
