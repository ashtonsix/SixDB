import unittest

from local_release import build
from sim import Actor
from traffic import Config, Strategy, audit
from replicated_contention import audit as replicated_audit


class LocalReleaseTests(unittest.TestCase):
    def test_local_successor_finishes_before_remote_fix_without_publishing_predecessor(self):
        class SnapshotProbe(Actor):
            def on(self, ctx, kind, data):
                if kind == "response":
                    ctx.note("snapshot_probe_reply", response=data)

        for replicated in (False, True):
            with self.subTest(replicated=replicated):
                config = Config(workload="transfer", count=1, shards=2, width=2,
                                topology="lan", drain_ns=4_000_000)
                world, plans, until = build(config, Strategy(supersede=True), seed=7,
                                            replicated=replicated)
                transition = "replicated_transition" if replicated else "traffic_transition"
                # All announcements can finish, but shard1 cannot persist its
                # subsequent fix. This delays actual remote fixing, not just ACKs.
                world.when(transition, lambda e:
                           e["shard"] == 1 and e["request"]["kind"] == "announce"
                           and e["request"]["tx"] == 1 and e.get("replica", 0) == 0,
                           "pause", host="h1", resource="disk_bytes")
                world.fault(1_500_000, "pause", host="h1", resource="disk_bytes", paused=False)
                successor = dict(id=2, at=500_000, origin=0, cohort="overwrite", program="set",
                                 reads=[], writes=["s0:k0"], value=9000, compute=2000,
                                 source_version=1)
                plans.append(successor)
                if replicated:
                    # The oracle's offered-plan registry is harness input only.
                    world.contention_offers = list(plans)
                world.inject("client", "offer", successor, at=successor["at"])
                world.add_actor("snapshot-probe", "client", SnapshotProbe)
                world.run(until=1_000_000)
                complete = [e["op"] for e in world.trace if e["kind"] == "traffic_response"]
                self.assertEqual(complete, [2])
                self.assertFalse(any(e["kind"] == "durable_write" and e["key"] == "outcome/1"
                                     for e in world.trace))
                self.assertFalse(any(e["kind"] == "traffic_read" and e["op"] == 1
                                     for e in world.trace))
                self.assertFalse(any(e["kind"] == transition and e["shard"] == 1
                                     and e["request"]["tx"] == 1 and e["request"]["kind"] == "fix"
                                     for e in world.trace))
                for event in world.trace:
                    if event["kind"] == "durable_write" and event["actor"] == "shard1":
                        requests = event["value"]["requests"] if replicated else event["value"]
                        self.assertFalse(any(r["kind"] == "fix" and r["tx"] == 1 for r in requests))
                positions = {e["op"]: e["position"] for e in world.trace
                             if e["kind"] == "traffic_position"}
                self.assertGreater(positions[2], positions[1])

                def probe(tx, position, at):
                    for shard in (0, 1):
                        world.inject(f"shard{shard}", "request",
                                     dict(request=f"{tx}/read/{shard}", kind="read", tx=tx,
                                          reply="snapshot-probe", position=position,
                                          keys=[f"s{shard}:k0"]), at=at)

                def answers(tx):
                    return {e["response"]["shard"]: e["response"]["values"] for e in world.trace
                            if e["kind"] == "snapshot_probe_reply" and e["response"]["tx"] == tx}

                def check_audit():
                    self.assertFalse(replicated_audit(world) if replicated else audit(world, plans, config))

                probe(900, positions[2], 1_000_001)
                world.run(until=1_400_000)
                # Complete replacement makes the local value readable. A read
                # spanning both shards still cannot finish: its remote half waits.
                self.assertEqual(answers(900), {0: {"s0:k0": 9000}})
                check_audit()
                world.run(until=until)
                self.assertEqual([e["op"] for e in world.trace if e["kind"] == "traffic_response"], [2, 1])
                self.assertEqual(answers(900), {0: {"s0:k0": 9000}, 1: {"s1:k0": 101}})
                check_audit()
                probe(901, positions[1], world.kernel.now + 1)
                world.run(until=world.kernel.now + 500_000)
                # The late predecessor installs at its original logical position;
                # it neither overwrites the newer replacement nor loses its old view.
                self.assertEqual(answers(901), {0: {"s0:k0": 99}, 1: {"s1:k0": 101}})
                check_audit()

    def test_candidate_preserves_snapshots_in_both_admission_bindings(self):
        for replicated in (False, True):
            for shape in ("transfer", "bulk-rmw", "max-update", "conditional", "overwrite"):
                with self.subTest(replicated=replicated, shape=shape):
                    config = Config(workload=shape, count=8, shards=2, width=4, topology="lan",
                                    slow_every=4, drain_ns=4_000_000)
                    world, plans, until = build(config, Strategy(supersede=True), seed=19, replicated=replicated)
                    world.run(until=until)
                    errors = replicated_audit(world) if replicated else audit(world, plans, config)
                    self.assertFalse(errors)
                    self.assertEqual(8, sum(e["kind"] == "traffic_response" for e in world.trace))
                    inputs = [e["request"]["kind"] for e in world.trace
                              if e["kind"] in ("traffic_transition", "replicated_transition")]
                    self.assertNotIn("release", inputs)

    def test_fix_before_callback_recovery_retains_local_release_and_global_execution_gate(self):
        for replicated in (False, True):
            config = Config(workload="transfer", count=4, shards=2, width=4, topology="lan", drain_ns=4_000_000)
            world, plans, until = build(config, seed=7, replicated=replicated)
            prefix = "log/" if replicated else "journal/"
            def is_fix(event):
                if event["actor"] != "shard0" or not event["key"].startswith(prefix):
                    return False
                batch = event["value"]["requests"] if replicated else event["value"]
                return any(request["kind"] == "fix" for request in batch)
            world.when("durable_write", is_fix, "power_loss", host="h0")
            world.fault(1_000_000, "power_on", host="h0")
            world.run(until=until)
            self.assertFalse(replicated_audit(world) if replicated else audit(world, plans, config))
            self.assertEqual(4, sum(e["kind"] == "traffic_response" for e in world.trace))
            self.assertTrue(any(e["kind"] == "power_loss" for e in world.trace))

    def test_lost_responses_and_replay(self):
        config = Config(workload="transfer", count=6, shards=2, width=4, topology="man",
                        loss=.04, duplicate=.3, drain_ns=12_000_000)
        world, plans, until = build(config, seed=31)
        world.run(until=until)
        self.assertFalse(audit(world, plans, config))
        self.assertEqual(6, sum(e["kind"] == "traffic_response" for e in world.trace))
        replay, _, _ = build(config, seed=31, replay=world.decisions)
        replay.run(until=until)
        replay.kernel.check_replay()
        self.assertEqual(world.report(), replay.report())


if __name__ == "__main__":
    unittest.main()
