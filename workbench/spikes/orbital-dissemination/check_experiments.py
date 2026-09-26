"""Causal and accounting checks independent of any latency target fit."""
import unittest

from experiments import run


class Workloads(unittest.TestCase):
    def test_write_dependencies_and_endpoints(self):
        for mode in ("strict", "pipelined_candidate"):
            r = run(dict(count=10, rate=1000, admission=mode))
            self.assertEqual(r["unfinished"], 0)
            self.assertEqual(r["pending_leader_journal_bytes"], 0)
            self.assertGreater(r["response"]["p50_us"], r["p50_us"])
            self.assertGreater(r["p50_us"], r["payload_ready"]["p50_us"])
            for t in r["traces"].values():
                self.assertGreaterEqual(t["effect"], t["admitted:f0"])
                self.assertGreaterEqual(t["admitted:f0"], t["payload:f0"])
                self.assertGreaterEqual(t["admitted:f0"], t["journal:f0"])
                self.assertGreater(t["payload:f0"], t["producer_durable"] + 100)
        strict = run(dict(count=5, rate=1000, admission="strict"))
        candidate = run(dict(count=5, rate=1000, admission="pipelined_candidate"))
        self.assertGreater(strict["p50_us"] - candidate["p50_us"], 150)

    def test_overload_cannot_disappear_from_denominator(self):
        r = run(dict(count=100, rate=1e6, admission="pipelined_candidate",
                     size=4096, queue_bytes=1024))
        self.assertEqual(r["offered"], 100)
        self.assertEqual(r["completed"] + r["unfinished"], 100)
        self.assertEqual(r["deadline_250_fraction"], 0)
        self.assertGreater(sum(v for k, v in r["counters"].items() if k.startswith("overflow:")), 0)

    def test_missing_payload_holds_candidate_prefix(self):
        cfg = dict(count=20, rate=10000, producers=2, max_retries=0,
                   faults=[dict(kind="partition", src="p0_p0", dst=w,
                                at_us=0, until_us=300) for w in ("f0", "s0")])
        strict = run(dict(cfg, admission="strict"))
        early = run(dict(cfg, admission="pipelined_candidate"))
        self.assertGreater(strict["completed"], 0)
        self.assertEqual(early["completed"], 0)
        self.assertGreater(early["journal_ready_behind_gap"], 0)

    def test_small_packet_batching_reduces_wire_work(self):
        cfg = dict(kind="messages", senders=8, receivers=1, count=200,
                   rate=200000, size=24, burst=8, relay=True, cpu_slots=1)
        raw = run(cfg)
        batched = run(dict(cfg, batch_bytes=1200, batch_wait_us=4))
        self.assertEqual(raw["completed"], batched["completed"])
        self.assertEqual(raw["unfinished"], 0)
        self.assertLess(batched["counters"]["packets"], raw["counters"]["packets"])
        self.assertLess(batched["counters"]["wire_bytes"], raw["counters"]["wire_bytes"])

    def test_shape_grid(self):
        for ns in (1, 3, 8):
            for nr in (1, 3, 8):
                r = run(dict(kind="messages", senders=ns, receivers=nr, count=10, rate=1000))
                self.assertEqual(r["completed"], 10)
                self.assertEqual(r["unfinished"], 0)

    def test_seed_replays(self):
        cfg = dict(count=20, rate=10000, jitter_us=4, loss=.01, seed=23)
        self.assertEqual(run(cfg), run(cfg))

    def test_fault_window_excludes_earlier_births(self):
        r = run(dict(count=1, rate=1000, admission="pipelined_candidate", fold_us=2000,
                     faults=[dict(kind="partition", src="p0", dst="s0", at_us=100, until_us=300)]))
        self.assertEqual(r["by_producer"]["p0"]["born_in_first_fault_window_unfinished_1ms_after"], 0)


if __name__ == "__main__":
    unittest.main()
