#!/usr/bin/env python3
"""Selected contrasts for component independence, compatibility and epoch folds."""

import argparse
import copy
import hashlib
import json
from pathlib import Path

from folds import probe
from run import execute, model_identity
from scenarios import independent_components, read_overlap, reservation_cycle, workload


def measure(name, scenario, lifetime, reservations="compatible", delay=1200):
    scenario = copy.deepcopy(scenario)
    scenario["policy"].update({"yield": lifetime, "reservations": reservations, "batch_us": 500,
                               "arbitration_us": delay, "solver": "age", "fast_retries": True,
                               "local_solver": False})
    result = execute(scenario)
    authored = {"independent": "independent", "read-overlap": "overlap", "verdict-lifetime": "reservation"}
    source = {"preset": authored[name]} if name in authored else {"generator": scenario["generator"]}
    row = {"case": name, "input": {**source, "policy": result["scenario"]["policy"]},
            "scenario_sha256": result["scenario_sha256"],
            "trace_sha256": result["trace_sha256"], "stop_reason": result["stop_reason"],
            "summary": result["summary"]}
    if name in authored:
        row["completions_us"] = {t["id"]: t["completed_us"] for t in result["final"]["transactions"]}
        row["first_components"] = [{k: e[k] for k in ("time_us", "members", "keys", "edges")}
                                   for e in result["trace"] if e["type"] == "component"][:4]
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    rows = []
    for name, scenario in (("independent", independent_components()), ("read-overlap", read_overlap())):
        for lifetime in ("batch-independent", "batch-hold"):
            for reservations in ("compatible", "scope"):
                rows.append(measure(name, scenario, lifetime, reservations))
    for delay in (100, 1200):
        for lifetime in ("batch-reset", "batch-hold", "batch-independent"):
            rows.append(measure("verdict-lifetime", reservation_cycle(), lifetime, delay=delay))
    for seed in (0, 1, 7, 19, 23):
        for lifetime in ("batch-independent", "batch-hold"):
            rows.append(measure(f"hotspot-seed-{seed}", workload(seed=seed), lifetime))
    root = Path(__file__).parent
    result = {"model_files_sha256": model_identity(),
              "study_files_sha256": {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
                                     for name in ("study.py", "folds.py")},
              "max_steps": 2000,
              "assumptions": "Complete multishard collection, atomic verdicts/invalidation, authored DAGs, no failures or CPU charge. Synthetic time only.",
              "comparisons": rows, "folds": probe()}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(f"Wrote {len(rows)} comparisons and the fold probe to {args.output}")


if __name__ == "__main__":
    main()
