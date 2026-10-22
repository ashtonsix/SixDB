#!/usr/bin/env python3
"""Compare maintained SeriesPack suite runs of two source variants.

Each case's time is the median over trials of per-trial median CPU ns. Prints
the distribution of candidate speedups over the base and every case outside ±3%.
"""
import argparse, json, statistics
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
parser.add_argument("--base", default="original")
parser.add_argument("--candidate", default="current")
args = parser.parse_args()
root, base, candidate = args.root, args.base, args.candidate
times = {}
for path in root.glob("*-*.json"):
    variant = path.stem.rsplit("-", 1)[0]
    runs = {}
    for b in json.loads(path.read_text())["benchmarks"]:
        if b.get("run_type") == "iteration":
            runs.setdefault(b["run_name"], []).append(b["cpu_time"])
    for case, values in runs.items():
        times.setdefault(case, {}).setdefault(variant, []).append(statistics.median(values))
speed = {case: statistics.median(v[base]) / statistics.median(v[candidate])
         for case, v in times.items() if base in v and candidate in v}
values = sorted(speed.values())
geomean = statistics.geometric_mean(values)
print(f"cases {len(values)}; speedup {candidate} over {base}: geomean {geomean:.3f}, "
      f"median {statistics.median(values):.3f}, min {values[0]:.3f}, max {values[-1]:.3f}")
print(f"faster >3%: {sum(x > 1.03 for x in values)}; slower >3%: {sum(x < 1 / 1.03 for x in values)}")
for case in sorted(speed, key=speed.get):
    if not 1 / 1.03 <= speed[case] <= 1.03:
        o, c = statistics.median(times[case][base]), statistics.median(times[case][candidate])
        print(f"{speed[case]:7.3f}  {o:11.1f} -> {c:11.1f} ns  {case}")
