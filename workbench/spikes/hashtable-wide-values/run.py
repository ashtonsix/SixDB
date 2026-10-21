#!/usr/bin/env python3
"""Sequential, counterbalanced complete-operation comparisons, with exact argv receipts."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def capacity(w, b):
    k = w - b
    return (1 << w) if k <= 9 else (1 << b) * 8 * (((64 if k <= 30 else 96) - 1) // (k + 1))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binaries", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--mode", choices=["pilot", "full", "check"], default="pilot")
    parser.add_argument("--cpu", default="0")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "samples").mkdir(exist_ok=True)
    library = ["abseil", "boost", "f14", "f14value", "abseil_slots", "boost_slots"]
    candidates = ["materialized", "borrow", "slots", "slots32"]
    values = [8, 64] if args.mode == "pilot" else [64, 8, 16, 32, 128]
    seeds = [713] if args.mode != "full" else [713, 991, 1729]
    plan = []
    for v in values:
        cold_b = 18 if v <= 16 else 16 if v == 128 else 17
        geometries = [(32, 9, "warm"), (32, cold_b if args.mode == "full" else 17, "cold")]
        if args.mode == "full" and v == 8:
            geometries.append((32, 11, "warm"))
        if args.mode == "full" and v == 64:
            geometries.append((40, 17, "cold"))
        for w, b, temperature in geometries:
            population = 13926 if v == 8 and b == 11 else capacity(w, b) * 85 // 100
            for seed_index, seed in enumerate(seeds):
                arms = candidates + library
                shift = seed_index * 3 % len(arms)
                arms = arms[shift:] + arms[:shift]
                for phase in (["check"] if args.mode == "check" else ["reads", "writes", "growth"]):
                    order = list(reversed(arms)) if phase == "writes" else arms
                    for arm in order:
                        binary = args.binaries / (f"wide_ikea_{w}_{b}_{v}" if arm in candidates else f"wide_peers_{w}_{v}")
                        requests = (65536 if temperature == "warm" else 524288) if phase == "reads" else 65536
                        hints = (["p", "u", "auto"] if v in [8, 64] else ["auto"]) if phase == "reads" and arm in candidates else ["p"]
                        distances = [0, 32] if phase == "reads" and temperature == "cold" else [0]
                        pipelines = [0, 16] if phase == "reads" and temperature == "cold" and v >= 32 else [0]
                        repetitions = 2 if args.mode == "pilot" else (1 if temperature == "cold" and phase != "reads" else 3)
                        for hint in hints:
                            for distance in distances:
                                for pipeline in pipelines:
                                    # Diagnose P/U separately on the ordinary path;
                                    # the pipeline comparison uses automatic candidates.
                                    if pipeline and arm in candidates and hint != "auto":
                                        continue
                                    case = f"{w}-{b}-{v}-{temperature}-{seed}-{phase}-{arm}-{hint}-{distance}-{pipeline}"
                                    cmd = [str(binary), "--impl", arm, "--phase", phase,
                                           "--population", str(min(population, 1024) if phase == "check" else population),
                                           "--requests", str(requests), "--seed", str(seed),
                                           "--repetitions", str(repetitions),
                                           "--hint", hint, "--prefetch", str(distance), "--pipeline", str(pipeline),
                                           "--temperature", temperature if phase != "check" else "warm"]
                                    plan.append({"case": case, "argv": ["taskset", "-c", args.cpu, *cmd]})
    (args.output / "plan.json").write_text(json.dumps(plan, indent=2) + "\n")
    with (args.output / "progress.jsonl").open("a", buffering=1) as progress:
        for index, case in enumerate(plan):
            output = args.output / "samples" / (case["case"] + ".csv")
            receipt = output.with_suffix(".json")
            if receipt.exists() and json.loads(receipt.read_text()).get("returncode") == 0:
                continue
            progress.write(json.dumps({"index": index, "total": len(plan), "case": case["case"], "phase": "start", "time": time.time()}) + "\n")
            start = time.monotonic()
            with output.open("w") as stdout, output.with_suffix(".stderr").open("w") as stderr:
                result = subprocess.run(case["argv"], stdout=stdout, stderr=stderr, check=False)
            receipt.write_text(json.dumps({**case, "returncode": result.returncode,
                                          "seconds": time.monotonic() - start,
                                          "sha256": hashlib.sha256(output.read_bytes()).hexdigest()}, indent=2) + "\n")
            progress.write(json.dumps({"index": index, "case": case["case"], "phase": "end", "returncode": result.returncode, "time": time.time()}) + "\n")
            if result.returncode:
                raise SystemExit(f"failed {case['case']}: {output.with_suffix('.stderr').read_text()}")
    subprocess.run([os.sys.executable, str(Path(__file__).with_name("report.py")), str(args.output)], check=True)


if __name__ == "__main__":
    main()
