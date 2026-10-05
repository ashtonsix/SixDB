#!/usr/bin/env python3
"""Check retained-page work; bucket labels do not measure cache residency."""
import csv
import json
import math
from pathlib import Path
import statistics
import sys

root = Path(sys.argv[1])
assert json.loads((root / "checks.jsonl").read_text())["result"] == "pass"
expected = {("hot", "hot"), ("warm", "warm"), ("cold", "cold"), ("hot", "cold")}
groups = {key: [] for key in expected}
for trial in (1, 2, 3):
    for order in ("forward", "reverse"):
        seen = set()
        for row in csv.DictReader((root / f"{order}-{trial}.csv").open()):
            key = row["request"], row["release"]
            assert key in expected and key not in seen
            seen.add(key)
            assert int(row["iterations"]) == int(row["reused"]) == 10000
            assert int(row["new_maps"]) == int(row["unmaps"]) == 0
            assert int(row["fallback"]) == (10000 if key == ("hot", "cold") else 0)
            assert all(math.isfinite(float(row[x])) for x in ("mean_ns", "release_ns"))
            assert float(row["mean_ns"]) >= float(row["release_ns"]) >= 0
            groups[key].append(row)
        assert seen == expected
fields = ["request", "release", "trials", "iterations_per_trial", "median_mean_ns", "median_release_ns",
          "min_mean_ns", "max_mean_ns", "new_maps", "unmaps", "reused", "fallback"]
with (root / "comparisons.csv").open("w", newline="") as file:
    writer = csv.DictWriter(file, fieldnames=fields); writer.writeheader()
    for key, rows in sorted(groups.items()):
        means = [float(r["mean_ns"]) for r in rows]
        writer.writerow(dict(zip(fields, [*key, len(rows), 10000, round(statistics.median(means), 3),
            round(statistics.median(float(r["release_ns"]) for r in rows), 3), min(means), max(means),
            *[int(rows[0][x]) for x in ("new_maps", "unmaps", "reused", "fallback")]])))
print(json.dumps({"result": "pass", "cells": len(groups), "trials": 6, "iterations_per_trial": 10000}))
