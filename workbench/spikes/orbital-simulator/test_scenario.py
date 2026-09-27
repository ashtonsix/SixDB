import unittest

from scenario import Fault, Input, Scenario
from sim import Actor, Host, World


class Source(Actor):
    def on(self, ctx, kind, data):
        if kind == "boot":
            ctx.timer(100, "ready")
        elif kind == "ready":
            ctx.note("ready")
        elif kind == "work":
            ctx.note("received_work", value=data["value"], arrival=data["at"])


def fixture(replay=None):
    world = World(seed=7, ordering="shuffle", replay=replay)
    world.add_host(Host("host", handler_ns=1))
    world.add_actor("source", "host", Source)
    scenario = Scenario(world)
    payload = dict(value=17)
    scenario.when("ready-once", "ready", lambda e: True, [
        Fault(10, "crash", dict(actor="source")),
        Fault(20, "restart", dict(actor="source")),
        Input(30, "source", "work", payload, time_field="at"),
    ])
    payload["value"] = 999
    return world, scenario


class ScenarioTests(unittest.TestCase):
    def test_relative_incident_sequence_runs_once_and_replays(self):
        world, scenario = fixture()
        world.run(until=1000)
        scenario.require_all()
        at = scenario.fired["ready-once"]["time"]
        crashes = [e for e in world.trace if e["kind"] == "process_crash"]
        self.assertEqual(len(crashes), 1)
        self.assertEqual(crashes[0]["time"], at + 10)
        self.assertEqual(world.actors["source"].incarnation, 2)
        received = [e for e in world.trace if e["kind"] == "received_work"]
        self.assertEqual(len(received), 1)
        self.assertEqual((received[0]["value"], received[0]["arrival"]), (17, at + 30))
        replay, repeated = fixture(world.decisions)
        replay.run(until=1000)
        replay.kernel.check_replay()
        self.assertEqual(world.report(), replay.report())
        self.assertEqual(scenario.coverage(), repeated.coverage())

    def test_unreached_milestone_is_visible(self):
        world, scenario = fixture()
        scenario.when("absent", "never", lambda e: True, [])
        world.run(until=1000)
        self.assertEqual(scenario.coverage()["missing"], ["absent"])
        with self.assertRaisesRegex(ValueError, "absent"):
            scenario.require_all()

    def test_invalid_fault_fails_at_construction(self):
        world, scenario = fixture()
        for step in (Fault(-1, "crash", dict(actor="source")),
                     Fault(0, "crash", dict(actor="missing")),
                     Fault(0, "crash", dict(host="host")),
                     Input(0, "missing", "work", {})):
            with self.subTest(step=step), self.assertRaises((ValueError, TypeError)):
                scenario.when("invalid", "ready", lambda e: True, [step])


if __name__ == "__main__":
    unittest.main()
