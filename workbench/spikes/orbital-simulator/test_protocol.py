"""Bounded protocol histories; synthetic service time is not a benchmark."""
import unittest

from protocol import (ACTOR_HOST, DEFAULT_COMMANDS, AuditError, audit,
                      build_scenario, run_scenario)


class ProtocolTests(unittest.TestCase):
    def assert_complete(self, world):
        result = audit(world, raise_on_error=True)
        self.assertEqual(result["values"], {"x": 11, "y": 7})
        self.assertEqual(result["applications"], 4)
        self.assertEqual(set(result["published"]), {1, 2, 3, 4})
        self.assertTrue(result["ordinary_completed"])
        for machine in world.hosts.values():
            self.assertLessEqual(machine.peak, machine.config.memory_bytes)
            self.assertLessEqual(machine.storage_used + machine.storage_reserved,
                                 machine.config.durable_bytes)
        return result

    def test_direct_and_relay_same_value_different_physical_work(self):
        direct = run_scenario(mode="direct", until=1_500_000)
        relay = run_scenario(mode="relay", until=1_500_000)
        self.assert_complete(direct["world"])
        self.assert_complete(relay["world"])
        self.assertNotIn("relayed", direct["report"]["counts"])
        self.assertGreater(relay["report"]["counts"]["packet_departed"],
                           direct["report"]["counts"]["packet_departed"])
        self.assertGreater(relay["report"]["resources"]["relay_a/tx"]["busy_ns"], 0)

    def test_two_witnesses_and_early_follower_need_no_leader_return(self):
        w = build_scenario()
        w.fault(0, "power_loss", host="witness_c")
        w.partition("witness_b", "witness_a")
        w.run(until=1_500_000)
        self.assert_complete(w)
        chosen = [e for e in w.trace if e["kind"] == "prefix_chosen"]
        self.assertTrue(chosen)
        self.assertEqual({e["actor"] for e in chosen}, {"follower_b"})
        self.assertTrue(all({v["voter"] for v in e["proof"]} ==
                            {"leader", "follower_b"} for e in chosen))

    def test_payload_wait_does_not_stop_same_host_ordinary_work(self):
        w = build_scenario()
        for source in ("source", "copy_b", "copy_c"):
            w.partition(source, "reader")
        w.run(until=700_000)
        partial = audit(w, raise_on_error=True)
        self.assertTrue(partial["ordinary_completed"])
        self.assertEqual(partial["applications"], 0)
        self.assertTrue(partial["chosen_prefixes"])
        self.assertTrue(any(v["reason"] == "chosen payload missing" for v in w.waits.values()))
        self.assertEqual(w.actors["ordinary"].host, w.actors["consumer"].host)
        w.partition("copy_b", "reader", blocked=False)
        w.run(until=1_500_000)
        self.assert_complete(w)

    def test_consumer_restart_after_durable_write_before_callback(self):
        w = build_scenario()
        w.when("durable_write", lambda e: e["actor"] == "consumer" and e["key"] == "applied/1",
               "crash", actor="consumer")
        w.fault(800_000, "restart", actor="consumer")
        w.run(until=1_800_000)
        self.assert_complete(w)
        self.assertTrue(any(e["kind"] == "consumer_restored" and e["lsn"] == 1 for e in w.trace))
        self.assertTrue(any(e["kind"] == "stale_completion" and e["actor"] == "consumer"
                            for e in w.trace))
        # No callback was needed to make the original durable record real.
        applied_one = [e for e in w.trace if e["kind"] == "application_durable" and e["lsn"] == 1]
        self.assertEqual(applied_one, [])

    def test_producer_and_witness_restart_recover_durable_identities(self):
        w = build_scenario()
        w.fault(700_000, "crash", actor="producer")
        w.fault(700_000, "crash", actor="follower_b")
        w.fault(850_000, "restart", actor="producer")
        w.fault(850_000, "restart", actor="follower_b")
        w.inject("producer", "submit", {"lsn": 2, "command": DEFAULT_COMMANDS[1]}, at=950_000)
        w.run(until=1_800_000)
        self.assert_complete(w)
        records = w.durable("source", "producer")
        self.assertEqual(len([k for k in records if k.startswith("payload/")]), 4)
        self.assertFalse(any(e["kind"] == "identity_conflict" for e in w.trace))

    def test_source_reconciles_write_completing_after_recovery_scan(self):
        w = build_scenario()
        w.when("disk_submit", lambda e: e["actor"] == "producer" and e["key"] == "payload/1",
               "crash", actor="producer")
        w.fault(150_000, "restart", actor="producer")
        w.run(until=1_800_000)
        self.assert_complete(w)
        recovered = [e for e in w.trace if e["kind"] == "protocol_recovered"
                     and e["actor"] == "producer" and e["incarnation"] == 2]
        self.assertEqual(recovered[0]["records"], 0)
        self.assertTrue(any(e["kind"] == "late_payload_recovered" and e["lsn"] == 1
                            and e["incarnation"] == 2 for e in w.trace))

    def test_power_loss_retains_completed_consumer_records(self):
        w = build_scenario()
        w.fault(700_000, "power_loss", host="reader")
        w.fault(850_000, "power_on", host="reader")
        w.run(until=1_800_000)
        self.assert_complete(w)
        self.assertTrue(any(e["kind"] == "consumer_restored" for e in w.trace))

    def test_one_domain_destroyed_after_choice_surviving_payloads_finish(self):
        w = build_scenario(consumer_compute_ns=250_000)
        for host in ("source", "witness_a"):
            w.when("prefix_chosen", lambda e: len(e["entries"]) == 4, "destroy", host=host)
        w.run(until=2_500_000)
        self.assert_complete(w)
        self.assertEqual(w.durable("source", "producer"), {})
        self.assertEqual(w.durable("witness_a", "leader"), {})
        self.assertTrue(w.durable("copy_b", "payload_b"))
        destroyed = next(e["time"] for e in w.trace if e["kind"] == "storage_destroyed")
        final = next(e["time"] for e in w.trace if e["kind"] == "published" and e["lsn"] == 4)
        self.assertGreater(final, destroyed)

    def test_route_change_retries_preserve_end_to_end_identity(self):
        w = build_scenario(mode="relay")
        w.fault(0, "power_loss", host="relay_a")
        for actor in ("producer", "payload_b", "payload_c", "leader",
                      "follower_b", "follower_c", "consumer"):
            w.inject(actor, "route_update", {"version": 1, "via": "relay_b"}, at=600_000)
        w.run(until=2_000_000)
        self.assert_complete(w)
        self.assertTrue(any(e["kind"] == "route_changed" for e in w.trace))
        self.assertTrue(any(e["kind"] == "relayed" and e["actor"] == "relay_b" for e in w.trace))

    def test_loss_and_duplicates_have_one_logical_application(self):
        w = build_scenario(seed=7, ordering="shuffle", loss=.2, duplicate=.35)
        w.run(until=3_000_000)
        self.assert_complete(w)
        self.assertTrue(any(e["kind"] == "packet_duplicate" for e in w.trace))
        self.assertTrue(any(e["kind"] == "packet_drop" for e in w.trace))

    def test_static_leader_loss_is_honest_unavailability(self):
        w = build_scenario()
        w.fault(0, "destroy", host="witness_a")
        w.run(until=800_000)
        result = audit(w, raise_on_error=True)
        self.assertEqual(result["applications"], 0)
        self.assertTrue(result["ordinary_completed"])
        self.assertTrue(w.waits)
        self.assertFalse(any(e["kind"] == "prefix_chosen" for e in w.trace))

    def test_durable_capacity_refusal_never_becomes_publication(self):
        w = build_scenario()
        # The small ordinary result fits; a certified application snapshot does
        # not. No unmodeled memory/disk spill or success-on-enqueue is permitted.
        w.hosts["reader"].config.durable_bytes = 512
        w.run(until=800_000)
        result = audit(w, raise_on_error=True)
        self.assertTrue(result["ordinary_completed"])
        self.assertTrue(result["chosen_prefixes"])
        self.assertEqual(result["applications"], 0)
        self.assertEqual(result["published"], {})
        self.assertTrue(any(e["kind"] == "disk_refused" and e["actor"] == "consumer"
                            for e in w.trace))
        self.assertTrue(any(v["reason"] == "durable application backpressure"
                            for v in w.waits.values()))

    def test_chosen_before_payload_negative_is_rejected(self):
        w = build_scenario(negative="chosen-before-payload")
        w.partition("source", "copy_b")
        w.partition("source", "copy_c")
        w.run(until=800_000)
        self.assertTrue(any(e["kind"] == "prefix_chosen" for e in w.trace))
        with self.assertRaisesRegex(AuditError, "two durable payload domains"):
            audit(w, raise_on_error=True)

    def test_publish_too_early_negative_is_rejected(self):
        w = build_scenario(negative="publish-too-early")
        w.run(until=800_000)
        with self.assertRaisesRegex(AuditError, "lacks chosen evidence"):
            audit(w, raise_on_error=True)

    def test_observer_checks_physical_domains_not_receipt_configuration(self):
        w = build_scenario()
        # An invalid deployment says three different domains in bootstrap,
        # while physical copies actually share one failure domain.
        w.hosts["copy_b"].config.domain = "az-a"
        w.hosts["copy_c"].config.domain = "az-a"
        w.run(until=800_000)
        with self.assertRaisesRegex(AuditError, "two durable payload domains"):
            audit(w, raise_on_error=True)

    def test_exact_replay_preserves_causal_trace(self):
        original = build_scenario(seed=19, ordering="shuffle", loss=.1, duplicate=.1)
        first = original.run(until=1_000_000)
        replayed = build_scenario(seed=19, ordering="shuffle", loss=.1, duplicate=.1,
                                  replay=original.decisions)
        second = replayed.run(until=1_000_000)
        self.assertEqual(first["trace_hash"], second["trace_hash"])
        self.assertEqual(first["choices_hash"], second["choices_hash"])
        replayed.kernel.check_replay()
        self.assertEqual(audit(original), audit(replayed))


if __name__ == "__main__":
    unittest.main()
