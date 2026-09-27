import unittest
from dataclasses import replace

from traffic import Config, Strategy, Shard, audit, build, evaluate
from sim import Actor, Host, World


class TrafficTests(unittest.TestCase):
    def run_case(self, config, strategy=Strategy(), seed=1):
        world, plans, until = build(config, strategy, seed)
        world.run(until=until)
        return world, plans, audit(world, plans, config)

    def test_programs_and_modes_have_actual_serial_snapshots(self):
        for shape in ("points", "hot", "scan", "max-update", "bulk-rmw", "bulk-blind",
                      "conditional", "bridge", "transfer", "wan-independent", "overwrite", "global-scan"):
            for mode in ("mv", "protection", "shard"):
                with self.subTest(shape=shape, mode=mode):
                    c = Config(workload=shape, count=12, slow_every=4, drain_ns=8_000_000)
                    w, _, errors = self.run_case(c, Strategy(ordering=mode), seed=7)
                    self.assertFalse(errors)
                    self.assertEqual(12, sum(e["kind"] == "traffic_response" for e in w.trace))

    def test_read_width_does_not_hold_source_writers(self):
        observations = [evaluate(dict(config=dict(workload="scan", count=20, slow_every=5),
                                      strategy=dict(ordering=mode)), 1)
                        for mode in ("mv", "protection")]
        self.assertTrue(all(not o.violations for o in observations))
        self.assertLess(observations[0].metrics["point_max_ns"], observations[1].metrics["point_max_ns"])

    def test_waiter_wakeup_avoids_idle_retry_gaps(self):
        values = [evaluate(dict(config=dict(workload="hot", count=24, interval_ns=500),
                                strategy=dict(wake_waiters=wake)), 3) for wake in (True, False)]
        self.assertFalse(values[0].violations + values[1].violations)
        self.assertLess(values[0].metrics["hot_max_ns"], values[1].metrics["hot_max_ns"])

    def test_power_recovery_midway_uses_journals(self):
        for at in (160_000, 300_000, 650_000):
            c = Config(workload="transfer", count=16, slow_every=4, incident="power",
                       fault_at_ns=at, fault_duration_ns=150_000, drain_ns=12_000_000)
            w, _, errors = self.run_case(c, seed=19)
            self.assertFalse(errors)
            self.assertEqual(16, sum(e["kind"] == "traffic_response" for e in w.trace))
            self.assertTrue(any(e["kind"] == "traffic_recovered" and e["incarnation"] == 2 for e in w.trace))

    def test_loss_duplicates_preserve_identity_and_outcomes(self):
        c = Config(workload="transfer", count=12, loss=.05, duplicate=.5, drain_ns=30_000_000)
        w, _, errors = self.run_case(c, seed=19)
        self.assertFalse(errors)
        self.assertEqual(12, sum(e["kind"] == "traffic_response" for e in w.trace))

    def test_all_offers_remain_accounted_when_capacity_fails(self):
        o = evaluate(dict(config=dict(count=60, burst=20, interval_ns=1000, memory_bytes=12_000,
                                     durable_bytes=24_000, drain_ns=1_000_000)), 7)
        c = o.cohorts["point"]
        self.assertEqual(60, c["offered"])
        self.assertEqual(60, sum(c[k] for k in ("completed", "refused", "unfinished")))
        self.assertGreater(c["refused"] + c["unfinished"], 0)
        self.assertFalse(o.violations)

    def test_pending_output_negative_is_caught(self):
        c = Config(workload="transfer", count=16, slow_every=4, interval_ns=4000)
        _, _, errors = self.run_case(c, Strategy(negative="ignore-pending"))
        self.assertTrue(errors)
        self.assertTrue(any("pending" in e or "snapshot" in e for e in errors))

    def test_complete_replacement_can_supersede_pending_cell(self):
        observations = [evaluate(dict(config=dict(workload="overwrite", count=24, slow_every=24, slow_ns=2_000_000),
                                      strategy=dict(supersede=supersede)), 3) for supersede in (False, True)]
        self.assertTrue(all(not o.violations for o in observations))
        self.assertLess(observations[1].metrics["point_max_ns"], observations[0].metrics["point_max_ns"])

    def test_oracle_rejects_fabricated_observation_and_result_together(self):
        c = Config(workload="scan", count=8, slow_every=4)
        w, plans, _ = self.run_case(c, seed=7)
        row = next(e for e in w.trace if e["kind"] == "durable_write" and e["key"] == "outcome/1")
        observed = row["value"]["observed"]
        observed[next(iter(observed))] += 7
        row["value"]["values"]["s0:answer"] += 7
        self.assertTrue(any("wrong snapshot" in e for e in audit(w, plans, c)))

    def test_oracle_rejects_install_differing_from_decision(self):
        c = Config(workload="scan", count=8, slow_every=4)
        w, plans, _ = self.run_case(c, seed=7)
        row = next(e for e in w.trace if e["kind"] == "traffic_transition" and e["transition"] == "resolve" and e["op"] == 1)
        row["request"]["values"]["s0:answer"] += 99
        self.assertTrue(any("differs" in e for e in audit(w, plans, c)))

    def test_exact_replay_of_power_recovery(self):
        c = Config(workload="scan", count=8, incident="power", fault_at_ns=250_000,
                   fault_duration_ns=100_000, drain_ns=3_000_000)
        w, plans, errors = self.run_case(c)
        self.assertFalse(errors)
        replay, _, until = build(c, replay=w.decisions)
        replay.run(until=until)
        replay.kernel.check_replay()
        self.assertEqual(w.report(), replay.report())

    def test_scored_response_needs_durable_installed_outcome(self):
        c = Config(count=1)
        w, plans, _ = build(c)
        w.inject("client", "done", dict(tx=1), at=102_000)
        w.run(until=110_000)
        self.assertTrue(audit(w, plans, c))

    def test_journal_commit_before_callback_survives_power_cut(self):
        for phase in ("fix", "resolve"):
            c = Config(count=1)
            w, plans, _ = build(c)
            w.when("durable_write", lambda e, phase=phase: e["actor"] == "shard0"
                   and e["key"].startswith("journal/") and any(r["kind"] == phase for r in e["value"]),
                   "power_loss", host="h0")
            w.fault(1_000_000, "power_on", host="h0")
            w.run(until=4_000_000)
            self.assertFalse(audit(w, plans, c))
            self.assertEqual(1, sum(e["kind"] == "traffic_response" for e in w.trace))

    def test_initial_version_memory_is_charged(self):
        amounts = []
        for width in (2, 2000):
            c = Config(count=1, width=width, start_ns=1_000_000)
            w, _, _ = build(c)
            w.run(until=50_000)
            amounts.append(w.hosts["h0"].used)
        self.assertGreater(amounts[1], amounts[0] + 10_000)

    def test_supersession_respects_old_cut_and_other_output_cells(self):
        class Receiver(Actor):
            def on(self, ctx, kind, data):
                if kind == "response":
                    ctx.note("answer", **{k:v for k,v in data.items() if k != "kind"})
        w = World()
        w.add_host(Host("h", handler_ns=1, disk_latency=10, nic_bytes_per_ns=100, disk_bytes_per_ns=100))
        initial = {"s0:k0":0, "s0:k1":0}
        w.add_actor("shard0", "h", lambda: Shard(0, initial, Strategy(supersede=True, batch_ns=1), 0))
        w.add_actor("sink", "h", Receiver)
        def request(at, tx, kind, **fields):
            w.inject("shard0", "request", dict(tx=tx, kind=kind, request=f"{tx}/{kind}", reply="sink", **fields), at=at)
        request(1000, 1, "acquire", locks=list(initial), writes=list(initial))
        request(1500, 1, "announce")
        request(2000, 1, "fix", position=10)
        request(2500, 1, "release")
        request(3000, 2, "acquire", locks=["s0:k0"], writes=["s0:k0"])
        request(3500, 2, "announce")
        request(4000, 2, "fix", position=20)
        request(4500, 2, "release")
        request(5000, 2, "resolve", values={"s0:k0":2})
        request(6000, 3, "read", position=15, keys=["s0:k0"])
        request(6100, 4, "read", position=30, keys=["s0:k0"])
        request(6200, 5, "read", position=30, keys=list(initial))
        w.run(until=8000)
        answers = {e["tx"]:e["values"] for e in w.trace if e["kind"] == "answer" and "values" in e}
        self.assertEqual(answers, {4:{"s0:k0":2}})
        request(9000, 1, "resolve", values={"s0:k0":1,"s0:k1":1})
        w.run(until=11000)
        answers = {e["tx"]:e["values"] for e in w.trace if e["kind"] == "answer" and "values" in e}
        self.assertEqual(answers[3], {"s0:k0":1})
        self.assertEqual(answers[5], {"s0:k0":2,"s0:k1":1})


if __name__ == "__main__":
    unittest.main()
