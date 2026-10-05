#!/usr/bin/env python3
"""Validate equal-work receipts and summarize retained control-path trials."""
import csv
import json
import pathlib
import statistics
import sys

root = pathlib.Path(sys.argv[1])
paths = sorted(root.glob("forward-*.csv")) + sorted(root.glob("reverse-*.csv"))
if len(paths) != 6:
    raise SystemExit("Expected three forward and three reverse trials")
samples = {}
trial_keys = []
for path in paths:
    keys = set()
    work = {}
    with path.open() as source:
        for row in csv.DictReader(source):
            key = (row["family"], row["policy"], row["shape"], row["threads"])
            if key in keys:
                raise SystemExit(f"Duplicate cell in {path}: {key}")
            keys.add(key)
            receipt = tuple(int(row[k]) for k in ("operations", "units", "checksum"))
            family_key = (row["family"], row["shape"], row["threads"])
            if work.setdefault(family_key, receipt) != receipt:
                raise SystemExit(f"Unequal work across policies: {family_key}")
            previous = samples.setdefault(key, [])
            if previous and tuple(int(previous[0][k]) for k in ("operations", "units", "checksum", "holds")) != tuple(
                int(row[k]) for k in ("operations", "units", "checksum", "holds")
            ):
                raise SystemExit(f"Changed work across trials: {key}")
            previous.append(row)
    trial_keys.append(keys)
if any(keys != trial_keys[0] for keys in trial_keys) or len(trial_keys[0]) != 72:
    raise SystemExit("Expected the same 72 cells in every trial")

summary = []
for key, rows in sorted(samples.items()):
    values = [float(r["ns_per_operation"]) for r in rows]
    units = [float(r["ns_per_unit"]) for r in rows]
    summary.append(dict(zip(("family", "policy", "shape", "threads"), key)) | {
        "operations": rows[0]["operations"], "units": rows[0]["units"],
        "checksum": rows[0]["checksum"], "holds": rows[0]["holds"], "trials": len(rows),
        "median_ns_per_operation": statistics.median(values),
        "min_ns_per_operation": min(values), "max_ns_per_operation": max(values),
        "median_ns_per_unit": statistics.median(units),
    })

def write(path, rows):
    with path.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

write(root / "summary.csv", summary)
baseline = {"ownership": "global", "readiness": "ordered", "lifetime": "window",
            "history": "ordered", "combined": "ordered-page"}
index = {(r["family"], r["policy"], r["shape"], r["threads"]): r for r in summary}
comparisons = []
for row in summary:
    original = baseline[row["family"]]
    if row["policy"] == original:
        continue
    base = index[(row["family"], original, row["shape"], row["threads"])]
    comparisons.append({k: row[k] for k in ("family", "policy", "shape", "threads")} | {
        "baseline": original,
        "baseline_ns_per_operation": base["median_ns_per_operation"],
        "variant_ns_per_operation": row["median_ns_per_operation"],
        "baseline_over_variant": base["median_ns_per_operation"] / row["median_ns_per_operation"],
    })
write(root / "comparisons.csv", comparisons)
print(json.dumps({"status": "passed", "cells": len(summary), "trials": len(paths),
                  "summary": str(root / "summary.csv")}))
