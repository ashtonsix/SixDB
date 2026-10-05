#!/usr/bin/env python3
"""Check the retained comparison's coverage/work, then combine trial means."""
import csv
import json
import math
import statistics
import sys
from pathlib import Path

root = Path(sys.argv[1])
check = json.loads((root / "checks.jsonl").read_text())
assert check["result"] == "pass" and check["histories"] == 120
page = check["page_bytes"]
expected = {(n * page, selected, method, protected)
            for n in (1, 16, 256) for selected in {1, n}
            for method in ("copy", "cow", "take") for protected in (0, 1)}
groups = {key: [] for key in expected}
files = [root / f"{order}-{trial}.csv" for trial in (1, 2, 3) for order in ("forward", "reverse")]
for path in files:
    seen = set()
    for row in csv.DictReader(path.open()):
        key = (int(row["part_bytes"]), int(row["prepared_pages"]), row["method"], int(row["hardware_protection"]))
        assert key in expected and key not in seen, (path, key)
        seen.add(key)
        method, size = key[2], key[1] * page
        assert int(row["copy_bytes"]) == (size if method == "copy" else 0), row
        assert int(row["mapped_bytes"]) == (0 if method == "take" else size), row
        assert int(row["source_bytes"]) == 0 and int(row["repeats"]) == 120, row
        for name in ("mean_ns", "prepare_ns", "close_ns", "minor_faults"):
            assert math.isfinite(float(row[name])) and float(row[name]) >= 0, row
        assert float(row["mean_ns"]) >= float(row["prepare_ns"]) + float(row["close_ns"]) - 0.01, row
        groups[key].append(row)
    assert seen == expected, (path, expected - seen)
fields = ["part_bytes", "prepared_pages", "method", "hardware_protection", "trials", "iterations_per_trial",
          "median_mean_ns", "min_mean_ns", "max_mean_ns", "median_prepare_ns", "median_close_ns",
          "median_minor_faults", "copy_bytes", "mapped_bytes", "source_bytes"]
with (root / "comparisons.csv").open("w", newline="") as output:
    writer = csv.DictWriter(output, fieldnames=fields)
    writer.writeheader()
    for key, rows in sorted(groups.items()):
        timings = [float(r["mean_ns"]) for r in rows]
        result = dict(zip(fields[:4], key))
        result.update(trials=len(rows), iterations_per_trial=120,
                      median_mean_ns=round(statistics.median(timings), 3),
                      min_mean_ns=min(timings), max_mean_ns=max(timings))
        for name in ("prepare_ns", "close_ns", "minor_faults"):
            result[f"median_{name}"] = round(statistics.median(float(r[name]) for r in rows), 3)
        for name in ("copy_bytes", "mapped_bytes", "source_bytes"):
            result[name] = int(rows[0][name])
        writer.writerow(result)
print(json.dumps({"result": "pass", "cells": len(groups), "trials_per_cell": 6,
                  "iterations_per_trial": 120, "input_files": [p.name for p in files]}))
