#!/usr/bin/env python3
"""Alternate the transpose variants case by case and summarize CPU ns.

OrbStack pins a virtual CPU, not a physical performance core, so each case runs
in several short rounds with variant order reversed each round. The summary
reports the minimum and median of per-round medians for every variant, and
each candidate's speedup over the first variant by median (and by minimum).
"""
import argparse, json, statistics, subprocess, sys
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("build", type=Path)
parser.add_argument("--variants", default="original,bit4,current")
parser.add_argument("--rounds", type=int, default=6)
parser.add_argument("--filter", default=".*")
parser.add_argument("--cpu", default="2")
parser.add_argument("--min-time", default="0.03s")
parser.add_argument("--output", type=Path)
args = parser.parse_args()
variants = args.variants.split(",")
binary = lambda v: args.build / f"neon_local_transpose_bench_{v}"
cases = subprocess.run([binary(variants[0]), f"--benchmark_filter={args.filter}",
                        "--benchmark_list_tests=true"], capture_output=True, text=True,
                       check=True).stdout.split()
samples = {c: {v: [] for v in variants} for c in cases}
for case in cases:
    for r in range(args.rounds):
        for v in (variants if r % 2 == 0 else variants[::-1]):
            out = subprocess.run(["taskset", "-c", args.cpu, binary(v),
                                  f"--benchmark_filter=^{case}$", "--benchmark_format=json",
                                  f"--benchmark_min_time={args.min_time}",
                                  "--benchmark_repetitions=3"],
                                 capture_output=True, text=True, check=True).stdout
            runs = [b["cpu_time"] for b in json.loads(out)["benchmarks"]
                    if b.get("run_type") == "iteration"]
            samples[case][v].append(statistics.median(runs))
base = variants[0]
print(f"{'case':24}" + "".join(f"{v + ' min':>14}{v + ' med':>14}" for v in variants)
      + "".join(f"{'x ' + v + ' med':>14}{'x ' + v + ' min':>14}" for v in variants[1:]))
for case in cases:
    s = samples[case]
    row = f"{case:24}" + "".join(f"{min(s[v]):14.0f}{statistics.median(s[v]):14.0f}" for v in variants)
    row += "".join(f"{statistics.median(s[base]) / statistics.median(s[v]):14.3f}"
                   f"{min(s[base]) / min(s[v]):14.3f}" for v in variants[1:])
    print(row)
if args.output:
    args.output.write_text(json.dumps(samples, indent=1))
