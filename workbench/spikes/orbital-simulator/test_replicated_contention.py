import unittest

from kernel import digest
from sim import Actor
from traffic import Config, Strategy
from replicated_contention import Witness, audit, build, consumer_names, witness_names


class Sink(Actor):
    def on(self, ctx, kind, data):
        if kind == "response":
            ctx.note("test_response", result=data)


class ReplicatedContentionTests(unittest.TestCase):
    def small(self, **kwargs):
        return Config(count=6, shards=2, width=4, topology="lan", drain_ns=3_000_000, **kwargs)

    def test_transaction_programs_fold_equal_full_state_at_all_replicas(self):
        for shape in ("points", "hot", "scan", "transfer", "max-update", "conditional"):
            with self.subTest(shape=shape):
                w, _, until = build(self.small(workload=shape, slow_every=3), seed=13,
                                    replica_delays=(0, 7_000, 23_000))
                w.run(until=until)
                self.assertFalse(audit(w))
                self.assertEqual(6, sum(e["kind"] == "traffic_response" for e in w.trace))
                for shard in range(2):
                    consumers = [w.actors[name].actor for name in consumer_names(shard)]
                    self.assertEqual(len({consumer.applied for consumer in consumers}), 1)
                    self.assertGreater(consumers[0].applied, 0)
                    states = {digest(consumer.history.logical_state()) for consumer in consumers}
                    self.assertEqual(len(states), 1)
                    for epoch in consumers[0].state_hashes:
                        self.assertEqual(len({c.state_hashes[epoch] for c in consumers}), 1)
                        self.assertEqual(len({c.output_hashes[epoch] for c in consumers}), 1)

    def test_lost_callbacks_restore_real_witness_and_consumer_records(self):
        for victim, host, prefix in (("shard0", "h0", "log/"),
                                     ("witness0_1", "w0_1", "log/"),
                                     ("consumer0_0", "c0_0", "epoch/")):
            with self.subTest(victim=victim):
                w, _, until = build(self.small(workload="transfer"), seed=3)
                w.when("durable_write", lambda e, victim=victim, prefix=prefix:
                       e["actor"] == victim and e["key"].startswith(prefix),
                       "power_loss", host=host)
                w.fault(1_000_000, "power_on", host=host)
                w.run(until=until)
                self.assertFalse(audit(w))
                self.assertEqual(6, sum(e["kind"] == "traffic_response" for e in w.trace))
                self.assertTrue(any(e["kind"] == "replicated_recovered" and e["actor"] == victim
                                    and e["incarnation"] == 2 and e["records"] > 0 for e in w.trace))

    def test_consumer_loses_local_storage_and_catches_up_from_witness_reads(self):
        w, _, until = build(self.small(), seed=7)
        w.when("replicated_fixpoint", lambda e: e["actor"] == "consumer0_2" and e["epoch"] == 2,
               "destroy", host="c0_2")
        w.fault(1_000_000, "power_on", host="c0_2")
        w.run(until=until)
        self.assertFalse(audit(w))
        self.assertEqual(6, sum(e["kind"] == "traffic_response" for e in w.trace))
        consumers = [w.actors[name].actor for name in consumer_names(0)]
        self.assertEqual(len({digest(c.history.logical_state()) for c in consumers}), 1)
        self.assertTrue(any(e["kind"] == "disk_submit" and e["actor"] in witness_names(0)
                            and e.get("mode") == "read" for e in w.trace))

    def test_unavailable_quorum_blocks_its_shard_and_not_independent_shard(self):
        w, _, until = build(self.small())
        for target in ("w0_1", "w0_2"):
            w.fault(0, "partition", source_host="h0", target_host=target)
        w.run(until=800_000)
        complete = {e["op"] for e in w.trace if e["kind"] == "traffic_response"}
        self.assertEqual(complete, {2, 4, 6})
        self.assertEqual(w.actors["consumer0_0"].actor.applied, 0)
        self.assertFalse(audit(w))
        for target in ("w0_1", "w0_2"):
            w.fault(850_000, "partition", source_host="h0", target_host=target, blocked=False)
        w.run(until=until)
        self.assertEqual(6, sum(e["kind"] == "traffic_response" for e in w.trace))
        self.assertFalse(audit(w))

    def test_one_unavailable_follower_keeps_prepared_quorum_serving(self):
        w, _, until = build(self.small())
        w.fault(0, "partition", source_host="h0", target_host="w0_1")
        w.run(until=until)
        self.assertEqual(6, sum(e["kind"] == "traffic_response" for e in w.trace))
        self.assertFalse(audit(w))
        self.assertFalse(w.actors["witness0_1"].actor.batches)
        self.assertGreater(w.actors["consumer0_0"].actor.applied, 0)

    def test_pending_continuation_does_not_prevent_following_epoch_progress(self):
        w, _, _ = build(self.small(), Strategy(batch=1), offers=False)
        w.add_actor("sink", "client", Sink)
        requests = [(1, "acquire", dict(locks=["s0:k0"], writes=["s0:k0"])),
                    (1, "announce", {}), (1, "fix", dict(position=10)), (1, "release", {}),
                    (2, "read", dict(position=20, keys=["s0:k0"])),
                    (3, "acquire", dict(locks=["s0:k1"], writes=["s0:k1"])),
                    (3, "announce", {}), (3, "fix", dict(position=20)), (3, "release", {}),
                    (3, "resolve", dict(values={"s0:k1":123})),
                    (4, "read", dict(position=30, keys=["s0:k1"]))]
        for i, (tx, kind, fields) in enumerate(requests):
            w.inject("shard0", "request", dict(request=f"{tx}/{kind}/0", tx=tx,
                     kind=kind, reply="sink", **fields), at=100_000 + i * 20_000)
        w.run(until=800_000)
        results = [e["result"] for e in w.trace if e["kind"] == "test_response"]
        self.assertTrue(any(r["tx"] == 4 and r.get("values") == {"s0:k1":123} for r in results))
        self.assertFalse(any(r["tx"] == 2 for r in results))
        pending_states = [e for e in w.trace if e["kind"] == "replicated_fixpoint"
                          and e["state"]["reads"] and "2/read/0" not in e["state"]["responses"]]
        self.assertTrue(pending_states)
        self.assertFalse(audit(w))
        w.inject("shard0", "request", dict(request="1/resolve/0", tx=1, kind="resolve",
                 reply="sink", values={"s0:k0":42}), at=900_000)
        w.run(until=1_200_000)
        self.assertTrue(any(e["kind"] == "test_response" and e["result"]["tx"] == 2
                            and e["result"].get("values") == {"s0:k0":42} for e in w.trace))
        self.assertFalse(audit(w))

    def test_loss_and_duplicates_do_not_create_alternate_agreed_histories(self):
        w, _, until = build(self.small(loss=.03, duplicate=.2), seed=19)
        w.run(until=until)
        self.assertEqual(6, sum(e["kind"] == "traffic_response" for e in w.trace))
        self.assertFalse(audit(w))

    def test_oracle_rejects_fabricated_quorum_and_replica_state(self):
        for corruption in ("proof", "one-voter", "state", "outputs"):
            with self.subTest(corruption=corruption):
                w, _, until = build(Config(count=1, shards=2, topology="lan", drain_ns=1_000_000))
                w.run(until=until)
                self.assertFalse(audit(w))
                if corruption == "proof":
                    row = next(e for e in w.trace if e["kind"] == "durable_write"
                               and e["actor"] == "shard0" and e["key"] == "log/1")
                    row["value"]["requests"][0]["tx"] += 1000
                elif corruption == "one-voter":
                    row = next(e for e in w.trace if e["kind"] == "durable_write"
                               and e["actor"] == "consumer0_0" and e["key"] == "epoch/1")
                    row["value"]["proof"] = row["value"]["proof"][:1]
                else:
                    row = next(e for e in w.trace if e["kind"] == "replicated_fixpoint"
                               and e["actor"] == "consumer0_1")
                    if corruption == "state":
                        row["state"]["bounds"]["s0:k0"] = 999
                        row["logical_hash"] = digest(row["state"])
                    else:
                        row["outputs"] = []
                        row["output_hash"] = digest([])
                self.assertTrue(audit(w))

    def test_transaction_oracle_rejects_forged_completion_and_missing_or_altered_decision(self):
        config = Config(count=1, shards=2, width=4, topology="lan", drain_ns=1_000_000)
        w, _, _ = build(config)
        w.inject("client", "done", dict(tx=1), at=102_000)
        w.run(until=110_000)
        self.assertTrue(any("client response" in error for error in audit(w)))
        for corruption in ("missing", "values", "snapshot"):
            with self.subTest(corruption=corruption):
                config = Config(count=1, shards=2, width=4, workload="scan", topology="lan", drain_ns=1_000_000)
                w, _, until = build(config)
                w.run(until=until)
                self.assertFalse(audit(w))
                row = next(e for e in w.trace if e["kind"] == "durable_write" and e["key"] == "outcome/1")
                if corruption == "missing":
                    row["kind"] = "test_deleted_decision"
                elif corruption == "values":
                    row["value"]["values"]["s0:answer"] += 123
                else:
                    key = next(iter(row["value"]["observed"]))
                    row["value"]["observed"][key] += 123
                    row["value"]["values"]["s0:answer"] += 123
                self.assertTrue(audit(w))

    def test_finite_accepted_transaction_can_exhaust_completion_headroom(self):
        config = Config(count=1, shards=2, width=2, topology="lan", memory_bytes=8192,
                        drain_ns=3_000_000)
        w, _, until = build(config, seed=7)
        w.run(until=until)
        self.assertFalse(audit(w))  # Safety is different from progress.
        self.assertEqual(1, sum(e["kind"] == "traffic_accepted" for e in w.trace))
        self.assertEqual(0, sum(e["kind"] == "traffic_response" for e in w.trace))
        self.assertTrue(any(e["kind"] == "replicated_transition" and e["request"]["kind"] == "announce"
                            for e in w.trace))
        self.assertTrue(any(wait["reason"] == "retained input capacity" for wait in w.waits.values()))

    def test_finite_prefix_oracle_rejects_skipping_unfinished_predecessor(self):
        config = Config(workload="bulk-rmw", count=3, shards=2, width=2, topology="lan",
                        slow_every=20, slow_ns=5_000_000, point_rmw=True,
                        interval_ns=200_000, drain_ns=1_000_000)
        w, _, until = build(config, Strategy(negative="ignore-pending"), seed=7)
        w.run(until=until)
        self.assertFalse(any(e["kind"] == "durable_write" and e["key"] == "outcome/1" for e in w.trace))
        self.assertTrue(any("skipped pending" in error for error in audit(w)))

    def test_duplicate_acceptance_does_not_trigger_more_replication(self):
        class Port:
            actor = "shard0"
            def __init__(self): self.sent = []
            def send(self, target, kind, data, size):
                self.sent.append((target, kind, data))
                return True
        leader, port = Witness(0, Strategy(), leader=True), Port()
        leader.batches = {1: {"epoch":1}, 2: {"epoch":2}}
        ack = dict(epoch=1, digest=digest(leader.batches[1]))
        leader.message(port, "witness0_1", "accepted", ack)
        after_progress = len(port.sent)
        self.assertGreater(after_progress, 0)
        for _ in range(100):
            leader.message(port, "witness0_1", "accepted", ack)
        self.assertEqual(len(port.sent), after_progress)
        # New local input and progress at the first follower must not resend
        # the second follower's already outstanding epoch.
        leader.batches[3] = {"epoch":3}
        for _ in range(100):
            leader.pump_replication(port)
        self.assertEqual(len(port.sent), after_progress)
        leader.message(port, "witness0_1", "accepted", dict(epoch=2, digest=digest(leader.batches[2])))
        self.assertEqual(len(port.sent), after_progress + 1)
        self.assertEqual(port.sent[-1][0], "witness0_1")
        leader.pump_replication(port, retry=True)
        self.assertEqual(len(port.sent), after_progress + 3)

    def test_exact_replay(self):
        config = Config(count=2, shards=2, topology="lan", drain_ns=1_000_000)
        w, _, until = build(config, seed=31)
        w.run(until=until)
        replay, _, _ = build(config, seed=31, replay=w.decisions)
        replay.run(until=until)
        replay.kernel.check_replay()
        self.assertEqual(w.report(), replay.report())


if __name__ == "__main__":
    unittest.main()
