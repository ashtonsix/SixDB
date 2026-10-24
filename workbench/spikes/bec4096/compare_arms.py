#!/usr/bin/env python3
"""Median ratio per case between two Google Benchmark sample files: compare_arms.py BEFORE AFTER"""
import json
import statistics
import sys


def medians(path):
    data = json.load(open(path))
    runs = {}
    for b in data["benchmarks"]:
        if b.get("run_type") == "iteration":
            runs.setdefault(b["run_name"], []).append(b["real_time"])
        elif b.get("aggregate_name") == "median":
            runs[b["run_name"]] = [b["real_time"]]
    return {k: statistics.median(v) for k, v in runs.items()}


before, after = medians(sys.argv[1]), medians(sys.argv[2])
ratios = []
for name in before:
    if name in after:
        r = after[name] / before[name]
        ratios.append((r, name, before[name], after[name]))
ratios.sort()
print(f"cases {len(ratios)}  geomean after/before {statistics.geometric_mean([r for r, *_ in ratios]):.4f}")
for item in ratios[:5] + [None] + ratios[-5:]:
    if item is None:
        print("...")
        continue
    r, name, b, a = item
    print(f"{r:.3f}  {b:9.2f} -> {a:9.2f}  {name}")
