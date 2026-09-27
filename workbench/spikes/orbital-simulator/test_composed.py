import unittest

from composed import audit, build


class ComposedTests(unittest.TestCase):
    def test_private_materialization_is_not_publication_and_point_write_progresses(self):
        world, config = build(hold_artifact_until=800_000)
        world.run(until=700_000)
        early = audit(world, config)
        self.assertTrue(early["ok"], early)
        self.assertEqual(early["materialized"], 2)
        self.assertEqual(set(early["publication_ns"]), {1})
        self.assertFalse(early["complete"])
        self.assertEqual(early["foreground_completed"], config.foreground_count)
        world.run(until=3_000_000)
        final = audit(world, config)
        self.assertTrue(final["ok"], final)
        self.assertTrue(final["complete"], final)
        self.assertLess(final["publication_ns"][1], 800_000)
        self.assertGreater(final["publication_ns"][2], 800_000)

    def test_recovery_crosses_application_and_protocol_boundaries_through_ports(self):
        world, config = build()
        world.when("durable_write", lambda e: e["actor"] == "application" and e["key"] == "outbox/2",
                   "crash", actor="application")
        world.fault(800_000, "restart", actor="application")
        world.when("durable_write", lambda e: e["actor"] == "consumer" and e["key"] == "applied/2",
                   "crash", actor="consumer")
        world.fault(1_500_000, "restart", actor="consumer")
        world.run(until=3_000_000)
        result = audit(world, config)
        self.assertTrue(result["ok"], result)
        self.assertTrue(result["complete"], result)
        self.assertEqual(world.durable("source", "producer")["payload/2"]["lsn"], 2)

    def test_shared_resource_backpressure_does_not_manufacture_publication(self):
        world, config = build()
        world.pause("witness_b", "disk_bytes")
        world.pause("witness_c", "disk_bytes")
        world.run(until=700_000)
        result = audit(world, config)
        self.assertTrue(result["ok"], result)
        self.assertEqual(result["publication_ns"], {})
        self.assertEqual(result["materialized"], 2)
        world.pause("witness_b", "disk_bytes", False)
        world.run(until=3_000_000)
        result = audit(world, config)
        self.assertTrue(result["ok"], result)
        self.assertTrue(result["complete"], result)

    def test_recorded_composed_schedule_replays(self):
        first, config = build(seed=19)
        first.run(until=2_000_000)
        second, _ = build(seed=19, replay=first.decisions)
        second.run(until=2_000_000)
        second.kernel.check_replay()
        self.assertEqual(first.report()["trace_hash"], second.report()["trace_hash"])
        self.assertEqual(audit(first, config), audit(second, config))


if __name__ == "__main__":
    unittest.main()
