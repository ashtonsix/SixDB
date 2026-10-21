#!/usr/bin/env python3
"""Paired direct, complete-window manual, and actual Loom consumption."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument("binaries", type=Path)
p.add_argument("output", type=Path)
p.add_argument("--cpu", default="0")
p.add_argument("--staged", action="store_true", help="Pair ID-before-payload staging with ordinary consumers")
args = p.parse_args()
(args.output / "samples").mkdir(exist_ok=True)
packed = ["materialized", "borrow", "slots", "slots32"]
peers = ["abseil", "boost", "f14", "f14value", "abseil_slots", "boost_slots", "f14_slots"]
plan = []
geometries = [(32, 17, 64), (40, 17, 64), (32, 17, 32), (32, 16, 128)]
if args.staged: geometries.append((32, 18, 64))
for w, b, v in geometries:
    n = 2673868 if b == 18 else (1 << b) * 8 * (63 // (w - b + 1)) * 85 // 100
    for trial, seed in enumerate([713, 991, 1729]):
        arms = ["slots", "slots32"] if b == 18 else packed + peers
        arms = arms[trial * 3:] + arms[:trial * 3]
        for arm in arms:
            binary = args.binaries / (f"wide_ikea_{w}_{b}_{v}" if arm in packed else f"wide_peers_{w}_{v}")
            modes = [("direct", 0, 0, 0), ("manual", 16, 0, 0), ("loom", 0, 1, 0)]
            if args.staged and arm in ["slots", "slots32", "abseil_slots", "boost_slots", "f14_slots"]:
                modes += [("manual-staged", 16, 0, 1), ("loom-staged", 0, 1, 1)]
            for mode, pipeline, loom, staged in modes:
                distances = [0, 32] if not args.staged or mode == "direct" else [32]
                for distance in distances:
                    case = f"{w}-{b}-{v}-{seed}-{arm}-{mode}-{distance}"
                    cmd = ["taskset", "-c", args.cpu, str(binary), "--impl", arm, "--phase", "reads",
                           "--population", str(n), "--requests", "262144", "--repetitions", "2",
                           "--seed", str(seed), "--temperature", "cold", "--hint", "auto" if arm in packed else "p",
                           "--prefetch", str(distance), "--pipeline", str(pipeline), "--loom", str(loom)]
                    if args.staged: cmd += ["--staged", str(staged)]
                    plan.append({"case": case, "argv": cmd})
            # F14's explicit external-slot control also gets a complete write /
            # growth panel, not just a new read arm with no lifecycle costs.
            if arm == "f14_slots" and not args.staged:
                for phase in ["writes", "growth"]:
                    case = f"{w}-{b}-{v}-{seed}-{arm}-{phase}"
                    cmd = ["taskset", "-c", args.cpu, str(binary), "--impl", arm, "--phase", phase,
                           "--population", str(n), "--requests", "65536", "--repetitions", "1",
                           "--seed", str(seed), "--temperature", "cold", "--hint", "p"]
                    plan.append({"case": case, "argv": cmd})
(args.output / "plan.json").write_text(json.dumps(plan, indent=2) + "\n")
with (args.output / "progress.jsonl").open("a", buffering=1) as progress:
    for index, case in enumerate(plan):
        path = args.output / "samples" / (case["case"] + ".csv")
        progress.write(json.dumps({"index": index, "total": len(plan), "case": case["case"], "phase": "start", "time": time.time()}) + "\n")
        with path.open("w") as output, path.with_suffix(".stderr").open("w") as error:
            result = subprocess.run(case["argv"], stdout=output, stderr=error, check=False)
        path.with_suffix(".json").write_text(json.dumps({**case, "returncode": result.returncode,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}, indent=2) + "\n")
        progress.write(json.dumps({"index": index, "case": case["case"], "returncode": result.returncode, "phase": "end", "time": time.time()}) + "\n")
        if result.returncode: raise SystemExit(path.with_suffix(".stderr").read_text())
subprocess.run(["python3", str(Path(__file__).with_name("report.py")), str(args.output)], check=True)
