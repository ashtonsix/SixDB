"""Independent adversarial checks for the composed checked-transaction probe.

Trace corruption deliberately challenges observers; callback injection challenges
recovery validation. Neither is claimed to be an ordinary modeled machine fault.
"""
from dataclasses import replace
import unittest

from demanding_case import Case, ContextShard, audit, build
from kernel import clone, digest
from sim import Actor


class PrivateReplies(Actor):
    def on(self, ctx, kind, data):
        if kind == "private-result":
            ctx.note("audit_private_reply", response=data)


class DemandingAuditTests(unittest.TestCase):
    def history(self, **options):
        case = Case(point_count=4, until_ns=5_000_000, **options)
        world, plans, until, _ = build(case, seed=7)
        world.run(until=until)
        self.assertFalse(audit(world, plans, case))
        return case, world, plans

    def test_private_snapshot_is_checked_before_any_checked_outcome_exists(self):
        case, world, plans = self.history(missing_checker=True)
        self.assertFalse(any(e["kind"] == "durable_write" and e["key"] == "outcome/1"
                             for e in world.trace))
        changed = False
        for event in world.trace:
            if event["kind"] == "checked_private_observation" and event["status"] == "ok":
                key = event["key"]
                event["values"][key] += 500
                changed = True
            elif event["kind"] == "checker_private_read":
                event["value"] += 500
        self.assertTrue(changed)
        self.assertTrue(any("snapshot" in error or "private value" in error
                            for error in audit(world, plans, case)))

    def test_equal_reports_cannot_license_unrelated_source_observations(self):
        case, world, plans = self.history()

        def corrupt_report(report):
            for key in report["observed"]:
                report["observed"][key] += 500
            for query in report["queries"]:
                query["value"] += 500
            report["effects"]["s0:answer"] += 500 * len(report["observed"])

        for event in world.trace:
            if event["kind"] != "durable_write":
                continue
            if event["actor"] in ("checker_a", "checker_b") and event["key"] == "report":
                corrupt_report(event["value"])
            elif event["key"] == "verification/1":
                for report in event["value"]["reports"].values():
                    corrupt_report(report)
        evidence = next(e["value"] for e in world.trace
                        if e["kind"] == "durable_write" and e["key"] == "verification/1")
        self.assertEqual(len({digest(r) for r in evidence["reports"].values()}), 1)
        self.assertTrue(audit(world, plans, case))

    def test_required_report_needs_its_own_durable_evidence(self):
        case, world, plans = self.history()
        report = next(e for e in world.trace if e["kind"] == "durable_write"
                      and e["actor"] == "checker_b" and e["key"] == "report")
        report["kind"] = "audit_removed_durable_report"
        self.assertTrue(any("durable checker report" in error for error in audit(world, plans, case)))

    def test_agreement_on_a_different_profile_does_not_change_the_agreed_job(self):
        case, world, plans = self.history()
        for event in world.trace:
            if event["kind"] == "checked_context_ready":
                event["context"]["profile"] = "other-runtime-profile"
            elif event["kind"] == "durable_write":
                if event["actor"] in ("checker_a", "checker_b") and event["key"] == "report":
                    event["value"]["context"]["profile"] = "other-runtime-profile"
                elif event["key"] == "verification/1":
                    evidence = event["value"]
                    evidence["context"]["profile"] = "other-runtime-profile"
                    for report in evidence["reports"].values():
                        report["context"]["profile"] = "other-runtime-profile"
        self.assertTrue(any("context" in error or "profile" in error
                            for error in audit(world, plans, case)))

    def test_recovered_outcome_cannot_bypass_its_checked_evidence(self):
        for field in ("verification", "observed", "values", "position"):
            with self.subTest(field=field):
                case, world, plans = self.history()
                host = world.actors["coordinator0"].host
                records = clone(world.durable(host, "coordinator0"))
                if field == "verification":
                    del records["verification/1"]
                elif field == "position":
                    records["outcome/1"][field] += 1
                else:
                    values = records["outcome/1"][field]
                    values[next(iter(values))] += 1
                start = len(world.trace)
                world.inject("coordinator0", "recovered", dict(ok=True, records=records),
                             at=world.kernel.now + 1)
                world.run(until=world.kernel.now + 100_000)
                self.assertTrue(any(e["kind"] == "checked_recovery_rejected"
                                    for e in world.trace[start:]))
                self.assertFalse(any(e["kind"] == "traffic_transition" and
                                     e["request"]["tx"] == 1 and e["request"]["kind"] == "resolve"
                                     for e in world.trace[start:]))

    def test_foreign_private_queries_are_rejected_without_ordering_changes(self):
        for replicated in (False, True):
            with self.subTest(replicated=replicated):
                case = Case(point_count=4, replicated=replicated, until_ns=5_000_000)
                world, plans, _, _ = build(case, seed=7)
                world.add_actor("audit-probe", "client", PrivateReplies)
                world.run(until=1_200_000)
                context = next(e["context"] for e in world.trace if e["kind"] == "checked_context_ready")
                endpoint = "consumer0_0" if replicated else "shard0"
                source = world.actors[endpoint].actor
                history = source.history if replicated else source
                before = digest(history.logical_state())
                for mutation in ({"invocation": "another-invocation"},
                                 {"position": context["position"] + 1}, {"key": "s0:answer"}):
                    request = dict(tx=1, key=context["keys"][0], position=context["position"],
                                   invocation=context["invocation"], reply="audit-probe")
                    request.update(mutation)
                    world.inject(endpoint, "private-read", request, at=world.kernel.now + 1)
                world.run(until=1_300_000)
                replies = [e["response"] for e in world.trace if e["kind"] == "audit_private_reply"]
                self.assertEqual(len(replies), 3)
                self.assertTrue(all(r["status"] == "rejected" and "values" not in r for r in replies))
                self.assertEqual(digest(history.logical_state()), before)
                self.assertFalse(audit(world, plans, case))

    def test_unfinished_predecessor_blocks_private_values_even_without_an_outcome(self):
        for replicated in (False, True):
            for ignore_pending in (False, True):
                with self.subTest(replicated=replicated, ignore_pending=ignore_pending):
                    case = Case(point_count=4, replicated=replicated, until_ns=2_500_000)
                    world, plans, until, _ = build(case, seed=7)
                    predecessor = dict(id=0, at=30_000, origin=0, cohort="predecessor",
                        program="set", reads=[], writes=["s0:k0"], value=777,
                        compute=5_000_000, source_version="integer-object-v1")
                    plans.append(predecessor)
                    world.inject("client", "offer", predecessor, at=predecessor["at"])
                    if ignore_pending:
                        # Exercise the actual private-read path, with a deliberate
                        # broken strategy. No finished predecessor outcome is
                        # available for a final-state-only oracle to notice.
                        for state in world.actors.values():
                            history = getattr(state.actor, "history", state.actor)
                            if isinstance(history, ContextShard):
                                history.strategy = replace(history.strategy, negative="ignore-pending")
                    world.run(until=until)
                    positions = {int(e["key"].split("/")[1]): e["value"]
                        for e in world.trace if e["kind"] == "durable_write"
                        and e["key"].startswith("position/")}
                    self.assertLess(positions[0], positions[1])
                    self.assertFalse(any(e["kind"] == "durable_write" and e["key"] == "outcome/0"
                                         for e in world.trace))
                    completed = {e["op"] for e in world.trace if e["kind"] == "traffic_response"}
                    self.assertTrue({2, 3, 4, 5}.issubset(completed))
                    self.assertEqual(1 in completed, ignore_pending)
                    errors = audit(world, plans, case)
                    if ignore_pending:
                        self.assertTrue(any("private query skipped pending predecessor 0" in error
                                            for error in errors), errors)
                    else:
                        self.assertFalse(errors)
                        self.assertTrue(any(e["kind"] == "checked_private_observation"
                            and e["status"] == "pending" and 0 in e.get("predecessors", [])
                            for e in world.trace))

    def test_source_restart_needs_its_own_rebuilt_context(self):
        for replicated in (False, True):
            with self.subTest(replicated=replicated):
                case, world, plans = self.history(replicated=replicated, source_reset=True)
                endpoint = "consumer0_1" if replicated else "shard0"
                self.assertTrue(any(e["kind"] == "checked_private_observation"
                    and e["actor"] == endpoint and e["incarnation"] == 2 and e["status"] == "ok"
                    for e in world.trace))
                removed = 0
                for event in world.trace:
                    if event.get("actor") != endpoint or event.get("incarnation") != 2:
                        continue
                    replayed = (event["kind"] == "replicated_transition"
                                and event["request"]["kind"] == "register-context")
                    recovered = event["kind"] == "traffic_recovered"
                    if replayed or recovered:
                        event["kind"] = "audit_removed_context_reconstruction"
                        removed += 1
                self.assertGreater(removed, 0)
                self.assertTrue(any(e["kind"] in ("traffic_transition", "replicated_transition")
                    and e["actor"] == endpoint and e["incarnation"] == 1
                    and e["request"]["kind"] == "register-context" for e in world.trace))
                self.assertTrue(any("private query before locally agreed context 1" in error
                                    for error in audit(world, plans, case)))


if __name__ == "__main__":
    unittest.main()
