"""Application oracles and adverse histories for the actor dataflow composition."""
from dataclasses import replace
import unittest

from dataflow_scenario import FlowConfig, build, manifest, oracle, summarize


def events(world, kind):
    return [row for row in world.trace if row["kind"] == kind]


class DataflowTests(unittest.TestCase):
    def assert_complete(self, world, config):
        summary = summarize(world, config)
        self.assertEqual(summary["materializations"], 2)
        self.assertTrue(summary["correct"])
        self.assertEqual(summary["finished_sources"], 2)
        self.assertEqual(summary["foreground"]["completed"], config.foreground_count)
        self.assertEqual(summary["foreground"]["refused"], 0)
        self.assertEqual(summary["waits"], [])
        for actor in ("sink0", "sink1"):
            host = world.actors[actor].host
            self.assertEqual(world.durable(host, actor)["materialization"], oracle(config))
        return summary

    def test_placements_and_enhancement_preserve_exact_coverage(self):
        results = {}
        for placement in ("source", "destination", "consumer"):
            for enhance in (False, True):
                config = FlowConfig(placement=placement, enhance=enhance)
                world = build(config)
                world.run(until=1_000_000)
                results[placement, enhance] = self.assert_complete(world, config)
        self.assertLess(results["destination", True]["wan_bytes"],
                        results["source", True]["wan_bytes"])
        for placement in ("source", "destination", "consumer"):
            self.assertLess(results[placement, True]["worker_ns"],
                            results[placement, False]["worker_ns"])
        self.assertGreater(results["consumer", True]["foreground"]["max_ns"],
                           results["destination", True]["foreground"]["max_ns"])

    def test_enhancement_cost_can_reverse_when_computation_is_cheap(self):
        results = []
        for enhance in (False, True):
            config = FlowConfig(enhance=enhance, filter_ns_per_row=0,
                                consume_ns_per_row=0, wan_bytes_per_ns=.02,
                                retry_ns=2_000_000, foreground_count=0)
            world = build(config)
            world.run(until=2_000_000)
            results.append(self.assert_complete(world, config))
        self.assertGreater(results[1]["wan_bytes"], results[0]["wan_bytes"])
        self.assertGreater(max(results[1]["materialized_at"].values()),
                           max(results[0]["materialized_at"].values()))

    def test_delayed_permit_changes_latency_not_result(self):
        config = FlowConfig(permit_delay_ns=40_000)
        world = build(config)
        world.run(until=40_000)
        self.assertEqual(events(world, "flow_relay_ready"), [])
        self.assertEqual(events(world, "flow_materialized"), [])
        self.assertTrue(any(row.get("reason") == "relay_join_inputs" for row in world.waits.values()))
        world.run(until=1_000_000)
        self.assert_complete(world, config)

    def test_duplicate_and_simultaneous_orders_do_not_duplicate_reduction(self):
        for seed in range(16):
            config = FlowConfig(seed=seed, ordering="shuffle", duplicate=.8,
                                chunks=3, rows=13, window=3)
            world = build(config)
            world.run(until=1_000_000)
            self.assert_complete(world, config)
            for actor in ("sink0", "sink1"):
                receipts = [k for k in world.durable(world.actors[actor].host, actor)
                            if k.startswith("record/")]
                self.assertEqual(len(receipts), 2 * config.chunks)

    def test_crash_after_durable_receipt_before_application_ack_recovers(self):
        config = FlowConfig()
        world = build(config)
        world.when("durable_write",
                   lambda row: row["actor"] == "sink0" and row["key"].startswith("record/"),
                   "crash", actor="sink0")
        world.run(until=100_000)
        self.assertFalse(world.actors["sink0"].live)
        self.assertTrue(world.durable("c0", "sink0"))
        self.assertFalse(any(row["actor"] == "sink0" for row in events(world, "flow_receipt")))
        self.assertEqual(summarize(world, config)["finished_sources"], 0)
        world.restart("sink0")
        world.run(until=1_000_000)
        self.assert_complete(world, config)
        self.assertTrue(events(world, "flow_duplicate"))

    def test_lost_ack_route_retries_durable_contribution(self):
        config = FlowConfig()
        world = build(config)
        world.partition("c0", "p0", True)
        world.run(until=100_000)
        self.assertLess(summarize(world, config)["finished_sources"], 2)
        self.assertTrue(events(world, "packet_drop"))
        world.partition("c0", "p0", False)
        world.run(until=1_000_000)
        self.assert_complete(world, config)
        self.assertTrue(events(world, "flow_duplicate"))

    def test_cold_restart_recovers_materialization_from_actual_storage(self):
        config = FlowConfig(foreground_count=0)
        world = build(config)
        world.run(until=1_000_000)
        self.assert_complete(world, config)
        world.power_loss("c0")
        world.power_on("c0")
        world.run(until=1_200_000)
        self.assert_complete(world, config)
        self.assertTrue(any(r["actor"] == "sink0" and r["recovered"]
                            for r in events(world, "flow_materialized")))
        self.assertEqual(world.hosts["c0"].used, 128)

    def test_source_and_relay_restart_use_persisted_manifest(self):
        config = FlowConfig()
        world = build(config)
        world.run(until=9_000)
        world.crash("source0")
        world.crash("relay0")
        world.run(until=20_000)
        world.restart("source0")
        world.restart("relay0")
        world.run(until=1_000_000)
        self.assert_complete(world, config)
        self.assertGreater(sum(row["kind"] == "disk_submit" and row.get("actor") == "source0"
                               and row.get("mode") == "read" for row in world.trace), 1)

    def test_materialization_needs_its_own_capacity_after_every_source_is_acked(self):
        config = FlowConfig()
        world = build(config)
        world.hosts["c0"].config.durable_bytes = 2 * config.chunks * 96
        world.run(until=1_000_000)
        result = summarize(world, config)
        self.assertEqual(result["finished_sources"], 2)
        self.assertEqual(result["materializations"], 1)
        self.assertNotIn("materialization", world.durable("c0", "sink0"))
        self.assertTrue(any(w["reason"] == "materialization_capacity" for w in result["waits"]))
        world.hosts["c0"].config.durable_bytes += 128
        world.run(until=1_200_000)
        self.assert_complete(world, config)

    def test_missing_middle_partition_does_not_materialize_from_later_sequence(self):
        config = FlowConfig(chunks=3, rows=4, foreground_count=0)
        world = build(config, offer=False)
        messages = []
        for source in ("source0", "source1"):
            data = manifest(config, source)
            for seq, rows in enumerate(data["chunks"]):
                messages.append(dict(job=data["job"], source=source,
                                     snapshot=data["snapshot"], code=data["code"],
                                     seq=seq, total=config.chunks, rows=rows))
        for message in messages:
            if message["seq"] != 1:
                world.inject("sink0", "chunk", message, at=1_000)
        world.run(until=10_000)
        self.assertEqual(events(world, "flow_materialized"), [])
        self.assertEqual(len(world.durable("c0", "sink0")), 4)
        for message in messages:
            if message["seq"] == 1:
                world.inject("sink0", "chunk", message, at=10_001)
        world.run(until=20_000)
        self.assertEqual(world.durable("c0", "sink0")["materialization"], oracle(config))

    def test_tiny_relay_window_retries_without_losing_obligations(self):
        config = FlowConfig(chunks=5, window=5, relay_slots=1)
        world = build(config)
        world.run(until=2_000_000)
        self.assert_complete(world, config)
        self.assertTrue(events(world, "flow_relay_refused"))

    def test_finite_admission_refusal_is_not_success(self):
        config = FlowConfig(memory_bytes=600)
        world = build(config)
        world.run(until=500_000)
        result = summarize(world, config)
        self.assertEqual(result["materializations"], 0)
        self.assertEqual(result["finished_sources"], 0)
        self.assertEqual(len(events(world, "flow_refused")), 2)
        self.assertTrue(all(h.peak <= h.config.memory_bytes for h in world.hosts.values()))

    def test_source_persistence_pressure_is_unfinished_not_false_success(self):
        config = FlowConfig(memory_bytes=2_048)
        world = build(config)
        world.run(until=500_000)
        result = summarize(world, config)
        self.assertEqual(result["materializations"], 0)
        self.assertEqual(result["finished_sources"], 0)
        self.assertEqual(len(events(world, "flow_offered")), 2)
        self.assertEqual(len(events(world, "flow_refused")), 0)
        self.assertEqual(len([w for w in result["waits"]
                              if w["reason"] == "source_persistence"]), 2)
        self.assertTrue(all(h.peak <= h.config.memory_bytes for h in world.hosts.values()))

    def test_transient_leases_drain_and_materialized_residency_stays_charged(self):
        config = FlowConfig()
        world = build(config)
        world.run(until=1_000_000)
        self.assert_complete(world, config)
        self.assertEqual({name: host.used for name, host in world.hosts.items()},
                         dict(p0=0, p1=0, d0=0, d1=0, c0=128, c1=128))
        world.crash("sink0")
        self.assertEqual(world.hosts["c0"].used, 0)
        self.assertIn("materialization", world.durable("c0", "sink0"))

    def test_replay_matches_semantic_events_and_rejects_changed_inputs(self):
        config = FlowConfig(seed=5, ordering="shuffle", duplicate=.2)
        first = build(config)
        first.run(until=1_000_000)
        replay = build(config, replay=first.decisions)
        replay.run(until=1_000_000)
        self.assertEqual(summarize(first, config), summarize(replay, config))
        with self.assertRaises(ValueError):
            changed = build(replace(config, rows=config.rows + 1), replay=first.decisions)
            changed.run(until=1_000_000)

    def test_oracle_detects_corrupt_enhancement_negative_control(self):
        config = FlowConfig(chunks=1, rows=4, foreground_count=0)
        world = build(config, offer=False)
        for source in ("source0", "source1"):
            data = manifest(config, source)
            world.inject("sink0", "chunk", dict(job=data["job"], source=source,
                         snapshot=data["snapshot"], code=data["code"], seq=0, total=1,
                         rows=data["chunks"][0], mask=[False] * config.rows), at=1_000)
        world.run(until=10_000)
        self.assertEqual(len(events(world, "flow_materialized")), 1)
        self.assertFalse(summarize(world, config)["correct"])


if __name__ == "__main__":
    unittest.main()
