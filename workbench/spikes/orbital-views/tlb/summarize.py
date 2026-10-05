#!/usr/bin/env python3
"""Check coverage and summarize trial means; IRQ counters are observations."""
import csv
import json
from pathlib import Path
from statistics import median
import sys

directory = Path(sys.argv[1])
fields = ["context", "operation", "pages", "populated"]
contexts = ["local", "remote_unseen", "remote_seen", "sleep_after_seen",
            "switched_after_seen", "independent_seen", "three_remote_unseen"]
operations = ["protect_noop", "protect_batch", "protect_pages", "replace", "unmap"]
expected = {(c, o, str(p), str(t)) for c in contexts for o in operations
            for p in (1, 64) for t in (0, 1)}
trials = sorted(directory.glob("forward-*.csv")) + sorted(directory.glob("reverse-*.csv"))
if len(trials) != 6:
    raise SystemExit(f"Expected six trial files, got {len(trials)}")
cells = {}
for trial in trials:
    seen = set()
    for row in csv.DictReader(trial.open()):
        key = tuple(row[f] for f in fields)
        if key in seen or key not in expected:
            raise SystemExit(f"Unexpected or duplicate cell in {trial}: {key}")
        seen.add(key)
        if int(row["iterations"]) != 100 or int(row["page_bytes"]) < 4096:
            raise SystemExit(f"Unexpected geometry or count in {trial}: {key}")
        if float(row["operation_ns"]) <= 0 or int(row["minor_faults"]) < 0:
            raise SystemExit(f"Invalid measurement in {trial}: {key}")
        cells.setdefault(key, []).append(row)
    if seen != expected:
        raise SystemExit(f"Missing {len(expected - seen)} cells in {trial}")
metrics = ["operation_ns", "touch_ns", "minor_faults", "tlb_irq_total", "tlb_irq_remote"]
with (directory / "comparisons.csv").open("w") as output:
    columns = fields + [f"{metric}_{stat}" for metric in metrics for stat in ("median", "min", "max")]
    writer = csv.DictWriter(output, fieldnames=columns)
    writer.writeheader()
    for key, samples in sorted(cells.items()):
        row = dict(zip(fields, key))
        for metric in metrics:
            values = [float(s[metric]) for s in samples]
            row.update({f"{metric}_median": median(values), f"{metric}_min": min(values),
                        f"{metric}_max": max(values)})
        writer.writerow(row)
(directory / "checks.json").write_text(json.dumps({"result": "pass", "trials": 6,
                                                    "cells_per_trial": len(expected)}, indent=2) + "\n")
