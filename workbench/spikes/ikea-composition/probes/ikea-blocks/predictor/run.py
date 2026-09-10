#!/usr/bin/env python3
"""Capture and run the BEC256 scalar-size model experiment."""
import argparse
import json
from datetime import datetime, timezone
import os
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[6]
sys.path.insert(0, str(ROOT / "workbench/tools"))
import datasets
from experiment import Run
from evidence import digest, verify_compact


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cap-per-stratum", type=int, default=2000)
    args = parser.parse_args()
    inputs = {name: datasets.get(name) for name in ("msmarco-keyset", "real-roaring")}
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    base = Path(os.environ.get("SIXDB_RESULTS", ROOT / "build/experiments/ikea-predictor"))
    output = base / stamp
    run = Run(ROOT, output, vars(args), workspace=ROOT / "build/workspaces/ikea-predictor")
    error = None
    try:
        paths = {name: run.input("inputs/" + name, path) for name, path in inputs.items()}
        run.step("train", [sys.executable, str(run.source_root / "workbench/spikes/ikea-composition/probes/ikea-blocks/predictor/train.py"),
                           "--msmarco", str(paths["msmarco-keyset"]), "--roaring", str(paths["real-roaring"]),
                           "--cap-per-stratum", str(args.cap_per_stratum), "--output", str(output)])
        predictor = run.source_root / "workbench/spikes/ikea-composition/probes/ikea-blocks/predictor"
        run.step("refine", [sys.executable, str(predictor / "refine.py"), "--sample", str(output / "sample.npz"),
                            "--output", str(output)])
        frozen = ROOT / "workbench/spikes/ikea-composition/probes/ikea-blocks/predictor/evidence/20260909"
        frozen_meta = verify_compact(frozen)
        shutil.copyfile(frozen / "refinement-selected.json", output / "frozen-models.json")
        run.step("one-runs", [sys.executable, str(predictor / "compare_runs.py"), "--sample", str(output / "sample.npz"),
                              "--frozen-models", str(output / "frozen-models.json"), "--output", str(output)])
        run.step("compiler", ["clang++-21", "--version"])
        if "version 21.1.8" not in (output / "compiler.stdout").read_text():
            raise RuntimeError("Predictor check requires the pinned Clang 21.1.8")
        run.step("compile-check", ["clang++-21", "-std=c++23", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
                                   str(predictor / "check_model.cpp"), "-o", str(output / "check_model")])
        run.step("check-model", [str(output / "check_model"), str(output / "vectors.bin"), str(output / "structured-vectors.bin")], "check-model.csv")
        candidates = ["selected.json", "summary.csv", "training-provenance.json", "sampling.csv", "check-model.csv",
                      "refinement-selected.json", "refinement-summary.csv", "refinement-provenance.json",
                      "one-run-comparison.csv", "one-run-models.json", "one-run-provenance.json"]
        reused = {name: digest(output / name) for name in candidates
                  if frozen_meta["files_sha256"].get(name) == digest(output / name)}
        (output / "reused-evidence.json").write_text(json.dumps({
            "evidence": str(frozen.relative_to(ROOT)), "provenance_sha256": digest(frozen / "provenance.json"),
            "frozen_model": "refinement-selected.json", "files_sha256": reused,
        }, indent=2, sort_keys=True) + "\n")
        run.compact([name for name in candidates if name not in reused] + ["reused-evidence.json"], [])
    except Exception as exc:
        error = exc
    error = run.finish(error)
    print(output)
    if error:
        raise error


if __name__ == "__main__":
    main()
