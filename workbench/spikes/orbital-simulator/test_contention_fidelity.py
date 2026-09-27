"""Adversarial port histories for the brief's contention lifecycle.

These examples inspect replies and durable events, not Shard's private state.
The fixture supplies agreed request order at a prepared single authority; it
does not turn these tests into a replicated-epoch or consensus claim.
"""
import unittest

from sim import Actor, Host, World
from traffic import Config, Coordinator, Shard, Strategy, audit, build


class Answers(Actor):
    def on(self, ctx, kind, data):
        if kind in ("response", "done"):
            ctx.note("fidelity_answer", message_kind=kind, response=data)


class DelayedFixAck(Shard):
    """Delay a durable reply, without changing the authority's transitions."""
    delayed_kind = "fix"

    def reply(self, ctx, request, result):
        if request["kind"] == self.delayed_kind:
            ctx.timer(20_000, "fidelity_delayed_ack", dict(request=request, result=result))
        else:
            super().reply(ctx, request, result)

    def on(self, ctx, kind, data):
        if kind == "fidelity_delayed_ack":
            super().reply(ctx, data["request"], data["result"])
        else:
            super().on(ctx, kind, data)


class DelayedAcquireAck(DelayedFixAck):
    delayed_kind = "acquire"


class ObservedCoordinator(Coordinator):
    def on(self, ctx, kind, data):
        if kind == "response" and data["kind"] == "fix":
            ctx.note("fidelity_fix_ack_received", op=data["tx"], shard=data["shard"])
        super().on(ctx, kind, data)


class ContentionFidelityTests(unittest.TestCase):
    def make_shard(self, seed=1):
        world = World(seed=seed, ordering="shuffle")
        world.add_host(Host("h", handler_ns=1, disk_latency=10,
                            nic_bytes_per_ns=100, disk_bytes_per_ns=100))
        initial = {"s0:x": 10, "s0:y": 20, "s0:z": 30, "s0:w": 40}
        strategy = Strategy(batch=8, batch_ns=1, retry_ns=500,
                            backoff=False, jitter=False)
        world.add_actor("shard0", "h", lambda: Shard(0, initial, strategy, 0))
        world.add_actor("sink", "h", Answers)
        world.run(until=1000)
        return world

    def make_coordinator(self, remote_type):
        world = World(seed=7, ordering="shuffle")
        world.add_host(Host("h", handler_ns=1, disk_latency=10,
                            nic_bytes_per_ns=100, disk_bytes_per_ns=100))
        initial = {"s0:x": 10, "s1:y": 20}
        strategy = Strategy(batch_ns=1, retry_ns=500, backoff=False, jitter=False)
        world.add_actor("shard0", "h", lambda: Shard(0, initial, strategy, 0))
        world.add_actor("shard1", "h", lambda: remote_type(1, initial, strategy, 0))
        world.add_actor("coordinator0", "h", lambda: ObservedCoordinator(strategy, Config()))
        world.add_actor("client", "h", Answers)
        world.add_actor("sink", "h", Answers)
        plan = dict(id=1, at=1000, origin=0, cohort="test", program="set",
                    reads=[], writes=list(initial), value=77, compute=100,
                    source_version=1)
        world.inject("coordinator0", "submit", plan, at=1000)
        return world

    def request(self, world, tx, kind, **fields):
        identity = fields.pop("identity", f"{tx}/{kind}/0")
        world.inject("shard0", "request", dict(tx=tx, kind=kind,
                     request=identity, reply="sink", **fields), at=world.kernel.now)

    def step(self, world, duration=1000):
        world.run(until=world.kernel.now + duration)

    def replies(self, world, kind=None):
        replies = [e["response"] for e in world.trace
                   if e["kind"] == "fidelity_answer"]
        return [r for r in replies if kind is None or r.get("kind") == kind]

    def reply(self, world, tx, kind):
        return next((r for r in self.replies(world, kind) if r["tx"] == tx), None)

    def announce(self, world, tx):
        self.request(world, tx, "announce")
        self.step(world)
        reply = self.reply(world, tx, "announce")
        self.assertIsNotNone(reply)
        return reply["minimum"]

    def fix_and_release(self, world, tx, position):
        self.request(world, tx, "fix", position=position)
        self.step(world)
        self.request(world, tx, "release")
        self.step(world)

    def test_partial_reservation_does_not_delay_existing_or_new_read_cut(self):
        world = self.make_shard()
        self.request(world, 1, "acquire", locks=["s0:x"], writes=["s0:x"])
        self.step(world)
        self.assertIsNotNone(self.reply(world, 1, "acquire"))
        # The writer has not obtained its other shard or announced a minimum.
        self.request(world, 2, "read", keys=["s0:x"], position=0)
        self.request(world, 3, "read", keys=["s0:x"], position=50)
        self.step(world)
        self.assertEqual({r["tx"]: r["values"] for r in self.replies(world, "read")},
                         {2: {"s0:x": 10}, 3: {"s0:x": 10}})

    def test_waiter_overlap_chain_prevents_overtaking_but_not_disjoint_work(self):
        for seed in (1, 7, 19):
            with self.subTest(seed=seed):
                world = self.make_shard(seed)
                self.request(world, 1, "acquire", locks=["s0:x"], writes=["s0:x"])
                self.step(world)
                self.request(world, 2, "acquire", locks=["s0:x", "s0:y"], writes=["s0:x", "s0:y"])
                self.step(world)
                self.request(world, 3, "acquire", locks=["s0:y"], writes=["s0:y"])
                self.request(world, 4, "acquire", locks=["s0:z"], writes=["s0:z"])
                self.step(world)
                granted = {r["tx"] for r in self.replies(world, "acquire")}
                self.assertEqual(granted, {1, 4})

    def test_announcement_refreshes_floor_after_reads_during_partial_reservation(self):
        world = self.make_shard()
        self.request(world, 1, "acquire", locks=["s0:x"], writes=["s0:x"])
        self.step(world)
        self.request(world, 2, "read", keys=["s0:x"], position=50)
        self.step(world)
        self.assertIsNotNone(self.reply(world, 2, "read"))
        minimum = self.announce(world, 1)
        self.assertGreater(minimum, 50)
        self.fix_and_release(world, 1, minimum)
        self.request(world, 1, "resolve", values={"s0:x": 11})
        self.step(world)
        self.request(world, 3, "read", keys=["s0:x"], position=50)
        self.request(world, 4, "read", keys=["s0:x"], position=minimum)
        self.step(world)
        self.assertEqual(self.reply(world, 3, "read")["values"], {"s0:x": 10})
        self.assertEqual(self.reply(world, 4, "read")["values"], {"s0:x": 11})

    def test_waiter_protection_follows_overlapping_chain(self):
        world = self.make_shard()
        for tx, keys in ((1, ["s0:x"]), (2, ["s0:x", "s0:y"]),
                         (3, ["s0:y", "s0:z"]), (4, ["s0:z"]), (5, ["s0:w"])):
            self.request(world, tx, "acquire", locks=keys, writes=keys)
            self.step(world)
        self.assertEqual({r["tx"] for r in self.replies(world, "acquire")}, {1, 5})
        for tx, position, next_tx in ((1, 10, 2), (2, 20, 3), (3, 30, 4)):
            self.announce(world, tx)
            self.fix_and_release(world, tx, position)
            self.assertIsNotNone(self.reply(world, next_tx, "acquire"))
            if next_tx < 4:
                self.assertIsNone(self.reply(world, next_tx + 1, "acquire"))

    def test_only_announced_interval_blocks_and_fixing_position_narrows_it(self):
        world = self.make_shard()
        self.request(world, 1, "acquire", locks=["s0:x"], writes=["s0:x"])
        self.step(world)
        minimum = self.announce(world, 1)
        position = minimum + 20
        self.request(world, 2, "read", keys=["s0:x"], position=minimum - 1)
        self.request(world, 3, "read", keys=["s0:x"], position=minimum)
        self.request(world, 4, "read", keys=["s0:x"], position=position)
        self.step(world)
        self.assertIsNotNone(self.reply(world, 2, "read"))
        self.assertIsNone(self.reply(world, 3, "read"))
        self.assertIsNone(self.reply(world, 4, "read"))
        self.request(world, 1, "fix", position=position)
        self.step(world)
        self.assertEqual(self.reply(world, 3, "read")["values"], {"s0:x": 10})
        self.assertIsNone(self.reply(world, 4, "read"))
        self.request(world, 1, "release")
        self.step(world)
        self.request(world, 1, "resolve", values={"s0:x": 11})
        self.step(world)
        self.assertEqual(self.reply(world, 4, "read")["values"], {"s0:x": 11})

    def test_waiting_read_registers_bound_before_later_writer_is_announced(self):
        world = self.make_shard()
        self.request(world, 1, "acquire", locks=["s0:x"], writes=["s0:x"])
        self.step(world)
        self.announce(world, 1)
        self.fix_and_release(world, 1, 10)
        self.request(world, 2, "read", keys=["s0:x"], position=50)
        self.step(world)
        self.assertIsNone(self.reply(world, 2, "read"))
        self.request(world, 3, "acquire", locks=["s0:x"], writes=["s0:x"])
        self.step(world)
        minimum = self.announce(world, 3)
        self.assertGreater(minimum, 50)
        self.fix_and_release(world, 3, minimum)
        self.request(world, 3, "resolve", values={"s0:x": 99})
        self.step(world)
        self.assertIsNone(self.reply(world, 2, "read"))
        self.request(world, 1, "resolve", values={"s0:x": 42})
        self.step(world)
        self.assertEqual(self.reply(world, 2, "read")["values"], {"s0:x": 42})

    def test_waiter_order_survives_power_loss_before_any_grant(self):
        world = self.make_shard()
        self.request(world, 1, "acquire", locks=["s0:x"], writes=["s0:x"])
        self.step(world)
        self.request(world, 2, "acquire", locks=["s0:x", "s0:y"], writes=["s0:x", "s0:y"])
        self.step(world)
        self.assertIsNone(self.reply(world, 2, "acquire"))
        self.assertTrue(any(e["kind"] == "durable_write" and
                            e["actor"] == "shard0" and
                            any(r["tx"] == 2 and r["kind"] == "acquire" for r in e["value"])
                            for e in world.trace))
        world.fault(world.kernel.now, "power_loss", host="h")
        world.fault(world.kernel.now + 100, "power_on", host="h")
        self.step(world)
        self.request(world, 3, "acquire", locks=["s0:y"], writes=["s0:y"])
        self.request(world, 4, "acquire", locks=["s0:z"], writes=["s0:z"])
        self.step(world)
        self.assertIsNone(self.reply(world, 3, "acquire"))
        self.assertIsNotNone(self.reply(world, 4, "acquire"))
        self.announce(world, 1)
        self.fix_and_release(world, 1, 10)
        self.assertIsNotNone(self.reply(world, 2, "acquire"))
        self.assertIsNone(self.reply(world, 3, "acquire"))
        self.announce(world, 2)
        self.fix_and_release(world, 2, 20)
        self.assertIsNotNone(self.reply(world, 3, "acquire"))

    def test_waiting_read_bound_survives_power_loss(self):
        world = self.make_shard()
        self.request(world, 1, "acquire", locks=["s0:x"], writes=["s0:x"])
        self.step(world)
        self.announce(world, 1)
        self.fix_and_release(world, 1, 10)
        self.request(world, 2, "read", keys=["s0:x"], position=50)
        self.step(world)
        self.assertIsNone(self.reply(world, 2, "read"))
        world.fault(world.kernel.now, "power_loss", host="h")
        world.fault(world.kernel.now + 100, "power_on", host="h")
        self.step(world)
        self.request(world, 3, "acquire", locks=["s0:x"], writes=["s0:x"])
        self.step(world)
        self.assertGreater(self.announce(world, 3), 50)
        self.request(world, 1, "resolve", values={"s0:x": 42})
        self.step(world)
        self.assertEqual(self.reply(world, 2, "read")["values"], {"s0:x": 42})

    def test_reservations_release_only_after_all_fix_acknowledgements(self):
        world = self.make_coordinator(DelayedFixAck)
        world.run(until=10_000)
        fixed = {e["shard"] for e in world.trace if e["kind"] == "traffic_transition"
                 and e["transition"] == "fix" and e["op"] == 1}
        self.assertEqual(fixed, {0, 1})
        self.request(world, 2, "acquire", locks=["s0:x"], writes=["s0:x"])
        world.run(until=20_000)
        self.assertIsNone(self.reply(world, 2, "acquire"))
        world.run(until=40_000)
        self.assertIsNotNone(self.reply(world, 2, "acquire"))
        first_ack = {}
        for event in world.trace:
            if event["kind"] == "fidelity_fix_ack_received" and event["op"] == 1:
                first_ack.setdefault(event["shard"], event["time"])
        releases = [e["time"] for e in world.trace if e["kind"] == "traffic_transition"
                    and e["transition"] == "release" and e["op"] == 1]
        self.assertEqual(set(first_ack), {0, 1})
        self.assertEqual(len(releases), 2)
        self.assertGreater(min(releases), max(first_ack.values()))

    def test_coordinator_does_not_announce_until_all_reservations_are_known(self):
        world = self.make_coordinator(DelayedAcquireAck)
        world.run(until=10_000)
        self.request(world, 2, "read", keys=["s0:x"], position=500_000)
        world.run(until=20_000)
        self.assertEqual(self.reply(world, 2, "read")["values"], {"s0:x": 10})
        self.assertFalse(any(e["kind"] == "traffic_transition" and e["op"] == 1
                             and e["transition"] == "announce" for e in world.trace))
        world.run(until=40_000)
        positions = [e["position"] for e in world.trace
                     if e["kind"] == "traffic_position" and e["op"] == 1]
        self.assertEqual(len(positions), 1)
        self.assertGreater(positions[0], 500_000)
        self.assertTrue(any(e["kind"] == "fidelity_answer"
                            and e["message_kind"] == "done" for e in world.trace))

    def test_journaled_lifecycle_survives_power_cut_before_callback(self):
        for victim in ("h0", "h1"):
            for phase in ("acquire", "announce", "fix", "release", "read", "resolve"):
                with self.subTest(victim=victim, phase=phase):
                    config = Config(workload="transfer", count=1, shards=2,
                                    topology="lan", drain_ns=4_000_000)
                    world, plans, _ = build(config, seed=19)
                    shard = "shard" + victim[1:]
                    world.when("durable_write", lambda e, shard=shard, phase=phase:
                               e["actor"] == shard and e["key"].startswith("journal/")
                               and any(r["kind"] == phase for r in e["value"]),
                               "power_loss", host=victim)
                    world.fault(1_000_000, "power_on", host=victim)
                    world.run(until=4_000_000)
                    self.assertTrue(any(e["kind"] == "power_loss" and e["host"] == victim
                                        for e in world.trace))
                    self.assertFalse(audit(world, plans, config))
                    self.assertEqual(1, sum(e["kind"] == "traffic_response" for e in world.trace))


if __name__ == "__main__":
    unittest.main()
