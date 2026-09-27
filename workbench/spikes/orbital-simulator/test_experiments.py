import unittest

from protocol import audit, build_scenario
from reduce import minimize
from run import exercise
from sim import Actor, Host, World


class Work(Actor):
    def on(self, ctx, kind, data):
        if kind == "start":
            for i in range(2):
                assert ctx.compute(100, "done", {"i": i})
        elif kind == "done":
            ctx.note("done", op=str(data["i"]))


class ExperimentTests(unittest.TestCase):
    def test_pause_and_slowdown_affect_new_service_not_booked_work(self):
        world = World()
        world.add_host(Host("h", workers=1, handler_ns=0))
        world.add_actor("a", "h", Work)
        world.inject("a", "start")
        world.fault(20, "pause", host="h", resource="workers")
        world.fault(30, "slowdown", host="h", resource="workers", factor=3)
        world.run(until=150)
        self.assertEqual([r["time"] for r in world.trace if r["kind"] == "done"], [100])
        self.assertEqual(world.report()["resources"]["h/workers"]["queued"], 1)
        world.pause("h", "workers", False)
        world.run(until=450)
        self.assertEqual([r["time"] for r in world.trace if r["kind"] == "done"], [100, 450])

    def test_incident_reducer_keeps_failure_cause_and_removes_irrelevant_fault(self):
        faults = [dict(at=0, action="partition", source_host="source", target_host="copy_b"),
                  dict(at=0, action="partition", source_host="source", target_host="copy_c"),
                  dict(at=0, action="crash", actor="relay_a")]

        def fails(selected):
            world = build_scenario(negative="chosen-before-payload")
            for fault in selected:
                world.fault(**fault)
            world.run(until=800_000)
            return any("two durable payload domains" in v for v in audit(world)["violations"])

        reduced = minimize(faults, fails)
        self.assertEqual(reduced, faults[:2])
        self.assertTrue(fails(reduced))

    def test_runner_distinguishes_safe_unfinished_refusal_and_failed_negative_control(self):
        for name in ("admission-blocked-payload", "objects-refused", "negative-extension", "extension-match"):
            result = exercise(name)
            self.assertTrue(result["expectation_met"], result)
        unfinished = exercise("extension-missing")["observation"]
        self.assertTrue(unfinished["ok"])
        self.assertFalse(unfinished["complete"])


if __name__ == "__main__":
    unittest.main()
