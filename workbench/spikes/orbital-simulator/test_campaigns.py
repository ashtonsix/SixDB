"""Selection and accounting hazards, including real shared-World overload."""
from itertools import combinations as pairs
import json
from pathlib import Path
import tempfile
import unittest

from campaigns import Observation, Runner, all_work, cohort, combinations, hill_climb, ramp
from flow_campaign import evaluate


def result(completed=10, refused=0, violations=(), **metrics):
    return Observation(dict(requests=dict(offered=10, completed=completed, refused=refused,
                                          unfinished=10 - completed - refused)), metrics, list(violations))


class CampaignTests(unittest.TestCase):
    def test_pairwise_covers_feasible_pairs_without_inventing_forbidden_case(self):
        axes = dict(topology=["a", "b", "c"], strategy=["direct", "relay"], failure=[0, 1], window=[1, 4])
        allowed = lambda c: not (c["topology"] == "c" and c["strategy"] == "relay")
        full = combinations(axes, accept=allowed)
        reduced = combinations(axes, mode="pairwise", accept=allowed)
        self.assertLess(len(reduced), len(full))
        for a, b in pairs(axes, 2):
            self.assertEqual({(c[a], c[b]) for c in reduced}, {(c[a], c[b]) for c in full})
        self.assertTrue(all(allowed(c) for c in reduced))

    def test_unprocessed_arrivals_survive_and_latency_includes_pre_handler_queue(self):
        group = cohort({"a": 0, "b": 10, "never_delivered": 20}, {"a": 30}, {"b": 25},
                       offered_until=25, until=100)
        self.assertEqual(group["unfinished_age_ns"], {"never_delivered": 80})
        self.assertEqual(group["latency_ns"], {"a": 30})
        self.assertEqual(group["completed_in_window"], 0)
        with self.assertRaises(ValueError):
            cohort({"a": 0}, {"a": 20}, {"a": 10}, offered_until=5, until=50)

    def test_false_conservation_and_nonfinite_metrics_are_errors(self):
        for invalid in (result(completed=11), result(latency=float("nan"))):
            runner = Runner(lambda *_: invalid)
            record = runner.run({}, 1)
            self.assertEqual(record["status"], "error")
            self.assertFalse(all_work(record))

    def test_error_receipt_and_exact_case_memoization(self):
        calls = []
        def fail(case, seed, output):
            calls.append((case, seed))
            raise RuntimeError("simulator event budget exhausted")
        with tempfile.TemporaryDirectory() as root:
            output = Path(root) / "campaign"
            runner = Runner(fail, output, {"source": "test"})
            first = runner.run({"window": 2}, 7)
            runner.run({"window": 2}, 7)
            runner.run({"window": 2}, 19)
            self.assertEqual(len(calls), 2)
            saved = json.loads((output / (first["id"] + ".json")).read_text())
            self.assertEqual(saved["status"], "error")
            self.assertIn("event budget", saved["exception"])

    def test_ramp_keeps_reversal_and_simulator_limit_is_not_saturation(self):
        def evaluate(case, *_):
            if case["load"] == 3:
                raise RuntimeError("event budget")
            return result(latency=100 if case["load"] == 2 else 1)
        rows = ramp(Runner(evaluate), {}, "load", [1, 2, 3, 4], [1, 7], {"latency": 10})
        self.assertEqual(rows["first_outside"], 2)
        self.assertEqual([r["state"] for r in rows["levels"]], ["within", "outside", "unknown", "within"])

    def test_fast_dropping_or_wrong_strategies_cannot_win_search(self):
        calls = []
        def evaluate(case, seed, output):
            calls.append((case["policy"], seed, case.get("heldout", False)))
            policy = case["policy"]
            if policy == "drop":
                return result(completed=1, refused=9, latency=1)
            if policy == "wrong":
                return result(latency=0, violations=["wrong result"])
            if case.get("heldout") and policy == "faster":
                return result(completed=8, latency=1)
            return result(latency=10 if policy == "baseline" else 5)
        study = hill_climb(Runner(evaluate), [{"policy": "baseline"}],
            lambda c: [{"policy": p} for p in ("drop", "wrong", "faster")],
            seeds=[1, 7], heldout_seeds=[19], objectives=["latency"],
            validation_cases=lambda c: [dict(c, heldout=True)], max_cases=8)
        self.assertEqual([e["case"]["policy"] for e in study["frontier"]], ["faster"])
        self.assertEqual({v["strategy"]["policy"]: v["feasible"] for v in study["validation"]},
                         {"baseline": True, "faster": False})
        self.assertTrue(all(seed == 19 for _, seed, heldout in calls if heldout))
        with self.assertRaises(ValueError):
            hill_climb(Runner(evaluate), [], lambda c: [], seeds=[1], heldout_seeds=[1], objectives=["latency"])

    def test_real_flow_retains_admission_refusals_and_absent_materializations(self):
        runner = Runner(evaluate)
        record = runner.run(dict(memory_bytes=600), 7)
        self.assertEqual(record["status"], "ok")
        groups = record["observation"]["cohorts"]
        self.assertEqual(groups["sources"]["offered"], 2)
        self.assertEqual(groups["sources"]["refused"], 2)
        self.assertEqual(groups["materializations"]["unfinished"], 2)
        self.assertIsNone(record["observation"]["metrics"]["finalization_ns"])
        self.assertFalse(all_work(record))

    def test_real_overload_increases_latency_and_drains_after_offering(self):
        runner = Runner(evaluate)
        low = runner.run(dict(period_ns=4_000), 7)
        high = runner.run(dict(period_ns=400), 7)
        self.assertTrue(all_work(low))
        self.assertTrue(all_work(high))
        a, b = [r["observation"]["metrics"] for r in (low, high)]
        self.assertGreater(b["foreground_max_ns"], a["foreground_max_ns"])
        self.assertGreater(b["foreground_drain_ns"], 0)
        self.assertLess(b["foreground_throughput_per_model_ms"], b["offered_per_model_ms"])
        obs = high["observation"]
        self.assertGreater(obs["metrics"]["foreground_max_ns"], obs["details"]["actor_reported_foreground"]["max_ns"])

    def test_real_paused_worker_is_censored_and_event_limit_is_error_with_trace(self):
        runner = Runner(evaluate)
        paused = runner.run(dict(incident_ns=1_000_000), 1)
        self.assertEqual(paused["status"], "ok")
        self.assertGreater(paused["observation"]["cohorts"]["foreground"]["unfinished"], 0)
        self.assertIsNone(paused["observation"]["metrics"]["foreground_drain_ns"])
        with tempfile.TemporaryDirectory() as root:
            runner = Runner(evaluate, Path(root) / "campaign")
            limited = runner.run(dict(event_budget=1), 1)
            self.assertEqual(limited["status"], "error")
            self.assertTrue((runner.output / (limited["id"] + ".evidence") / "trace.jsonl").is_file())


if __name__ == "__main__":
    unittest.main()
