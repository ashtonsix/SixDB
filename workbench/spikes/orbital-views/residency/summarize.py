#!/usr/bin/env python3
import csv
import json
from pathlib import Path
from statistics import median
import sys

directory = Path(sys.argv[1])
methods = ["page_copy", "run_copy", "file_lazy", "file_runs", "file_populate",
           "anon_eager", "retained_copy", "retained_take", "pool_rebind"]
profiles = ["small", "dense", "sparse_runs", "sparse_scattered"]
readers = ["none", "held", "late_map", "late_pinned", "late_1_in_8"]
expected = {(m, p, r) for m in methods for p in profiles for r in readers}
trials = sorted(directory.glob("forward-*.csv")) + sorted(directory.glob("reverse-*.csv"))
if len(trials) != 6:
    raise SystemExit(f"Expected six pipeline trials, got {len(trials)}")
cells = {}
phases = ["initial_ns", "draft_ns", "use_ns", "old_ns", "next_ns", "release_ns"]
for trial in trials:
    seen = set()
    for row in csv.DictReader(trial.open()):
        key = tuple(row[f] for f in ("method", "profile", "readers"))
        if key in seen or key not in expected:
            raise SystemExit(f"Unexpected/duplicate cell {key} in {trial}")
        seen.add(key)
        n, count, page = (int(row[f]) for f in ("iterations", "selected", "page_bytes"))
        if n != 64 or float(row["initial_ns"]) <= 0:
            raise SystemExit(f"Bad counts/timing for {key}")
        copies = 0 if key[0].startswith("file_") else (2 if key[2] == "held" else 1) if key[0] == "retained_take" else 3
        if int(row["copied_bytes"]) != copies * n * count * page:
            raise SystemExit(f"Wrong useful preparation count for {key}")
        if key[0] in ("retained_copy", "retained_take"):
            expected_maps = n if key[2] == "late_map" else n // 8 if key[2] == "late_1_in_8" else 0
            if int(row["maps"]) != expected_maps or int(row["protects"]) != 0:
                raise SystemExit(f"Unexpected VM operations for stable homes: {key}")
        row["episode_ns"] = sum(float(row[p]) for p in phases)
        cells.setdefault(key, []).append(row)
    if seen != expected:
        raise SystemExit(f"Missing {len(expected - seen)} pipeline cells in {trial}")
metrics = phases + ["episode_ns", "seed_ns", "maps", "unmaps", "protects", "copied_bytes",
                   "zeroed_bytes", "minor_faults", "rss_before_kib", "peak_rss_kib", "vmas_peak", "pool_bytes"]
with (directory / "comparisons.csv").open("w") as output:
    columns = ["method", "profile", "readers", "page_bytes", "pages", "selected", "iterations"]
    columns += [f"{m}_{s}" for m in metrics for s in ("median", "min", "max")]
    writer = csv.DictWriter(output, fieldnames=columns); writer.writeheader()
    for key, rows in sorted(cells.items()):
        result = dict(zip(("method", "profile", "readers"), key))
        result.update({f: rows[0][f] for f in ("page_bytes", "pages", "selected", "iterations")})
        for metric in metrics:
            values = [float(row[metric]) for row in rows]
            result.update({f"{metric}_median": median(values), f"{metric}_min": min(values), f"{metric}_max": max(values)})
        writer.writerow(result)
memory_trials = sorted(directory.glob("memory-[0-9]*.csv")); memory_cells = {}
if len(memory_trials) != 3:
    raise SystemExit("Expected three resource/mask trials")
for trial in memory_trials:
    seen = set()
    for row in csv.DictReader(trial.open()):
        key = tuple(row[f] for f in ("kind", "method", "profile", "capacity_bytes", "used_pages"))
        if key in seen:
            raise SystemExit(f"Duplicate memory cell {key}")
        seen.add(key); memory_cells.setdefault(key, []).append(row)
        if row["kind"] == "growth" and row["method"].startswith("reserved_") and row["stable_address"] != "1":
            raise SystemExit("Reserved growth changed address")
        if row["kind"] == "projection":
            if int(row["copied_bytes"]) != int(row["iterations"]) * 768:
                raise SystemExit("Projection useful-byte count mismatch")
    if len(seen) != 41:
        raise SystemExit(f"Expected 41 resource/mask cells in {trial}, got {len(seen)}")
if any(len(rows) != 3 for rows in memory_cells.values()):
    raise SystemExit("Resource/mask coverage differs between trials")
with (directory / "memory-comparisons.csv").open("w") as output:
    fields = ["kind", "method", "profile", "capacity_bytes", "used_pages"]
    numeric = ["total_ns", "seed_ns", "maps", "protects", "unmaps", "copied_bytes", "zeroed_bytes",
               "rss_delta_kib", "pss_delta_kib", "anonhuge_delta_kib", "minor_faults", "vma_delta", "stable_address",
               "release_ns", "reuse_ns", "release_rss_delta_kib", "advises"]
    writer = csv.DictWriter(output, fieldnames=fields + [f"{f}_median" for f in numeric]); writer.writeheader()
    for key, rows in sorted(memory_cells.items()):
        result = dict(zip(fields, key))
        result.update({f"{f}_median": median(float(row[f]) for row in rows) for f in numeric})
        writer.writerow(result)
(directory / "summary-checks.json").write_text(json.dumps({"result": "pass", "pipeline_cells": 180,
    "pipeline_trials": 6, "resource_cells": 41, "resource_trials": 3}, indent=2) + "\n")
