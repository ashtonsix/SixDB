#!/usr/bin/env python3
"""Configure, build, verify, measure sequentially, and analyse one local run."""

from __future__ import annotations

import argparse
import csv
import os
import re
import shutil
import sys
from datetime import datetime, timezone
from pathlib import Path

STUDY = Path(__file__).resolve().parent
ROOT = STUDY.parents[2]
sys.path.insert(0, str(ROOT / "workbench/tools"))
from experiment import Run  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=["smoke", "screen"], default="smoke")
    parser.add_argument("--case", action="append", dest="cases", help="Exact case name; repeat to select several")
    parser.add_argument("--cpu", type=int, help="Default: lowest CPU in the permitted affinity set")
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--min-time", type=float, default=0.05)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/clang/aggregate-maintenance")
    parser.add_argument("--output", type=Path, help="New directory under build/; defaults to a unique timestamp")
    args = parser.parse_args()
    if not hasattr(os, "sched_getaffinity"):
        parser.error("run on Linux; from macOS use orb -m ubuntu python3 workbench/spikes/aggregate-maintenance/run.py")
    if args.repetitions < 1 or args.min_time <= 0:
        parser.error("repetitions and min-time must be positive")
    allowed = sorted(os.sched_getaffinity(0))
    cpu = min(allowed) if args.cpu is None else args.cpu
    if cpu not in allowed:
        parser.error(f"CPU {cpu} is outside permitted affinity {allowed}")
    build = args.build_dir.resolve()
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    output = (args.output or ROOT / "build/experiments/aggregate-maintenance" / stamp).resolve()
    if not output.is_relative_to(ROOT / "build"):
        parser.error("place raw run artifacts under build/ so they are excluded from source snapshots")
    if output.exists():
        parser.error(f"output already exists: {output}")
    config = vars(args) | {"cpu": cpu, "allowed_cpus": allowed, "build_dir": str(build), "output": str(output)}
    config["min_time"] = args.min_time
    run = Run(ROOT, output, config)
    print(f"Run: {output}", flush=True)
    error = None
    try:
        run.step("git-head", ["git", "rev-parse", "HEAD"])
        run.step("git-status", ["git", "status", "--short"])
        run.step("hardware", ["lscpu"])
        run.step("compiler", ["clang++-21", "--version"])
        run.step("dev-configure", [sys.executable, str(ROOT / "workbench/tools/dev.py"),
            "--add", "aggregate-maintenance"])
        run.step("configure", ["cmake", "-S", str(ROOT), "-B", str(build), "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release", "-DSIXDB_SPIKES=aggregate-maintenance",
            "-DSIXDB_TUNE=generic", "-DSIXDB_MARCH="])
        run.step("build", ["cmake", "--build", str(build), "--target",
            "aggregate_deltas_check", "aggregate_deltas_bench", "-j", "4"])
        for filename in ["CMakeCache.txt", "compile_commands.json"]:
            shutil.copyfile(build / filename, output / filename)
        binary_dir = build / "workbench/spikes/aggregate-maintenance"
        (output / "bin").mkdir()
        for filename in ["aggregate_deltas_check", "aggregate_deltas_bench"]:
            shutil.copy2(binary_dir / filename, output / "bin" / filename)
        run.step("binary-size", ["llvm-size-21", str(output / "bin/aggregate_deltas_bench")])
        os.sched_setaffinity(0, {cpu})
        run.receipt["measurement_affinity"] = sorted(os.sched_getaffinity(0))
        run.step("check", [str(output / "bin/aggregate_deltas_check"), args.profile], "accounting.csv")
        with (output / "accounting.csv").open() as handle:
            available = {row["case"] for row in csv.DictReader(handle)}
        unknown = set(args.cases or []) - available
        if unknown:
            raise ValueError(f"unknown cases: {', '.join(sorted(unknown))}; available: {', '.join(sorted(available))}")
        selection = "^(" + "|".join(re.escape(case) for case in args.cases) + ")/" if args.cases else "."
        run.step("benchmark", [str(output / "bin/aggregate_deltas_bench"), f"--probe_profile={args.profile}",
            f"--benchmark_filter={selection}", f"--benchmark_min_time={args.min_time}s",
            f"--benchmark_repetitions={args.repetitions}", "--benchmark_enable_random_interleaving=false",
            "--benchmark_display_aggregates_only=true", "--benchmark_report_aggregates_only=false",
            "--benchmark_color=false", f"--benchmark_out={output / 'benchmark.json'}",
            "--benchmark_out_format=json"])
        run.step("analyse", [sys.executable, str(STUDY / "analyze.py"), str(output)])
    except Exception as exc:
        error = exc
    finally:
        os.sched_setaffinity(0, set(allowed))
        error = run.finish(error)
    if error:
        print(f"FAILED: {error}", file=sys.stderr)
        return 1
    print(f"Complete: {output / 'summary.md'}")
    print(f"To keep this run: python3 workbench/tools/artifacts.py retain {output.relative_to(ROOT)} "
          "workbench/spikes/aggregate-maintenance/evidence/NAME")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
