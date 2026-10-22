#!/usr/bin/env python3
"""Compare maintained SeriesPack suite runs: original versus current sources.

Each case's time is the median over trials of per-trial median CPU ns. Prints
the distribution of current/original speedups and every case outside ±3%.
"""
import json, statistics, sys
from pathlib import Path

root = Path(sys.argv[1])
times = {}
for path in root.glob("*-*.json"):
    variant = path.stem.rsplit("-", 1)[0]
    runs = {}
    for b in json.loads(path.read_text())["benchmarks"]:
        if b.get("run_type") == "iteration":
            runs.setdefault(b["run_name"], []).append(b["cpu_time"])
    for case, values in runs.items():
        times.setdefault(case, {}).setdefault(variant, []).append(statistics.median(values))
speed = {case: statistics.median(v["original"]) / statistics.median(v["current"])
         for case, v in times.items() if "original" in v and "current" in v}
values = sorted(speed.values())
geomean = statistics.geometric_mean(values)
print(f"cases {len(values)}; speedup current/original: geomean {geomean:.3f}, "
      f"median {statistics.median(values):.3f}, min {values[0]:.3f}, max {values[-1]:.3f}")
print(f"faster >3%: {sum(x > 1.03 for x in values)}; slower >3%: {sum(x < 1 / 1.03 for x in values)}")
for case in sorted(speed, key=speed.get):
    if not 1 / 1.03 <= speed[case] <= 1.03:
        o, c = statistics.median(times[case]["original"]), statistics.median(times[case]["current"])
        print(f"{speed[case]:7.3f}  {o:11.1f} -> {c:11.1f} ns  {case}")
