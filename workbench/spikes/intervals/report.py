#!/usr/bin/env python3
"""Summarize complete-operation timings, retaining every arm and losing case."""
import csv
import json
from pathlib import Path
import statistics
import sys
from collections import defaultdict

root = Path(sys.argv[1])
groups = defaultdict(list)
bytes_by_key = {}
for path in root.glob("*.jsonl"):
    for line in path.read_text().splitlines():
        r = json.loads(line)
        panel = "geometry" if "maximum" in r else "edits" if "effect_bytes" in r else "seams"
        case = r.get("case", r.get("operation", ""))
        if panel == "geometry": case += f"/M{r['maximum']}/N{r['count']}"
        key = panel, case, r["arm"]
        groups[key].append(r["elapsed_ns"] / r["operations"])
        if panel == "geometry":
            bytes_by_key[key] = {k: r[k] for k in ["data_bytes", "control_bytes", "capacity_bytes", "binding_bytes", "field_bytes", "checkpoint_bytes"]}
        elif panel == "edits": bytes_by_key[key] = {"effect_bytes_per_pair": r["effect_bytes"] / r["operations"]}
with (root / "summary.csv").open("w") as out:
    writer = csv.writer(out)
    writer.writerow(["panel", "case", "arm", "samples", "min_ns", "median_ns", "max_ns"])
    for key, samples in sorted(groups.items()):
        writer.writerow([*key, len(samples), min(samples), statistics.median(samples), max(samples)])
(root / "bytes.json").write_text(json.dumps([dict(panel=k[0], case=k[1], arm=k[2], **v) for k,v in sorted(bytes_by_key.items())], indent=2) + "\n")
# The reused Directory driver emits medians/minima of seven inner repetitions,
# not raw samples. Keep these rows unpooled and never treat them as raw trials.
with (root / "directory-summary.csv").open("w") as out:
    writer = csv.writer(out)
    writer.writerow(["process_file", "variant", "layer", "j", "count", "placement", "metric", "median_ns", "min_ns", "reps"])
    for path in sorted(root.glob("directory-p*.csv")):
        for row in csv.reader(path.read_text().splitlines()): writer.writerow([path.name, *row])
print(f"{sum(map(len, groups.values()))} raw complete-operation samples; {len(groups)} cells (all arms retained)")
