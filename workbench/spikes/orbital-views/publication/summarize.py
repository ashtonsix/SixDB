#!/usr/bin/env python3
"""Pair complete deterministic cells; retain medians and trial ranges, not claims."""
import csv
import json
import math
from pathlib import Path
import statistics
import sys

KEYS = {
    "material": ("part_bytes", "shape", "epochs", "material", "content_root", "coalesced"),
    "recovery": ("part_bytes", "shape", "epochs", "coalesced", "reconstruction"),
    "pressure": ("part_bytes", "shape", "epochs", "material", "retention"),
}


def summarize(root, kind):
    files = sorted(root.glob(f"forward-*/{kind}.csv")) + sorted(root.glob(f"reverse-*/{kind}.csv"))
    if not files:
        raise ValueError(f"no {kind} trials")
    trials = []
    for path in files:
        rows = list(csv.DictReader(path.open()))
        trial = {tuple(row[k] for k in KEYS[kind]): row for row in rows}
        if not rows or len(trial) != len(rows):
            raise ValueError(f"empty or duplicate cells: {path}")
        numeric = [k for k in rows[0] if k not in KEYS[kind]]
        if any(not math.isfinite(float(row[k])) or float(row[k]) < 0 for row in rows for k in numeric):
            raise ValueError(f"invalid measurements: {path}")
        trials.append(trial)
    if any(set(t) != set(trials[0]) for t in trials[1:]):
        raise ValueError(f"incomplete paired cells: {kind}")
    rows = []
    for key in sorted(trials[0]):
        row = dict(zip(KEYS[kind], key))
        row["trials"] = len(trials)
        for field in numeric:
            values = [float(t[key][field]) for t in trials]
            if "_ns" in field:
                row[field] = statistics.median(values)
                row[field + "_min"] = min(values)
                row[field + "_max"] = max(values)
            else:
                if len(set(values)) != 1:
                    raise ValueError(f"counter drift: {kind} {key} {field}")
                row[field] = int(values[0])
        rows.append(row)
    with (root / f"{kind}-summary.csv").open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    return {"kind": kind, "cells": len(rows), "trials": len(trials), "paired_complete": True}


if __name__ == "__main__":
    root = Path(sys.argv[1])
    print(json.dumps([summarize(root, kind) for kind in KEYS], indent=2))
