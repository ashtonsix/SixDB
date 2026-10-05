#!/usr/bin/env python3
import csv
import json
import pathlib
import statistics
import sys

root = pathlib.Path(sys.argv[1])
paths = sorted(root.glob("cut-forward-*.csv")) + sorted(root.glob("cut-reverse-*.csv"))
if len(paths) != 6:
    raise SystemExit("Expected six cutoff trials")
samples = {}
keys_by_trial = []
for path in paths:
    keys = set()
    for row in csv.DictReader(path.open()):
        key = (row["family"], row["policy"], row["producers"], row["cadence"])
        if key in keys:
            raise SystemExit(f"Duplicate cell: {key}")
        keys.add(key)
        n = int(row["publications"])
        p, cadence = int(row["producers"]), int(row["cadence"])
        q, checksum = int(row["queries"]), int(row["checksum"])
        if row["family"] == "updates":
            expected_q = 1 + (n // cadence if cadence else 0)
            expected_sum = 136 * n * (n + 1) // 2 + 84 * n
            if n != p * 32768 or q != expected_q or checksum != expected_sum or int(row["final_cut"]) != n:
                raise SystemExit(f"Incorrect publication/query work: {key}")
            if int(row["max_unqueried_updates"]) != (cadence or 32768):
                raise SystemExit(f"Incorrect query cadence receipt: {key}")
        elif q != 1048576 or int(row["query_checksum"]) != q * (136 * p + 84) or n != 0:
            raise SystemExit(f"Incorrect readonly query work: {key}")
        samples.setdefault(key, []).append(row)
    keys_by_trial.append(keys)
if len(keys_by_trial[0]) != 24 or any(k != keys_by_trial[0] for k in keys_by_trial):
    raise SystemExit("Expected the same 24 cells in all trials")

summary = []
for key, rows in sorted(samples.items()):
    metric = "ns_per_publication" if key[0] == "updates" else "ns_per_query"
    costs = [float(row[metric]) for row in rows]
    summary.append(dict(zip(("family", "policy", "producers", "cadence"), key)) | {
        "publications": rows[0]["publications"], "queries": rows[0]["queries"],
        "checksum": rows[0]["checksum"], "trials": len(rows), "cost_unit": metric,
        "median_cost_ns": statistics.median(costs), "min_cost_ns": min(costs), "max_cost_ns": max(costs),
        "median_max_head_distance": statistics.median(int(r["max_head_distance"]) for r in rows),
        "min_max_head_distance": min(int(r["max_head_distance"]) for r in rows),
        "max_max_head_distance": max(int(r["max_head_distance"]) for r in rows),
        "max_unqueried_updates": rows[0]["max_unqueried_updates"],
    })

def write(path, rows):
    with path.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

write(root / "cut-summary.csv", summary)
index = {(r["family"], r["policy"], r["producers"], r["cadence"]): r for r in summary}
comparisons = []
for row in summary:
    if row["policy"] != "producer-counters":
        continue
    base = index[(row["family"], "shared-bitmap", row["producers"], row["cadence"])]
    comparisons.append({k: row[k] for k in ("family", "producers", "cadence", "cost_unit")} | {
        "shared_bitmap_ns": base["median_cost_ns"], "producer_counters_ns": row["median_cost_ns"],
        "shared_over_counters": base["median_cost_ns"] / row["median_cost_ns"],
    })
write(root / "cut-comparisons.csv", comparisons)
print(json.dumps({"status": "passed", "cells": len(summary), "trials": len(paths)}))
