#!/usr/bin/env python3
"""Compact medians (of 9 rotated trials) from worker cells.jsonl for the arms the findings cite.

  summarize.py OUT.csv RESULTS_DIR... [--large]
Full samples (min/median/max of 9 rotated trials, every arm) stay in each job's
worker archive; artifact.json beside the retained evidence recovers it.
"""
import csv, json, pathlib, sys

ARMS = ["before", "current", "proposed_halo", "std_branchy", "bisect", "lerp_hyb64_clamp"]
out, *dirs = sys.argv[1:]
large = "--large" in dirs
dirs = [d for d in dirs if d != "--large"]
rows = []
for d in dirs:
    for f in sorted(pathlib.Path(d).rglob("cells.jsonl")):
        for line in f.read_text().splitlines():
            c = json.loads(line)
            if c["arm"] in ARMS and (c["n"] > 16384) == large:
                rows.append([c["profile"], c["bytes"], c["counts"], c["stream"], c["n"], c["arm"],
                             c["median"]])
with open(out, "w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(["profile", "bytes", "counts", "stream", "n", "arm", "median_ns"])
    w.writerows(rows)
print(out, len(rows), "rows")
