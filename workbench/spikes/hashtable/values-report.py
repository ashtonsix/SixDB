#!/usr/bin/env python3
"""Validate full width panel and retain per-case ratios without choosing winners."""
import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path
from statistics import median

p = argparse.ArgumentParser()
p.add_argument("csv", type=Path)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
rows = list(csv.DictReader(a.csv.open()))
groups = defaultdict(list)
seen = set()
phases = {"construct", "build", "hit", "miss", "serial", "update", "erase", "release", "lifecycle"}
for row in rows:
    identity = tuple(int(row[k]) for k in ("value_bytes", "kind", "A", "fanout", "arm", "rep")) + (row["phase"],)
    assert identity not in seen
    seen.add(identity)
    w, kind, level, fanout, arm, rep, phase = identity
    assert phase in phases and int(row["K"]) == 40 and arm in range(5)
    n = int(row["n"])
    assert n == 6 << level
    operations = int(row["operations"])
    assert operations == (1 if phase in ("construct", "release") else n // 2 if phase == "erase"
                          else n * max(8, 65536 // n) if phase in ("hit", "miss", "serial") else n)
    if arm < 3:
        assert sum(int(row[k]) for k in ("build_primary_rows", "build_secondary_rows", "build_overflow_rows")) == n
        assert int(row["inline_values"]) == ((8 if arm == 0 else 16) // w if kind == 2 else 0)
    groups[(w, kind, level, fanout, arm, phase)].append(row)

fans = {2: [1, 2, 4, 5, 8, 9, 16, 32], 8: [1, 2, 3, 4, 8, 32], 16: [1, 2, 3, 8, 32]}
expected = {(w, k, level, f, arm, phase) for w in fans for k in (1, 2) for level in (6, 10)
            for f in ([1] if k == 1 else fans[w]) for arm in range(5) for phase in phases}
assert set(groups) == expected, (len(groups), len(expected))
reps = {int(r["rep"]) for r in rows}
assert reps == set(range(len(reps)))
inventory = ("requested", "owned_capacity", "metadata", "inline_values", "abseil_inline_capacity",
             "build_primary_rows", "build_secondary_rows", "build_overflow_rows")
summary = {}
for case, batch in groups.items():
    assert len(batch) == len(reps)
    assert len({tuple(row[k] for k in inventory) for row in batch}) == 1
    times = [float(row["ns"]) / int(row["operations"]) for row in batch]
    summary[case] = {"ns_per_operation": median(times), "min": min(times), "max": max(times),
                     **{k: int(batch[0][k]) for k in inventory}}
comparison = []
for (w, kind, level, fanout, arm, phase), entry in sorted(summary.items()):
    base = summary[(w, kind, level, fanout, 3, phase)]
    original = summary[(w, kind, level, fanout, 0, phase)]
    logical = entry["requested"] + entry["metadata"]
    comparison.append({"value_bytes": w, "kind": kind, "A": level, "fanout": fanout, "arm": arm, "phase": phase,
                       **entry, "logical_bytes": logical,
                       "time_over_abseil": entry["ns_per_operation"] / base["ns_per_operation"],
                       "time_over_budget8": entry["ns_per_operation"] / original["ns_per_operation"],
                       "logical_over_abseil": logical / (base["requested"] + base["metadata"])})
out = {"rows": len(rows), "repetitions": len(reps), "cases": len(groups) // len(phases), "comparison": comparison}
a.output.write_text(json.dumps(out, indent=2))
with a.output.with_suffix(".csv").open("w") as f:
    writer = csv.DictWriter(f, fieldnames=comparison[0].keys())
    writer.writeheader()
    writer.writerows(comparison)
print(f"Validated {len(rows)} rows, {out['cases']} cases, {len(reps)} repetitions")
print("K40/A10 multimap: fixed candidate policies; median complete-operation ns divided by ordinary Abseil")
print("W fanout arm hit serial append lifetime initial-logical")
for w in fans:
    for fanout in fans[w]:
        for arm in range(3):
            cell = {r["phase"]: r for r in comparison if (r["value_bytes"], r["kind"], r["A"], r["fanout"], r["arm"]) == (w, 2, 10, fanout, arm)}
            ratios = [cell[phase]["time_over_abseil"] for phase in ("hit", "serial", "update", "lifecycle")]
            print(w, fanout, arm, *(f"{v:.3f}" for v in ratios), f"{cell['hit']['logical_over_abseil']:.3f}")
