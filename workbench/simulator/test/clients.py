"""Independent CLI/JSON/replay and experiment-client checks.

Uses the executable boundary and public Python clients; imports no Orbital fold.
Small trace-corruption controls challenge the observer, not the physical model.
"""
from __future__ import annotations

import argparse
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT.parents[1] / "build/clang/dev/workbench/simulator/simulator_run"


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    sys.modules[name] = loaded
    spec.loader.exec_module(loaded)
    return loaded


campaign = module("simulator_campaign_client_check", ROOT / "campaign.py")
trace = module("simulator_trace_client_check", ROOT / "trace.py")


class ClientChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not BINARY.is_file():
            raise unittest.SkipTest(f"build simulator_run first: {BINARY}")
        cls.scratch = tempfile.TemporaryDirectory(prefix="sixdb-simulator-clients-")
        cls.addClassCleanup(cls.scratch.cleanup)
        cls.directory = Path(cls.scratch.name)
        cls.case = {"points": 3, "until": 6_000_000, "seed": 7}
        cls.trace_path = cls.directory / "baseline.jsonl"
        cls.choices_path = cls.directory / "baseline.choices"
        cls.traced = campaign.evaluate(BINARY, cls.case, trace=cls.trace_path, choices=cls.choices_path)

    def test_trace_and_choice_observers_do_not_change_the_experiment(self):
        plain = campaign.evaluate(BINARY, self.case)
        self.assertEqual(plain.result, self.traced.result)
        self.assertEqual(plain.returncode, 0)
        self.assertFalse(plain.result["violations"])
        for cohort in plain.result["cohorts"].values():
            self.assertEqual(cohort["completed"], cohort["offered"])
            self.assertEqual(cohort["failed"] + cohort["unfinished"], 0)
        self.assertEqual(sum(c["offered"] for c in plain.result["cohorts"].values()), 4)

    def test_exact_choice_replay_reproduces_json_and_causal_trace(self):
        replay_path = self.directory / "replayed.jsonl"
        replayed = campaign.evaluate(BINARY, self.case, trace=replay_path, replay=self.choices_path)
        self.assertEqual(replayed.result, self.traced.result)
        self.assertEqual(replay_path.read_bytes(), self.trace_path.read_bytes())
        with self.assertRaisesRegex(RuntimeError, "replay"):
            campaign.evaluate(BINARY, dict(self.case, points=4), replay=self.choices_path)

    def test_incident_coverage_is_identical_with_and_without_a_trace(self):
        case = dict(self.case, incident="consumer-reset")
        plain = campaign.evaluate(BINARY, case)
        traced = campaign.evaluate(BINARY, case, trace=self.directory / "incident.jsonl")
        self.assertEqual(plain.result, traced.result)
        self.assertFalse(plain.result["violations"])
        self.assertTrue(plain.result["triggered_incidents"])
        self.assertEqual(plain.result["missing_incidents"], [])

    def test_deadline_and_event_budget_preserve_unfinished_denominators(self):
        # All authored arrivals precede this cutoff; the delayed checked
        # transaction must still be reported as unfinished.
        for extra in ({"until": 700_000}, {"events": 1}):
            with self.subTest(extra=extra):
                result = campaign.evaluate(BINARY, dict(self.case, **extra)).result
                self.assertFalse(result["violations"])
                self.assertGreater(sum(c["unfinished"] for c in result["cohorts"].values()), 0)
                for cohort in result["cohorts"].values():
                    self.assertEqual(cohort["offered"], cohort["completed"] + cohort["failed"] + cohort["unfinished"])
                if "events" in extra:
                    self.assertTrue(result["execution"]["budget_exhausted"])
                    self.assertEqual(result["execution"]["events"], 1)

    def test_unsafe_negative_control_cannot_enter_the_good_frontier(self):
        unsafe = campaign.evaluate(BINARY, dict(self.case, negative="skip-verification", delay=10_000_000))
        self.assertEqual(unsafe.returncode, 2)
        self.assertTrue(unsafe.result["violations"])
        self.assertEqual(campaign.pareto([unsafe], [lambda result: 0]), [])

    def test_pareto_excludes_censored_work_even_after_client_completions(self):
        healthy = self.traced
        exhausted_result = copy.deepcopy(healthy.result)
        exhausted_result["execution"]["budget_exhausted"] = True
        exhausted = campaign.Trial({"name": "exhausted"}, exhausted_result, 0)
        unfinished_result = copy.deepcopy(healthy.result)
        group = next(iter(unfinished_result["cohorts"].values()))
        group["offered"] += 1
        group["unfinished"] += 1
        unfinished = campaign.Trial({"name": "unfinished"}, unfinished_result, 0)
        unsafe_result = copy.deepcopy(healthy.result)
        unsafe_result["violations"] = ["deliberately unsafe observer control"]
        unsafe = campaign.Trial({"name": "unsafe"}, unsafe_result, 2)
        unexercised_result = copy.deepcopy(healthy.result)
        unexercised_result["missing_incidents"] = ["requested incident never exercised"]
        unexercised = campaign.Trial({"name": "unexercised"}, unexercised_result, 0)
        self.assertEqual(campaign.pareto([healthy, exhausted, unfinished, unsafe, unexercised],
                                        [lambda result: 0]), [healthy])

    def test_trace_summary_and_causal_slice_use_the_emitted_records(self):
        summary = trace.summary(self.trace_path)
        self.assertEqual(summary["violations"], [])
        self.assertEqual(summary["records"], self.traced.result["records"])
        self.assertGreater(summary["counts"].get("buffer.allocate", 0), 0)
        last = None
        for row in trace.records(self.trace_path):
            last = row
        self.assertIsNotNone(last)
        selected = list(trace.slice_history(self.trace_path, [last["id"]]))
        identities = {row["id"] for row in selected}
        self.assertIn(last["id"], identities)
        self.assertEqual([row["id"] for row in selected], sorted(identities))
        for row in selected:
            for parent in [row["cause"], *row["parents"]]:
                self.assertTrue(parent == 0 or parent in identities)
        with self.assertRaisesRegex(ValueError, "missing causal record"):
            list(trace.slice_history(self.trace_path, [last["id"] + 1]))
        process = subprocess.run([sys.executable, str(ROOT / "trace.py"), str(self.trace_path)],
                                 text=True, capture_output=True, check=False)
        self.assertEqual(process.returncode, 0, process.stderr)
        # JSON object keys are strings; normalize the in-process integer host keys.
        self.assertEqual(json.loads(process.stdout), json.loads(json.dumps(summary)))

    def test_trace_observer_rejects_corrupt_causality_and_retirement(self):
        row = {"id": 1, "cause": 0, "parents": [], "kind": "buffer.allocate",
               "operation": 7, "host": 1, "size": 8}
        malformed = [row, dict(row, id=2, kind="buffer.retire", operation=99),
                     dict(row, id=3, kind="observation", cause=4)]
        path = self.directory / "invalid-trace.jsonl"
        path.write_text("".join(json.dumps(item) + "\n" for item in malformed))
        result = trace.summary(path)
        self.assertTrue(any("unmatched buffer retirement" in error for error in result["violations"]))
        self.assertTrue(any("invalid causal predecessor" in error for error in result["violations"]))

    def test_per_case_artifacts_preserve_inputs_results_and_exact_choices(self):
        for unsafe in (False, True):
            with self.subTest(unsafe=unsafe):
                case = dict(self.case)
                if unsafe:
                    case.update(negative="skip-verification", delay=10_000_000)
                destination = self.directory / ("artifacts-unsafe" if unsafe else "artifacts-good")
                trial = campaign.evaluate(BINARY, case, artifacts=destination)
                self.assertEqual(json.loads((destination / "case.json").read_text()), case)
                argv = json.loads((destination / "argv.json").read_text())
                self.assertEqual(Path(argv[0]), BINARY.resolve())
                self.assertEqual(Path(argv[argv.index("--choices") + 1]), destination / "choices.txt")
                self.assertEqual(json.loads((destination / "stdout.json").read_text()), trial.result)
                self.assertEqual(json.loads((destination / "exit.json").read_text()),
                                 {"returncode": 2 if unsafe else 0})
                self.assertTrue((destination / "stderr.txt").is_file())
                choices = (destination / "choices.txt").read_text()
                self.assertTrue(choices.startswith("S "))
                self.assertIn("\nD ", choices)
                self.assertEqual(bool(trial.result["violations"]), unsafe)
                if not unsafe:
                    self.assertEqual(choices, self.choices_path.read_text())

    def test_custom_case_iterator_preserves_errors_and_following_trials(self):
        destination = self.directory / "custom-cases"
        cases = [{"points": -1}, dict(self.case, events=1)]
        failed = campaign.run_cases(BINARY, iter(cases), destination)
        self.assertTrue(failed)
        result = json.loads((destination / "summary.json").read_text())
        self.assertEqual(len(result["errors"]), 1)
        self.assertEqual(result["errors"][0]["case"], cases[0])
        self.assertEqual(len(result["trials"]), 1)
        self.assertEqual(result["trials"][0]["case"], cases[1])
        self.assertTrue(result["trials"][0]["result"]["execution"]["budget_exhausted"])
        self.assertEqual(len((destination / "trials.jsonl").read_text().splitlines()), 2)

    def test_replay_input_cannot_be_truncated_through_path_aliases(self):
        for alias in ("relative", "symlink"):
            with self.subTest(alias=alias):
                protected = self.directory / f"protected-{alias}.choices"
                protected.write_bytes(self.choices_path.read_bytes())
                if alias == "relative":
                    output = str(protected.parent) + "/./" + protected.name
                else:
                    output = self.directory / "choice-symlink"
                    output.symlink_to(protected)
                process = subprocess.run([str(BINARY), "--replay", str(protected), "--trace", str(output)],
                                         text=True, capture_output=True, check=False)
                self.assertEqual(process.returncode, 1)
                self.assertEqual(protected.read_bytes(), self.choices_path.read_bytes(),
                                 "CLI opened an aliased output before protecting replay input")

    def test_bad_cli_options_are_errors_not_empty_successful_experiments(self):
        for arguments in (("--points", "-1"), ("--placement", "imaginary"), ("--unknown", "1")):
            with self.subTest(arguments=arguments):
                process = subprocess.run([str(BINARY), *arguments], text=True, capture_output=True, check=False)
                self.assertEqual(process.returncode, 1)
                self.assertFalse(process.stdout.strip())
                self.assertTrue(process.stderr.strip())

    @unittest.skipUnless(Path("/dev/full").exists(), "Linux full-device fixture unavailable")
    def test_failed_trace_or_choice_output_is_not_reported_as_success(self):
        for output in ("trace", "choices"):
            with self.subTest(output=output):
                process = subprocess.run([str(BINARY), "--points", "0", "--until", "6000000",
                                          "--" + output, "/dev/full"],
                                         text=True, capture_output=True, check=False)
                self.assertEqual(process.returncode, 1)
                self.assertFalse(process.stdout.strip())
                self.assertRegex(process.stderr, "trace|choice")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=BINARY)
    args, remaining = parser.parse_known_args()
    BINARY = args.binary.resolve()
    unittest.main(argv=[sys.argv[0], *remaining])
