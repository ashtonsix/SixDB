#!/usr/bin/env python3
"""Run or replay the Orbital contention slice, or compare its yield policies."""

import argparse
import copy
import hashlib
import json
from pathlib import Path

from model import POLICIES, canonical, run
from scenarios import presets


ROOT = Path(__file__).resolve().parent


def model_identity():
    files = ["model.py", "batch.py", "scenarios.py", "run.py"]
    return {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in files}


LOADED_IDENTITY = model_identity()


def execute(scenario, max_steps=2000, frames=False):
    if model_identity() != LOADED_IDENTITY:
        raise ValueError("Model sources changed. Restart the server before running again.")
    result = run(scenario, max_steps, frames)
    if model_identity() != LOADED_IDENTITY:
        raise ValueError("Model sources changed during this run. Restart the server and rerun.")
    result["model_files_sha256"] = LOADED_IDENTITY
    result["model_sha256"] = hashlib.sha256(canonical(LOADED_IDENTITY).encode()).hexdigest()
    result["context_files_sha256"] = {"MODEL.md": hashlib.sha256((ROOT / "MODEL.md").read_bytes()).hexdigest()}
    return result


def compare(scenario, max_steps=2000, policies=POLICIES):
    rows = []
    for policy in policies:
        candidate = copy.deepcopy(scenario)
        candidate.setdefault("policy", {})["yield"] = policy
        result = execute(candidate, max_steps)
        rows.append({"policy": policy, "scenario_sha256": result["scenario_sha256"],
                     "trace_sha256": result["trace_sha256"], "stop_reason": result["stop_reason"],
                     **result["summary"]})
    return {"scenario": scenario, "model_files_sha256": model_identity(), "max_steps": max_steps, "comparisons": rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scenario", nargs="?", type=Path)
    parser.add_argument("--preset", choices=presets(), default="independent")
    parser.add_argument("--policy", choices=POLICIES)
    parser.add_argument("--steps", type=int, default=2000)
    parser.add_argument("--compare", action="store_true")
    parser.add_argument("--frames", action="store_true")
    parser.add_argument("--replay", type=Path, help="verify a saved run including model identity and trace")
    parser.add_argument("--output", type=Path, help="write full run JSON to an ignored output path")
    args = parser.parse_args()
    if args.replay:
        old = json.loads(args.replay.read_text())
        if old["model_files_sha256"] != model_identity():
            raise SystemExit("Model sources changed: rerun the saved scenario as a new comparison.")
        result = execute(old["scenario"], old["max_steps"])
        for key in ("trace_sha256", "summary", "stop_reason", "final"):
            if result[key] != old[key]:
                raise SystemExit(f"Replay differs: {key}")
        print(f"Replay matches {result['trace_sha256']}")
        return
    scenario = json.loads(args.scenario.read_text()) if args.scenario else presets()[args.preset]
    # Exported runs can also be used as scenario inputs after source changes.
    scenario = scenario.get("scenario", scenario)
    if args.policy:
        scenario.setdefault("policy", {})["yield"] = args.policy
    result = compare(scenario, args.steps) if args.compare else execute(scenario, args.steps, args.frames)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(canonical(result) + "\n")
    print(json.dumps(result.get("comparisons", result.get("summary")), indent=2))


if __name__ == "__main__":
    main()
