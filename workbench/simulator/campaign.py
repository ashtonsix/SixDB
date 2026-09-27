"""Programmatic experiment clients for the native simulator library's CLI.

Uses ordinary mappings/callables rather than a scenario language. The native C++
API remains available for custom actors, topologies and interactive clients.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import itertools
import json
import os
from pathlib import Path
import subprocess
import sys


@dataclass(frozen=True)
class Trial:
    case: dict
    result: dict
    returncode: int


def grid(base, **axes):
    """All combinations, with input dictionaries independent of their neighbors."""
    names = tuple(axes)
    for values in itertools.product(*(axes[name] for name in names)):
        yield dict(base, **dict(zip(names, values)))


def evaluate(binary, case, *, trace=None, choices=None, replay=None, artifacts=None):
    argv = [str(Path(binary).resolve())]
    for key, value in case.items():
        if isinstance(value, bool):
            if value:
                argv.append("--" + key)
        else:
            argv.extend(["--" + key, str(value)])
    if artifacts:
        artifacts = Path(artifacts)
        artifacts.mkdir(parents=True, exist_ok=False)
        choices = choices or artifacts / "choices.txt"
        (artifacts / "case.json").write_text(json.dumps(case, indent=2) + "\n")
    for key, value in (("trace", trace), ("choices", choices), ("replay", replay)):
        if value:
            argv.extend(["--" + key, str(value)])
    if artifacts:
        (artifacts / "argv.json").write_text(json.dumps(argv, indent=2) + "\n")
        with (artifacts / "stdout.json").open("w") as out, (artifacts / "stderr.txt").open("w") as err:
            process = subprocess.run(argv, text=True, stdout=out, stderr=err, check=False, timeout=120)
        process.stdout = (artifacts / "stdout.json").read_text()
        process.stderr = (artifacts / "stderr.txt").read_text()
        (artifacts / "exit.json").write_text(json.dumps({"returncode": process.returncode}) + "\n")
    else:
        process = subprocess.run(argv, text=True, capture_output=True, check=False, timeout=120)
    if process.returncode not in (0, 2):
        raise RuntimeError(f"simulator failed ({process.returncode}): {process.stderr.strip()}")
    result = json.loads(process.stdout)
    for name, cohort in result["cohorts"].items():
        if cohort["offered"] != cohort["completed"] + cohort["failed"] + cohort["unfinished"]:
            raise ValueError(f"offer accounting failed for {name}")
    if bool(result["violations"]) != (process.returncode == 2):
        raise ValueError("exit status disagrees with violation report")
    return Trial(dict(case), result, process.returncode)


def ramp(binary, base, intervals, seeds=(1, 7, 19)):
    """Keep overloaded and recovery-side samples; do not assume monotonicity."""
    for case in grid(base, interval=intervals, seed=seeds):
        yield evaluate(binary, case)


def pareto(trials, objectives):
    """Minimize callable objectives, excluding unsafe or censored histories.

    The caller chooses goals/cohorts. An unfinished experiment is not a fast
    successful experiment; callers studying survival must analyze it separately.
    """
    valid = [t for t in trials if not t.result["violations"] and
             not t.result.get("execution", {}).get("budget_exhausted", False) and
             not t.result.get("missing_incidents", []) and
             all(not c["unfinished"] and not c["failed"] for c in t.result["cohorts"].values())]
    scored = [(t, tuple(f(t.result) for f in objectives)) for t in valid]
    return [t for t, score in scored if not any(
        all(a <= b for a, b in zip(other, score)) and any(a < b for a, b in zip(other, score))
        for _, other in scored)]


def cases(suite):
    baseline = {"points": 18, "until": 10_000_000}
    if suite == "smoke":
        yield from grid(baseline, seed=(1, 7), incident=("none", "consumer-reset", "coordinator-reset", "checker-reset"))
    elif suite == "gauntlet":
        # A common semantic workload, with physical sharing, delay and offered
        # load changed separately. The WAN has the same arrivals and deadline;
        # unfinished work is deliberately retained, not silently given longer.
        yield from grid(baseline, seed=(1, 7, 19), link=(1_000, 100_000, 20_000_000),
                        interval=(5_000, 40_000), incident=("none", "one-follower", "quorum-pause", "consumer-reset", "coordinator-reset", "checker-reset"))
        yield from grid(baseline, seed=(1, 7, 19), placement=("shared",),
                        memory=(32_768, 131_072, 16_777_216), interval=(5_000, 40_000))
        # Give the WAN a separate, declared observation window as well. A retry
        # period useful on the LAN is a candidate to falsify, not a universal
        # default whose censored prefix can be compared as completed throughput.
        yield from grid({"points": 6, "link": 20_000_000, "until": 3_000_000_000,
                         "events": 200_000}, seed=(1, 7, 19), retry=(100_000, 80_000_000))
    else:
        raise ValueError(f"unknown suite {suite}")


def run_suite(binary, suite, output):
    output.mkdir(parents=True, exist_ok=False)
    trials, errors = [], []
    with (output / "trials.jsonl").open("w") as journal:
        for index, case in enumerate(cases(suite)):
            artifact = output / "cases" / str(index)
            try:
                trial = evaluate(binary, dict(case, name=f"{suite}-{index}"), artifacts=artifact)
                row = {"case": trial.case, "result": trial.result, "artifacts": str(artifact)}
                trials.append(row)
            except Exception as error:
                row = {"case": case, "error": str(error), "artifacts": str(artifact)}
                errors.append(row)
            journal.write(json.dumps(row, separators=(",", ":")) + "\n")
            journal.flush()
            os.fsync(journal.fileno())
    result = {"format": 1, "suite": suite, "trials": trials, "errors": errors,
              "units": "authored modeled nanoseconds and bytes; no production calibration"}
    temporary = output / "summary.tmp"
    temporary.write_text(json.dumps(result, indent=2) + "\n")
    temporary.replace(output / "summary.json")
    print(json.dumps({"histories": len(trials), "errors": len(errors),
                      "unsafe": sum(bool(t["result"]["violations"]) for t in trials)}))
    return bool(errors or any(t["result"]["violations"] for t in trials))


def captured(suite, output, workspace):
    root = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(root / "workbench/tools"))
    from experiment import Run
    run = Run(root, output, {"suite": suite}, workspace=workspace)
    failure = None
    try:
        run.step("configure", ["cmake", "-S", str(run.source_root), "-B", str(run.build_dir),
                               "-G", "Ninja", "-DCMAKE_BUILD_TYPE=RelWithDebInfo"])
        run.step("build", ["cmake", "--build", str(run.build_dir), "--target", "simulator_validate", "simulator_run", "-j", "4"])
        run.step("campaign", [sys.executable, str(run.source_root / "workbench/simulator/campaign.py"),
                              "--binary", str(run.build_dir / "workbench/simulator/simulator_run"),
                              "--suite", suite, "--output", str(output / "campaign")])
        run.compact(["campaign/summary.json"], [])
    except Exception as error:
        failure = error
    failure = run.finish(failure)
    if failure:
        raise failure


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, help="explicit existing binary; omit for captured build")
    parser.add_argument("--suite", choices=("smoke", "gauntlet"), default="smoke")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--workspace", type=Path, default=Path("build/workspaces/simulator"))
    args = parser.parse_args()
    if args.binary:
        raise SystemExit(run_suite(args.binary, args.suite, args.output.resolve()))
    captured(args.suite, args.output.resolve(), args.workspace.resolve())
