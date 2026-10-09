#!/usr/bin/env python3
"""Compare two frozen Allocation source trees; keep receipts/results in build/."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import sys
import os


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, type=Path, help="tree containing orbital/")
    parser.add_argument("--candidate", required=True, type=Path, help="frozen first-three control tree")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--repetitions", type=int, default=7)
    parser.add_argument("--samples", type=int, default=512)
    parser.add_argument("--cold-samples", type=int, default=64)
    parser.add_argument("--cpus", type=int, nargs=3, metavar=("OWNER", "FOREIGN", "HELPER"))
    parser.add_argument("--sanitize", action="store_true", help="qualify both complete runtimes before timing")
    args = parser.parse_args()
    if platform.system() != "Linux":
        parser.error("run through orb -m ubuntu python3")
    if args.repetitions < 3 or args.samples < 2 or args.cold_samples < 2:
        parser.error("need >=3 repetitions and >=2 samples per phase")
    available = sorted(os.sched_getaffinity(0))
    cpus = args.cpus or available[:3]
    if len(set(cpus)) != 3 or not set(cpus) <= set(available):
        parser.error("three distinct permitted CPUs are required")
    version = subprocess.check_output(["clang++-21", "--version"], text=True).splitlines()[0]
    if "21.1.8" not in version:
        parser.error(f"expected pinned Clang 21.1.8, found {version}")
    here = Path(__file__).resolve().parent
    root = Path(subprocess.check_output(["git", "rev-parse", "--show-toplevel"], cwd=here, text=True).strip())
    output = (args.output or root / "build/orbital-mvcc-metadata/controls-cost" /
              datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")).resolve()
    sys.path.insert(0, str(root / "workbench/tools"))
    from experiment import Run
    run = Run(root, output, {
        "compiler": version, "target": platform.machine(), "cpus": cpus,
        "repetitions": args.repetitions, "samples": args.samples, "cold_samples": args.cold_samples,
        "scope": "local AArch64 OrbStack guest; batch wall-time quantiles, not point latency or Zen5",
        "units": "owner cycle/foreign return=1 of 64 per batch; trim/map=whole operation",
        "cleanup": "equal close/remove/reset/relinquish; trim to zero idle payload in both",
        "order": "baseline/candidate alternate by repetition; no concurrent competitors",
        "timing_overhead": "two steady_clock calls per batch; no overhead subtraction",
    }, workspace=root / "build/orbital-mvcc-metadata/controls-cost-workspace")
    error = None
    try:
        probe = run.source_root / here.relative_to(root) / "controls_cost.cpp"
        flags = ["clang++-21", "-std=c++23", "-g", "-DNDEBUG", "-pthread", "-Wall", "-Wextra",
                 "-Wpedantic", "-fno-fast-math", "-ffp-contract=off", "-mtune=generic"]
        binaries = {}
        runtime = ["types.cpp", "worker.cpp", "allocation/allocation.cpp", "allocation/arena.cpp",
                   "allocation/group.cpp", "allocation/window.cpp", "allocation/finite.cpp"]
        run.receipt["runtime_inputs"] = {}
        for label in ["baseline", "candidate"]:
            source = getattr(args, label).resolve()
            captured = output / "runtime" / label
            for folder in ["include", "src"]:
                shutil.copytree(source / "orbital" / folder, captured / "orbital" / folder)
            hashes = {str(p.relative_to(captured)): hashlib.sha256(p.read_bytes()).hexdigest()
                      for p in sorted(captured.rglob("*")) if p.is_file()}
            run.receipt["runtime_inputs"][label] = {"source": str(source), "sha256": hashes}
            run.save()
            include = ["-I" + str(captured / "orbital/include"),
                       "-I" + str(captured / "orbital/src/allocation")]
            sources = [captured / "orbital/src" / path for path in runtime]
            binary = output / f"{label}-native"
            run.step(f"build-{label}", flags + ["-O3", *include, probe, *sources, "-o", binary])
            binaries[label] = binary
            if args.sanitize:
                sanitized = output / f"{label}-sanitize"
                run.step(f"build-{label}-sanitize", flags + ["-O1", "-fsanitize=address,undefined",
                         "-fno-omit-frame-pointer", *include, probe, *sources, "-o", sanitized])
                run.step(f"check-{label}-sanitize", ["env", "ASAN_OPTIONS=detect_leaks=1",
                         sanitized, "8", "3", *map(str, cpus)])
        rows = []
        for repetition in range(args.repetitions):
            order = ["baseline", "candidate"] if repetition % 2 == 0 else ["candidate", "baseline"]
            for label in order:
                name = f"{repetition:02d}-{label}"
                run.step(name, [binaries[label], str(args.samples), str(args.cold_samples), *map(str, cpus)],
                         name + ".jsonl")
                current = [json.loads(line) for line in (output / (name + ".jsonl")).read_text().splitlines()]
                if len(current) != 14:
                    raise ValueError(f"expected all 14 phases, found {len(current)}")
                rows.extend(dict(row, variant=label, repetition=repetition) for row in current)
        comparisons = []
        for phase, palette in sorted({(row["phase"], row["palette"]) for row in rows}):
            paired = {label: sorted([r for r in rows if r["phase"] == phase and r["palette"] == palette
                                    and r["variant"] == label], key=lambda r: r["repetition"])
                      for label in binaries}
            comparison = {"phase": phase, "palette": palette, "repetitions": args.repetitions}
            for metric in ["p50_ns_per_unit", "p99_ns_per_unit"]:
                for label, group in paired.items():
                    values = [row[metric] for row in group]
                    comparison[label + "_" + metric] = {"median": statistics.median(values),
                                                        "min": min(values), "max": max(values)}
                ratios = [b[metric] / a[metric] for a, b in zip(paired["baseline"], paired["candidate"])]
                comparison["candidate_over_baseline_" + metric] = statistics.median(ratios)
            comparisons.append(comparison)
        (output / "summary.json").write_text(json.dumps(comparisons, indent=2) + "\n")
        run.compact(["summary.json"], [])
    except Exception as exc:
        error = exc
    error = run.finish(error)
    if error:
        raise error
    print(f"Comparison complete: {output}")


if __name__ == "__main__":
    main()
