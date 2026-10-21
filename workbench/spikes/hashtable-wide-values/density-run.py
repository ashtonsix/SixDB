#!/usr/bin/env python3
"""Bounded V64 ablation: spend capacity on the index, not on payload holes."""
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
a = p.parse_args()
(a.output / "samples").mkdir(exist_ok=True)
packed = ["materialized", "borrow", "slots", "slots32"]
peers = ["abseil", "boost", "f14", "f14value", "abseil_slots", "boost_slots", "f14_slots"]
plan = []
for b, n, temperature in [(9, 6963, "warm"), (17, 2673868, "cold")]:
    for trial, seed in enumerate([713, 991, 1729]):
        arms = [(arm, b) for arm in packed + peers] + [(arm, b + 1) for arm in ["slots", "slots32"]]
        shift = trial * 4 % len(arms)
        arms = arms[shift:] + arms[:shift]
        for arm, index_b in arms:
            binary = a.binaries / (f"wide_ikea_32_{index_b}_64" if arm in packed else "wide_peers_32_64")
            modes = [(0, 0, 0)] if temperature == "warm" else [(0, 0, 0), (32, 0, 0), (32, 16, 0), (32, 0, 1)]
            for phase in ["reads", "writes", "growth"]:
                for pf, pipeline, loom in modes if phase == "reads" else [(0, 0, 0)]:
                    name = f"32-{index_b}-64-{temperature}-{seed}-{phase}-{arm}-{pf}-{pipeline}-{loom}"
                    cmd = ["taskset", "-c", a.cpu, str(binary), "--impl", arm, "--phase", phase,
                           "--population", str(n), "--seed", str(seed), "--temperature", temperature,
                           "--requests", str(262144 if temperature == "cold" and phase == "reads" else 65536),
                           "--repetitions", str(3 if temperature == "warm" else 2 if phase == "reads" else 1),
                           "--hint", "auto" if arm in packed and phase == "reads" else "p",
                           "--prefetch", str(pf), "--pipeline", str(pipeline), "--loom", str(loom)]
                    plan.append({"case": name, "argv": cmd})
(a.output / "plan.json").write_text(json.dumps(plan, indent=2) + "\n")
with (a.output / "progress.jsonl").open("a", buffering=1) as progress:
    for index, case in enumerate(plan):
        path = a.output / "samples" / (case["case"] + ".csv")
        progress.write(json.dumps({"index": index, "case": case["case"], "phase": "start", "time": time.time()}) + "\n")
        with path.open("w") as out, path.with_suffix(".stderr").open("w") as err:
            r = subprocess.run(case["argv"], stdout=out, stderr=err, check=False)
        path.with_suffix(".json").write_text(json.dumps({**case, "returncode": r.returncode,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}, indent=2) + "\n")
        progress.write(json.dumps({"index": index, "case": case["case"], "phase": "end", "returncode": r.returncode, "time": time.time()}) + "\n")
        if r.returncode: raise SystemExit(path.with_suffix(".stderr").read_text())
subprocess.run(["python3", str(Path(__file__).with_name("report.py")), str(a.output)], check=True)
