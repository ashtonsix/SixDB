import unittest
from dataclasses import replace

from replicated_contention import Assembly, audit, build
from sim import Host, Link
from traffic import Config, Coordinator, Strategy


def dedicated(config, placement=None, factories=None, connected=True):
    """Test fixture explicitly prices every directed path to the added host."""
    names = ["client"] + [name for shard in range(config.shards)
        for name in (f"h{shard}", *(f"c{shard}_{i}" for i in range(3)), f"w{shard}_1", f"w{shard}_2")]
    host = Host("coordinator-home", domain="control-az", workers=1, control=1,
                handler_ns=config.handler_ns, memory_bytes=config.memory_bytes,
                durable_bytes=config.durable_bytes, disk_latency=config.disk_latency,
                disk_bytes_per_ns=2, nic_bytes_per_ns=config.bandwidth)
    links = tuple(Link(a, b, latency=500, bandwidth=2) for other in names
                  for a, b in ((other, host.name), (host.name, other))) if connected else ()
    return Assembly(placement=placement or {"coordinator0": host.name}, hosts=(host,), links=links,
                    factories=factories or {})


class PlacementTests(unittest.TestCase):
    def config(self, **changes):
        return replace(Config(count=6, shards=2, width=4, topology="lan", drain_ns=3_000_000), **changes)

    def test_empty_and_explicit_default_assembly_preserve_trace(self):
        config = self.config(count=2, drain_ns=1_000_000)
        default, _, until = build(config, seed=7)
        default.run(until=until)
        empty, _, _ = build(config, seed=7, assembly=Assembly())
        empty.run(until=until)
        placement = {name: actor.host for name, actor in default.actors.items()}
        explicit, _, _ = build(config, seed=7, assembly=Assembly(placement=placement))
        explicit.run(until=until)
        self.assertEqual(default.report(), empty.report())
        self.assertEqual(default.report(), explicit.report())

    def test_coordinator_device_reset_does_not_reset_leader_or_its_storage(self):
        config = self.config()
        world, _, until = build(config, seed=7, assembly=dedicated(config))
        world.when("durable_write", lambda e: e["actor"] == "coordinator0" and e["key"] == "outcome/1",
                   "power_loss", host="coordinator-home")
        world.fault(1_000_000, "power_on", host="coordinator-home")
        world.run(until=until)
        self.assertFalse(audit(world))
        self.assertEqual(6, sum(e["kind"] == "traffic_response" for e in world.trace))
        self.assertEqual(world.actors["coordinator0"].incarnation, 2)
        self.assertEqual(world.actors["shard0"].incarnation, 1)
        self.assertEqual(world.hosts["coordinator-home"].generation, 1)
        self.assertEqual(world.hosts["h0"].generation, 0)
        self.assertNotEqual(world.hosts["coordinator-home"].config.domain, world.hosts["h0"].config.domain)
        self.assertIn(("coordinator0", "outcome/1"), world.hosts["coordinator-home"].storage)
        self.assertNotIn(("coordinator0", "outcome/1"), world.hosts["h0"].storage)
        self.assertTrue(any(actor == "shard0" for actor, key in world.hosts["h0"].storage))
        self.assertFalse(any(e["kind"] == "process_crash" and e["actor"] == "shard0" for e in world.trace))
        self.assertTrue(any(e["kind"] == "wire_transmitted" and e["source"] == "coordinator-home"
                            for e in world.trace))

    def test_placement_changes_actual_worker_contention(self):
        for share in (False, True):
            with self.subTest(share=share):
                config = self.config()
                placement = {"coordinator0": "coordinator-home"}
                if share:
                    placement["coordinator1"] = "coordinator-home"
                world, _, until = build(config, assembly=dedicated(config, placement))
                world.fault(0, "pause", host="coordinator-home", resource="workers")
                world.fault(800_000, "pause", host="coordinator-home", resource="workers", paused=False)
                world.run(until=700_000)
                completed = {e["op"] for e in world.trace if e["kind"] == "traffic_response"}
                self.assertEqual(completed, set() if share else {2, 4, 6})
                pool = world.resources["coordinator-home/workers"]
                self.assertTrue(pool.paused)
                self.assertGreaterEqual(len(pool.queue), 2 if share else 1)
                self.assertGreater(world.resources["c0_0/workers"].completed, 0)
                world.run(until=until)
                self.assertFalse(audit(world))
                self.assertEqual(6, sum(e["kind"] == "traffic_response" for e in world.trace))

    def test_new_hosts_inherit_no_unstated_network_connectivity(self):
        config = self.config(count=2, drain_ns=600_000)
        world, _, until = build(config, assembly=dedicated(config, connected=False))
        world.run(until=until)
        self.assertEqual({e["op"] for e in world.trace if e["kind"] == "traffic_response"}, {2})
        self.assertTrue(any(e["kind"] == "packet_drop" and e["reason"] == "no_route" for e in world.trace))
        self.assertFalse(audit(world))

    def test_supplied_factory_is_used_on_boot_and_recovery(self):
        config, strategy = self.config(count=1), Strategy()
        class TaggedCoordinator(Coordinator):
            def on(self, ctx, kind, data):
                if kind == "boot":
                    ctx.note("selected_factory_boot")
                return super().on(ctx, kind, data)
        assembly = dedicated(config, factories={"coordinator0": lambda: TaggedCoordinator(strategy, config)})
        world, _, until = build(config, strategy, assembly=assembly)
        self.assertIsInstance(world.actors["coordinator0"].actor, TaggedCoordinator)
        world.fault(500_000, "power_loss", host="coordinator-home")
        world.fault(800_000, "power_on", host="coordinator-home")
        world.run(until=until)
        self.assertEqual([e["incarnation"] for e in world.trace if e["kind"] == "selected_factory_boot"], [1, 2])
        self.assertFalse(audit(world))

    def test_reused_assembly_does_not_share_mutable_physical_configuration(self):
        config = self.config(count=1)
        assembly = dedicated(config)
        first, _, _ = build(config, assembly=assembly)
        first.hosts["coordinator-home"].config.memory_bytes = 1
        first.links["client", "coordinator-home"].loss = 1
        second, _, _ = build(config, assembly=assembly)
        self.assertEqual(second.hosts["coordinator-home"].config.memory_bytes, config.memory_bytes)
        self.assertEqual(second.links["client", "coordinator-home"].loss, 0)

    def test_local_release_wrapper_composes_authored_work_and_factories(self):
        from local_release import LocalReleaseCoordinator, build as candidate
        from traffic import audit as standalone_audit
        config, strategy = self.config(count=1), Strategy()
        class TaggedLocalCoordinator(LocalReleaseCoordinator):
            def on(self, ctx, kind, data):
                if kind == "boot":
                    ctx.note("selected_local_factory_boot")
                return super().on(ctx, kind, data)
        for replicated in (False, True):
            with self.subTest(replicated=replicated):
                world, plans, until = candidate(config, strategy, replicated=replicated, offers=False,
                    assembly=dedicated(config) if replicated else None,
                    factories={"coordinator0": lambda: TaggedLocalCoordinator(strategy, config)})
                self.assertEqual(plans, [])
                plan = dict(id=1, at=100_000, origin=0, cohort="authored", program="set",
                            reads=[], writes=["s0:k0"], value=77, compute=2_000, source_version=1)
                plans.append(plan)
                world.inject("client", "offer", plan, at=plan["at"])
                world.run(until=until)
                self.assertEqual(1, sum(e["kind"] == "selected_local_factory_boot" for e in world.trace))
                self.assertEqual(1, sum(e["kind"] == "traffic_response" for e in world.trace))
                self.assertFalse(audit(world) if replicated else standalone_audit(world, plans, config))
                transitions = [e["request"]["kind"] for e in world.trace
                               if e["kind"] in ("traffic_transition", "replicated_transition")]
                self.assertNotIn("release", transitions)
                if replicated:
                    self.assertEqual(world.actors["coordinator0"].host, "coordinator-home")

    def test_invalid_construction_choices_are_rejected(self):
        cases = [Assembly(placement={"no-role": "h0"}),
                 Assembly(placement={"coordinator0": "no-host"}),
                 Assembly(hosts=(Host("h0"),)),
                 Assembly(hosts=(Host("new"), Host("new"))),
                 Assembly(links=(Link("h0", "h0"),)),
                 Assembly(links=(Link("h0", "client"),)),
                 Assembly(links=(Link("h0", "missing"),)),
                 Assembly(hosts=(Host("new"),), links=(Link("h0", "new"), Link("h0", "new"))),
                 Assembly(factories={"unknown": lambda: None}),
                 Assembly(factories={"coordinator0": None})]
        for assembly in cases:
            with self.subTest(assembly=assembly), self.assertRaises((ValueError, TypeError)):
                build(self.config(), assembly=assembly)


if __name__ == "__main__":
    unittest.main()
