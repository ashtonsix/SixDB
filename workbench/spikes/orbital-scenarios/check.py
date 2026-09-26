#!/usr/bin/env python3
"""Counterexamples and invariants of this slice, not tests of Orbital itself."""

import copy
import itertools
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

from model import POLICIES, canonical, run, validate
from batch import BatchModel as Model
from scenarios import base, cycle, discovery, part, presets, reservation_cycle, transaction, workload


class ModelChecks(unittest.TestCase):
    def test_replay_identity_distinguishes_code_from_documentation(self):
        import run as replay
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in [*replay.model_identity(), "MODEL.md"]:
                shutil.copyfile(replay.ROOT / name, root / name)
            with patch.object(replay, "ROOT", root):
                before = replay.execute(discovery())
                (root / "MODEL.md").write_text("Revised explanation\n")
                after = replay.execute(discovery())
                self.assertEqual(before["model_sha256"], after["model_sha256"])
                self.assertEqual(before["trace_sha256"], after["trace_sha256"])
                self.assertNotEqual(before["context_files_sha256"], after["context_files_sha256"])
                with (root / "batch.py").open("a") as source:
                    source.write("\n# Changed executable source\n")
                with self.assertRaisesRegex(ValueError, "Model sources changed"):
                    replay.execute(discovery())

    def test_pending_cycle_remains_visible(self):
        s = cycle()
        s["policy"]["arbitration_us"] = 10000
        r = run(s)
        self.assertEqual(r["summary"]["completed"], 0)
        self.assertEqual(r["summary"]["cycles"], [["T1", "T2"]])
        self.assertIsNone(r["summary"]["p99_completed_us"])
        self.assertEqual(r["summary"]["oldest_pending_us"], s["horizon_us"])

    def test_discovery_closure_and_causal_order(self):
        r = run(discovery())
        events = r["trace"]
        authorizations = [i for i, e in enumerate(events) if e["type"] == "authorize"]
        reports = [i for i, e in enumerate(events) if e["type"] == "report"]
        self.assertEqual(len(authorizations), 1)
        self.assertEqual(len(reports), 3)
        self.assertLess(max(reports), authorizations[0])
        self.assertEqual(r["summary"]["completed"], 1)

    def test_stale_report_does_not_resurrect_invalidated_chain(self):
        m = Model(discovery())
        while m.transactions["T1"].parts["read-B"].state != "prepared":
            self.assertTrue(m.step())
        old = {"transaction": "T1", "part": "read-B", "generation": 0}
        m.invalidate("T1", ["read-A"], "test")
        m.report(old)
        self.assertEqual(m.counts["stale_report"], 1)
        self.assertEqual(m.transactions["T1"].reported, {})
        self.assertEqual(m.transactions["T1"].parts["write-A"].state, "hidden")
        self.assertFalse(m.grants)
        while m.step():
            pass
        self.assertEqual(m.summary()["completed"], 1)

    def test_c2_authorization_closes_yield_window(self):
        s = base("C2 fence", [transaction("holder", [part("p", "A", "x")]),
                              transaction("request", [part("p", "A", "x")])])
        m = Model(s)
        while m.transactions["holder"].state != "authorized":
            self.assertTrue(m.step())
        self.assertEqual(m.transactions["holder"].state, "authorized")
        self.assertFalse(m.invalidate("holder", ["p"], "test"))
        m.check()

    def test_multi_key_acquisition_leaves_no_partial_grant(self):
        both = part("both", "A", "x")
        both["locks"]["y"] = "W"
        s = base("all or none", [transaction("a", [part("held", "A", "y")]),
                                 transaction("b", [both])])
        m = Model(s)
        m.step()
        self.assertEqual(m.grants, {("a", "held")})
        self.assertEqual(m.part(("b", "both")).state, "ready")

    def test_readers_share_but_writer_waits(self):
        m = Model(presets()["readers"])
        m.step()
        self.assertEqual(len(m.grants), 4)
        while m.time < 100:
            m.step()
        self.assertNotEqual(m.transactions["W"].state, "complete")
        while m.step():
            pass
        self.assertEqual(m.summary()["completed"], 5)

    def test_fork_join_waits_for_all_parents(self):
        s = base("join", [transaction("t", [part("a", "A", "x"), part("b", "B", "y"),
                                                     part("join", "A", "z", ["a", "b"])])])
        r = run(s)
        trace = r["trace"]
        prep = next(i for i, e in enumerate(trace) if e["type"] == "prepare" and e["part"] == "join")
        before = {e["part"] for e in trace[:prep] if e["type"] == "report"}
        self.assertEqual(before, {"a", "b"})

    def test_replay_and_input_iteration_order(self):
        s = workload(count=18, seed=23)
        a = run(s)
        self.assertEqual(canonical(a), canonical(run(s)))
        reverse = copy.deepcopy(s)
        reverse["transactions"].reverse()
        for tx in reverse["transactions"]:
            tx["parts"].reverse()
        b = run(reverse)
        self.assertEqual(a["summary"], b["summary"])
        # Arrival log order is authored; same-time deterministic epoch choices
        # still produce the same final logical state for this workload.
        self.assertEqual(a["final"]["transactions"], b["final"]["transactions"])

    def test_hotspot_slider_preserves_other_workload_inputs(self):
        def without_locks(s):
            txs = copy.deepcopy(s["transactions"])
            for tx in txs:
                for p in tx["parts"]:
                    del p["locks"]
            return txs
        self.assertEqual(without_locks(workload(hot_percent=0)), without_locks(workload(hot_percent=100)))

    def test_seeded_workloads_and_epoch_ties(self):
        for policy, order, seed in itertools.product(POLICIES, (["A", "B"], ["B", "A"]), range(4)):
            with self.subTest(policy=policy, order=order, seed=seed):
                s = workload(count=14, seed=seed)
                s["horizon_us"] = 4000
                s["policy"]["yield"] = policy
                s["shard_order"] = order
                r = run(s)
                summary = r["summary"]
                self.assertEqual(summary["completed"] + summary["pending"] + summary["future"], 14)

    def test_invalid_dag_and_timing_fail_at_boundary(self):
        s = discovery()
        s["transactions"][0]["parts"][0]["after"] = ["write-A"]
        with self.assertRaisesRegex(ValueError, "dependency cycle"):
            validate(s)
        s = discovery()
        s["shards"]["A"]["period_us"] = 0
        with self.assertRaises(ValueError):
            validate(s)
        s = discovery()
        s["loss_probability"] = .1
        with self.assertRaisesRegex(ValueError, "unsupported"):
            validate(s)

    def test_future_arrivals_and_step_limit_are_not_completions(self):
        s = discovery()
        s["transactions"][0]["arrival_us"] = 100000
        r = run(s, max_steps=4)
        self.assertEqual(r["stop_reason"], "step_limit")
        self.assertEqual(r["summary"]["future"], 1)
        self.assertIsNone(r["summary"]["completion_fraction"])


if __name__ == "__main__":
    unittest.main()
