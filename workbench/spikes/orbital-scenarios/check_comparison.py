#!/usr/bin/env python3
"""Bounded semantic checks for the shared contention comparison, not benchmarks."""

from copy import deepcopy
import heapq
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest

from arbitration import probe as arbitration_probe
from certification import Store, Version, probe as certification_probe
from comparison import POLICIES, Simulation
from priority_locks import probe as priority_probe, progress_probe
from write_admission import probes as admission_probes


def transaction(identifier, arrival, reads=(), writes=(), op="increment", **options):
    return {"id": identifier, "arrival": arrival, "group": identifier,
            "coordinator": "eu", "reads": list(reads), "writes": list(writes),
            "op": op, **options}


def fixture(name, initial, transactions, collections=None, **options):
    return {"name": name, "description": "Small authored regression history",
            "initial": dict(initial),
            "scopes": {**{key: [key] for key in initial}, **(collections or {})},
            "transactions": sorted(transactions, key=lambda tx: (tx["arrival"], tx["id"])),
            "horizon": 220, "capacity": 16, "link_delay": 2,
            "arbitration_delay": 4, "retry_delay": 3, "retry_limit": 1,
            "collect_period": 5, **options}


def maximum_case(backedge=False):
    return fixture("maximum-backedge" if backedge else "maximum-independent",
        {"eu/row": 1, "us/row": 2, "us/arrival": None, "eu/winner": None}, [
            transaction("maximum", 0, ["eu/rows", "us/rows"], ["eu/winner"],
                        "max", delay=40),
            transaction("arrival", 30, ["eu/winner"] if backedge else [],
                        ["us/arrival"], "blind", value=99, coordinator="us"),
        ], {"eu/rows": ["eu/row"], "us/rows": ["us/row", "us/arrival"]})


def bulk_case(mutate=False):
    other = (transaction("point", 30, ["us/y"], ["us/y"], coordinator="us")
             if mutate else transaction("report", 30, ["eu/x", "us/y"], [], "report"))
    return fixture("bulk-source-change" if mutate else "bulk-readers",
        {"eu/x": 0, "us/y": 0}, [
            transaction("bulk", 0, ["eu/x", "us/y"], ["eu/x", "us/y"], delay=40),
            other,
        ])


def reports_case(allow_old=False):
    initial = {f"{owner}/{index}": 0 for owner in ("eu", "us") for index in range(4)}
    collections = {owner: [key for key in initial if key.startswith(owner + "/")]
                   for owner in ("eu", "us")}
    offered = [transaction(f"writer{index}", 4 * index, [],
                           [f"eu/{index % 4}", f"us/{index % 4}"], "blind",
                           value=index + 1, group="writer") for index in range(6)]
    offered += [transaction(f"report{index}", 5 + 3 * index, ["eu", "us"], [],
                            "report", allow_old=allow_old, group="report")
                for index in range(10)]
    return fixture("reports-old" if allow_old else "reports-fresh", initial,
                   offered, collections, link_delay=8, retry_limit=2)


def drive_until(simulation, predicate):
    """Pause the real harness between authored events, without bypassing service."""
    for _ in range(1000):
        if predicate():
            return
        if not simulation.events:
            break
        when, _, fn, args = heapq.heappop(simulation.events)
        assert when <= simulation.horizon
        simulation.occupancy(when - simulation.time)
        simulation.time = when
        fn(*args)
    raise AssertionError("authored intermediate state was not reached")


class ComparisonChecks(unittest.TestCase):
    def simulate(self, case, policy):
        simulation = Simulation(deepcopy(case), policy)
        result = simulation.run()
        self.assertTrue(result["serial_check"])
        if simulation.new and all(tx.state != "committed" for tx in simulation.tx.values()):
            simulation.store.check_serial()
        return simulation, result

    def test_core_probes(self):
        self.assertTrue(arbitration_probe()["passed"])
        self.assertTrue(priority_probe()["passed"])
        self.assertTrue(admission_probes())
        results = certification_probe()
        self.assertGreaterEqual(len(results), 7)
        self.assertEqual(len({result["name"] for result in results}), len(results))

    def test_priority_oldest_remote_waiter_makes_progress(self):
        result = progress_probe()
        self.assertTrue(result["passed"])
        self.assertEqual(set(result["oldest_completion_ticks"]), {"wound-wait", "wait-die"})
        self.assertTrue(all(value <= 160 for value in result["oldest_completion_ticks"].values()))

    def test_disjoint_local_work_completes_under_both_policies(self):
        initial = {f"{owner}/{index}": 0 for owner in ("eu", "us") for index in range(6)}
        offered = [transaction(f"increment-{index}", index // 3, [key], [key],
                               coordinator=key.split("/")[0], group="point")
                   for index, key in enumerate(initial)]
        case = fixture("disjoint-points", initial, offered)
        for policy in POLICIES:
            with self.subTest(policy=policy):
                simulation, result = self.simulate(case, policy)
                self.assertEqual(result["cohorts"]["point"]["complete"], len(offered))
                self.assertEqual(result["cohorts"]["point"]["attempts"], len(offered))
                self.assertEqual(simulation.store.head() if simulation.new else simulation.data,
                                 dict.fromkeys(initial, 1))

    def test_hot_local_rmw_progresses_in_logical_order_for_both(self):
        case = fixture("hot-local", {"eu/hot": 0}, [
            transaction(f"increment-{index:02}", 0, ["eu/hot"], ["eu/hot"], group="hot")
            for index in range(20)], capacity=8)
        for policy in POLICIES:
            with self.subTest(policy=policy):
                simulation, result = self.simulate(case, policy)
                self.assertEqual(result["cohorts"]["hot"]["complete"], 20)
                self.assertEqual(result["cohorts"]["hot"]["attempts"], 20)
                ordered = sorted(simulation.decisions, key=lambda record: record["position"])
                self.assertEqual([record["reads"]["eu/hot"]["eu/hot"] for record in ordered],
                                 list(range(20)))
                self.assertEqual(ordered[-1]["writes"], {"eu/hot": 20})

    def test_old_maximum_can_commit_without_a_marker_backedge(self):
        for policy in ("arbitration", "certification"):
            with self.subTest(policy=policy):
                simulation, result = self.simulate(maximum_case(), policy)
                self.assertEqual(result["cohorts"]["maximum"]["complete"], 1)
                self.assertEqual(result["cohorts"]["arrival"]["complete"], 1)
                maximum = next(record for record in simulation.decisions if record["id"] == "maximum")
                self.assertIsNone(maximum["reads"]["us/rows"]["us/arrival"])
                self.assertEqual(maximum["writes"], {"eu/winner": 2})
                if simulation.new:
                    attempt = simulation.tx["maximum"].attempt
                    self.assertEqual(attempt.c, attempt.s)

    def test_marker_backedge_rejects_old_computation(self):
        simulation, result = self.simulate(maximum_case(backedge=True), "certification")
        self.assertEqual(result["cohorts"]["arrival"]["complete"], 1)
        self.assertEqual(result["cohorts"]["maximum"]["failed"], 1)
        self.assertEqual(result["cohorts"]["maximum"]["rejections"], {"source_changed": 1})
        self.assertIsNone(simulation.store.head()["eu/winner"])
        self.assertGreater(result["counts"]["discarded_compute_units"], 0)

    def test_reports_allow_bulk_promotion_without_recomputation(self):
        simulation, result = self.simulate(bulk_case(), "certification")
        bulk = simulation.tx["bulk"]
        self.assertEqual(bulk.attempts, 1)
        self.assertEqual(bulk.state, "complete")
        self.assertGreater(bulk.attempt.c, bulk.attempt.s)
        self.assertEqual(bulk.attempt.counters["captured_rows"], 2)
        self.assertEqual(bulk.attempt.renewed, {"eu/x", "us/y"})
        self.assertEqual(simulation.store.head(), {"eu/x": 1, "us/y": 1})
        control, control_result = self.simulate(bulk_case(), "fixed-position-control")
        self.assertEqual(control_result["cohorts"]["bulk"]["failed"], 1)
        self.assertEqual(control_result["cohorts"]["bulk"]["rejections"], {"fixed_position": 1})
        self.assertEqual(control.store.head(), {"eu/x": 0, "us/y": 0})

    def test_real_source_change_discards_entire_bulk_update(self):
        simulation, result = self.simulate(bulk_case(mutate=True), "certification")
        self.assertEqual(result["cohorts"]["bulk"]["failed"], 1)
        self.assertEqual(result["cohorts"]["point"]["complete"], 1)
        self.assertEqual(simulation.store.head(), {"eu/x": 0, "us/y": 1})
        self.assertEqual(simulation.tx["bulk"].attempt.installed, set())

    def test_fresh_and_allowed_old_reports_never_publish_mixed_pairs(self):
        for allow_old in (False, True):
            with self.subTest(allow_old=allow_old):
                simulation, result = self.simulate(reports_case(allow_old), "certification")
                self.assertGreater(result["cohorts"]["report"]["complete"], 0)
                for record in simulation.decisions:
                    if not record["id"].startswith("report"):
                        continue
                    for index in range(4):
                        self.assertEqual(record["reads"]["eu"][f"eu/{index}"],
                                         record["reads"]["us"][f"us/{index}"])
                    attempt = simulation.tx[record["id"]].attempt
                    self.assertEqual(attempt.c, attempt.s)
                if allow_old:
                    self.assertGreater(result["cohorts"]["report"]["historical_omitted_keys"], 0)
                else:
                    self.assertGreater(result["cohorts"]["report"]["rejections"].get("capture_promise", 0), 0)

    def test_exact_replay_including_metadata_and_across_hash_seeds(self):
        case = reports_case(allow_old=True)
        for policy in POLICIES:
            with self.subTest(policy=policy):
                first, first_result = self.simulate(case, policy)
                second, second_result = self.simulate(case, policy)
                self.assertEqual(first_result, second_result)
                self.assertEqual(first.trace, second.trace)
                self.assertEqual(first.store.trace, second.store.trace)
                self.assertEqual(first.store.R, second.store.R)
                self.assertEqual(first.store.W, second.store.W)
        script = (
            "import json,sys; from comparison import Simulation; "
            "case=json.load(sys.stdin); "
            "print(json.dumps([Simulation(case,p).run() for p in "
            f"{POLICIES!r}],sort_keys=True))"
        )
        outputs = []
        for seed in ("7", "83"):
            process = subprocess.run([sys.executable, "-c", script],
                input=json.dumps(case), capture_output=True, text=True, check=True,
                cwd=Path(__file__).resolve().parent,
                env={**os.environ, "PYTHONHASHSEED": seed}, timeout=20)
            outputs.append(json.loads(process.stdout))
        self.assertEqual(*outputs)

    def test_same_participant_rejection_does_not_claim_y(self):
        db = Store({"eu/x": 0, "eu/y": 0}, {"x": ("eu/x",), "y": ("eu/y",)})
        holder = db.begin("holder", (1, "holder"))
        holder.writes["eu/x"] = 1
        self.assertTrue(db.promise(holder, ("eu/x",)))
        both = db.begin("both", (2, "both"))
        both.writes.update({"eu/x": 2, "eu/y": 2})
        self.assertFalse(db.promise(both, ("eu/x", "eu/y")))
        self.assertEqual(set(db.claims), {"eu/x"})
        self.assertEqual(db.promises[both.owner], [])
        independent = db.begin("independent", (3, "independent"))
        independent.writes["eu/y"] = 3
        self.assertTrue(db.promise(independent, ("eu/y",)))

    def test_late_distributed_promise_survives_until_abort_delivery(self):
        db = Store({"eu/x": 0, "us/y": 0}, {"x": ("eu/x",), "y": ("us/y",)})
        holder = db.begin("holder", (1, "holder"))
        holder.writes["eu/x"] = 1
        self.assertTrue(db.promise(holder, ("eu/x",)))
        bridge = db.begin("bridge", (2, "bridge"))
        bridge.writes.update({"eu/x": 2, "us/y": 2})
        self.assertFalse(db.promise(bridge, ("eu/x",), in_flight=True))
        db.decide(bridge, False)
        # The original y request arrives before that participant sees abort.
        self.assertTrue(db.promise(bridge, ("us/y",), in_flight=True))
        self.assertEqual(bridge.decision, "abort")
        self.assertTrue(bridge.rejected)
        self.assertEqual(db.claims["us/y"].owner, "bridge")
        with self.assertRaises(AssertionError):
            db.choose(bridge)
        victim = db.begin("victim", (3, "victim"))
        victim.writes["us/y"] = 3
        self.assertFalse(db.promise(victim, ("us/y",)))
        db.release(bridge, ("us/y",))
        self.assertNotIn("us/y", db.claims)
        retry = db.begin("retry", (4, "retry"))
        retry.writes["us/y"] = 3
        self.assertTrue(db.promise(retry, ("us/y",)))

    def test_committed_duplicate_promise_never_reclaims_installed_output(self):
        db = Store({"eu/x": 0}, {"x": ("eu/x",)})
        attempt = db.begin("once", (1, "once"))
        attempt.writes["eu/x"] = 1
        self.assertTrue(db.promise(attempt, ("eu/x",)))
        db.choose(attempt)
        db.decide(attempt, True)
        db.install(attempt, ("eu/x",))
        self.assertTrue(db.promise(attempt, ("eu/x",), in_flight=True))
        self.assertFalse(db.claims)
        self.assertEqual(len(db.versions["eu/x"]), 2)
        with self.assertRaises(AssertionError):
            db.decide(attempt, False)

    def test_inflight_read_evidence_can_survive_abort_without_reviving_it(self):
        db = Store({"eu/x": 0, "us/y": 0}, {"x": ("eu/x",), "y": ("us/y",)})
        attempt = db.begin("late-read", (10, "late-read"))
        db.decide(attempt, False)
        self.assertTrue(db.capture(attempt, "x", in_flight=True))
        self.assertEqual(db.R["x"], attempt.s)
        self.assertEqual(attempt.decision, "abort")

        promoted = db.begin("promoted", (20, "promoted"))
        self.assertTrue(db.capture(promoted, "x"))
        promoted.writes["us/y"] = 1
        report = db.begin("report", (30, "report"))
        self.assertTrue(db.capture(report, "y"))
        db.decide(report, True)
        self.assertTrue(db.promise(promoted, ("us/y",)))
        position = db.choose(promoted)
        self.assertGreater(position, promoted.s)
        db.decide(promoted, False)
        self.assertTrue(db.renew(promoted, "x", in_flight=True))
        self.assertEqual(db.R["x"], position)
        self.assertEqual(promoted.c, position)
        self.assertEqual(promoted.decision, "abort")
        with self.assertRaises(AssertionError):
            db.decide(promoted, True)

    def test_waiting_capture_preserves_earlier_reads_until_writer_installs(self):
        db = Store({"eu/x": 0, "us/y": 0}, {"x": ("eu/x",), "y": ("us/y",)})
        holder = db.begin("holder", (5, "holder"))
        holder.writes["us/y"] = 7
        self.assertTrue(db.promise(holder, ("us/y",)))
        reader = db.begin("reader", (10, "reader"))
        self.assertTrue(db.capture(reader, "x", wait=True))
        before_y = db.R["y"]
        self.assertFalse(db.capture(reader, "y", wait=True))
        self.assertFalse(reader.rejected)
        self.assertEqual(reader.last_wait_reason, "capture_promise")
        self.assertEqual(reader.wait_blockers, ["holder"])
        self.assertEqual(db.R["y"], before_y)
        self.assertEqual(reader.reads, {"x": {"eu/x": 0}})

        later = db.begin("later", (20, "later"))
        later.writes["eu/x"] = 1
        self.assertTrue(db.promise(later, ("eu/x",)))
        db.choose(later)
        db.decide(later, True)
        db.install(later, ("eu/x",))
        db.choose(holder)
        db.decide(holder, True)
        db.install(holder, ("us/y",))
        self.assertTrue(db.capture(reader, "y", wait=True))
        self.assertIsNone(reader.last_wait_reason)
        self.assertEqual(reader.wait_blockers, [])
        self.assertEqual(reader.reads, {"x": {"eu/x": 0}, "y": {"us/y": 7}})
        self.assertEqual(reader.counters["captured_rows"], 2)
        self.assertEqual(reader.counters["capture_waits"], 1)
        db.decide(reader, True)
        self.assertEqual(reader.c, reader.s)
        self.assertEqual(db.check_serial(), {"eu/x": 1, "us/y": 7})

    def test_waiting_capture_uses_fixed_snapshot_if_writer_commits_after_it(self):
        db = Store({"eu/x": 0}, {"x": ("eu/x",)})
        writer = db.begin("writer", (5, "writer"))
        writer.writes["eu/x"] = 1
        self.assertTrue(db.promise(writer, ("eu/x",)))
        reader = db.begin("reader", (10, "reader"))
        self.assertFalse(db.capture(reader, "x", wait=True))
        db.choose(writer, minimum=(20, "writer"))
        db.decide(writer, True)
        db.install(writer, ("eu/x",))
        self.assertTrue(db.capture(reader, "x", wait=True))
        self.assertEqual(reader.reads["x"], {"eu/x": 0})
        db.decide(reader, True)
        db.check_serial()

    def test_default_capture_still_rejects_instead_of_waiting(self):
        db = Store({"eu/x": 0}, {"x": ("eu/x",)})
        writer = db.begin("writer", (1, "writer"))
        writer.writes["eu/x"] = 1
        self.assertTrue(db.promise(writer, ("eu/x",)))
        reader = db.begin("reader", (2, "reader"))
        self.assertFalse(db.capture(reader, "x"))
        self.assertTrue(reader.rejected)
        self.assertEqual(reader.rejection_reason, "capture_promise")
        self.assertIsNone(reader.last_wait_reason)
        self.assertEqual(reader.counters["capture_waits"], 0)
        self.assertEqual(reader.reads, {})

    def test_commit_minimum_is_owned_and_chosen_only_once(self):
        db = Store({"eu/x": 0}, {"x": ("eu/x",)})
        attempt = db.begin("writer", (1, "writer"))
        attempt.writes["eu/x"] = 1
        self.assertTrue(db.promise(attempt, ("eu/x",)))
        with self.assertRaises(AssertionError):
            db.choose(attempt, minimum=(2, "someone-else"))
        self.assertEqual(db.choose(attempt, minimum=(3, "writer")), (3, "writer"))
        self.assertEqual(db.choose(attempt, minimum=(2, "writer")), (3, "writer"))
        with self.assertRaises(AssertionError):
            db.choose(attempt, minimum=(4, "writer"))
        self.assertEqual(attempt.c, (3, "writer"))

    def test_forced_end_certification_rejects_changed_maximum_source(self):
        for force_end in (False, True):
            with self.subTest(force_end=force_end):
                db = Store({"eu/a": 10, "us/b": None, "eu/out": None},
                           {"a": ("eu/a",), "b": ("us/b",), "out": ("eu/out",),
                            "source": ("eu/a", "us/b")})
                maximum = db.begin("maximum", (10, "maximum"))
                self.assertTrue(db.capture(maximum, "source"))
                maximum.writes["eu/out"] = 10
                insert = db.begin("insert", (20, "insert"))
                insert.writes["us/b"] = 99
                self.assertTrue(db.promise(insert, ("us/b",)))
                db.choose(insert)
                db.decide(insert, True)
                db.install(insert, ("us/b",))
                self.assertTrue(db.promise(maximum, ("eu/out",)))
                db.choose(maximum, minimum=(30, "maximum") if force_end else None)
                if force_end:
                    self.assertFalse(db.renew(maximum, "source"))
                    self.assertEqual(maximum.rejection_reason, "source_changed")
                    db.decide(maximum, False)
                    db.release(maximum, ("eu/out",))
                    self.assertIsNone(db.head()["eu/out"])
                else:
                    self.assertEqual(maximum.c, maximum.s)
                    db.decide(maximum, True)
                    db.install(maximum, ("eu/out",))
                    self.assertEqual(db.head()["eu/out"], 10)
                db.check_serial()

    def test_harness_snapshot_wait_keeps_snapshot_and_respects_deadline(self):
        for wait_ticks in (4, 100):
            with self.subTest(wait_ticks=wait_ticks):
                case = fixture("snapshot-wait", {"eu/x": 0, "eu/y": 0, "us/z": 0}, [
                    transaction("holder", 0, [], ["eu/y", "us/z"], "blind", value=1),
                    transaction("report", 4, ["eu/x", "eu/y"], [], "report"),
                    transaction("later", 10, ["eu/x"], ["eu/x"]),
                ], link_delay=12, read_wait_ticks=wait_ticks)
                simulation, result = self.simulate(case, "snapshot-wait")
                report = simulation.tx["report"]
                self.assertEqual(report.attempts, 1)
                self.assertGreater(report.attempt.counters["capture_waits"], 0)
                self.assertEqual(simulation.store.promises[report.attempt.owner], [])
                self.assertEqual(report.attempt.reads["eu/x"], {"eu/x": 0})
                if wait_ticks == 4:
                    self.assertEqual(report.state, "failed")
                    self.assertEqual(result["cohorts"]["report"]["rejections"],
                                     {"read_wait_timeout": 1})
                    self.assertNotIn("eu/y", report.attempt.reads)
                else:
                    self.assertEqual(report.state, "complete")
                    self.assertEqual(report.attempt.reads["eu/y"], {"eu/y": 1})
                    self.assertEqual(report.attempt.c, report.attempt.s)
                    self.assertEqual(report.attempt.counters["captured_rows"], 2)

    def test_harness_end_certification_rejects_independent_source_change(self):
        simulation, result = self.simulate(maximum_case(), "occ")
        self.assertEqual(result["cohorts"]["arrival"]["complete"], 1)
        self.assertEqual(result["cohorts"]["maximum"]["failed"], 1)
        self.assertEqual(result["cohorts"]["maximum"]["rejections"], {"source_changed": 1})
        self.assertIsNone(simulation.store.head()["eu/winner"])

    def test_ordered_known_outputs_progress_and_replay_under_distributed_contention(self):
        offered = [transaction(f"hot{index}", index, ["eu/hot", "us/hot"],
                               ["eu/hot", "us/hot"], delay=8, group="hot",
                               coordinator="eu" if index % 2 == 0 else "us")
                   for index in range(4)]
        offered += [transaction(f"free{index}", 4 + 3 * index, ["eu/free"],
                                ["eu/free"], group="free") for index in range(6)]
        case = fixture("ordered-known-outputs",
                       {"eu/hot": 0, "us/hot": 0, "eu/free": 0}, offered, horizon=500)
        simulation, result = self.simulate(case, "ordered-writes")
        self.assertEqual(result["cohorts"]["hot"]["complete"], 4)
        self.assertEqual(result["cohorts"]["hot"]["attempts"], 4)
        self.assertEqual(result["cohorts"]["free"]["complete"], 6)
        self.assertEqual(simulation.store.head(), {"eu/hot": 4, "us/hot": 4, "eu/free": 6})
        self.assertGreater(result["write_admission"]["blocked_requests"], 0)
        for index in range(4):
            admissions = [event["owner"] for event in simulation.trace
                          if event["event"] == "write_admission" and event["id"] == f"hot{index}"]
            # Canonical participant order also applies to US coordinators.
            self.assertEqual(admissions, ["eu", "us"])
        first_hot_commit = min(record["time"] for record in simulation.decisions
                               if record["id"].startswith("hot"))
        self.assertLess(simulation.tx["free0"].completion, first_hot_commit)
        self.assertFalse(simulation.admission.grants)
        self.assertFalse(simulation.admission.waiting)
        replay, replay_result = self.simulate(case, "ordered-writes")
        self.assertEqual(result, replay_result)
        self.assertEqual(simulation.trace, replay.trace)
        self.assertEqual(simulation.store.trace, replay.store.trace)

    def test_ordered_broad_waiter_convoys_covered_point_but_not_unrelated_work(self):
        rows = [f"eu/r{index}" for index in range(8)]
        initial = {**dict.fromkeys(rows, 0), "us/remote": 0, "eu/free": 0}
        case = fixture("ordered-broad-convoy", initial, [
            transaction("holder", 0, [], ["eu/r0", "us/remote"], "blind", value=1, delay=20),
            transaction("bulk", 4, [], rows, "blind", value=10),
            transaction("narrow", 6, ["eu/r7"], ["eu/r7"]),
            transaction("free", 8, ["eu/free"], ["eu/free"]),
        ], link_delay=4, horizon=300)
        simulation = Simulation(deepcopy(case), "ordered-writes")
        drive_until(simulation, lambda: {"bulk", "narrow"} <= simulation.admission.waiting.keys())
        self.assertEqual(simulation.admission.waiting["bulk"], frozenset(rows))
        self.assertNotIn("eu/r7", simulation.admission.grants)
        self.assertNotIn("eu/r7", simulation.store.claims)
        self.assertNotIn("bulk", simulation.admission.grants.values())
        self.assertIsNone(simulation.tx["bulk"].attempt)
        self.assertGreater(simulation.admission.counters["blocked_by_older_waiter"], 0)
        result = simulation.run()
        self.assertTrue(result["serial_check"])
        self.assertTrue(all(tx.state == "complete" for tx in simulation.tx.values()))
        decisions = {record["id"]: record for record in simulation.decisions}
        self.assertLess(simulation.tx["free"].completion, decisions["holder"]["time"])
        self.assertLess(decisions["bulk"]["position"], decisions["narrow"]["position"])
        self.assertEqual(simulation.store.head(),
                         {**dict.fromkeys(rows, 10), "eu/r7": 11, "us/remote": 1, "eu/free": 1})
        self.assertFalse(simulation.admission.grants)
        self.assertFalse(simulation.admission.waiting)
        simulation.store.check_serial()

    def test_oracle_requires_every_declared_source_scope(self):
        case = fixture("source-coverage", {"eu/guard": 0, "eu/out": 0}, [
            transaction("guarded", 0, ["eu/guard"], ["eu/out"], "blind", value=1)])
        for policy in ("arbitration", "certification"):
            with self.subTest(policy=policy):
                simulation, _ = self.simulate(case, policy)
                del simulation.decisions[0]["reads"]["eu/guard"]
                with self.assertRaises(AssertionError):
                    simulation.check_serial()

    def test_oracle_checks_installed_keys_during_partial_commit(self):
        case = fixture("partial-oracle", {"eu/x": 0, "us/y": 0}, [
            transaction("writer", 0, [], ["eu/x", "us/y"], "blind", value=1)], link_delay=8)
        for policy in ("arbitration", "certification"):
            with self.subTest(policy=policy):
                simulation = Simulation(deepcopy(case), policy)
                drive_until(simulation, lambda: simulation.tx["writer"].state == "committed"
                    and any(event["event"] == "install" and event.get("owner") == "eu"
                            for event in simulation.trace))
                simulation.check_serial()
                if simulation.new:
                    version = simulation.store.versions["eu/x"][-1]
                    simulation.store.versions["eu/x"][-1] = Version(version.position, 99)
                else:
                    simulation.data["eu/x"] = 99
                with self.assertRaises(AssertionError):
                    simulation.check_serial()


if __name__ == "__main__":
    unittest.main()
