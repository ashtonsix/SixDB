#!/usr/bin/env python3
"""Summarize matched physical-layout samples; timings are not confidence bounds."""
import csv
import argparse
import re
import statistics
import sys
from collections import defaultdict
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--prefix", choices=("layout", "bulk"), default="layout")
parser.add_argument("--output-name", default="summary.csv")
args = parser.parse_args()
source, output = args.source, args.output
output.mkdir(parents=True, exist_ok=True)
cells = defaultdict(list)
pattern = re.compile(args.prefix + r"-(\d+)-(direct|width)-(.+)-(\d+)-(\d+)\.csv")
for path in sorted(source.glob(f"*/{args.prefix}-*.csv")):
    match = pattern.fullmatch(path.name)
    assert match, path
    trial, geometry, placement, maximum, rows = match.groups()
    with path.open() as file:
        samples = list(csv.DictReader(file))
    assert len(samples) in (8, 12, 14, 16, 28, 40, 42), (path, len(samples))
    for sample in samples:
        assert (sample["placement"], sample["maximum"], sample["rows"]) == (placement, maximum, rows), path
        key = (path.parent.name, geometry, placement, int(maximum), int(rows), sample["operation"])
        sample["trial"] = int(trial)
        sample["ns_per_row"] = float(sample["ns_per_row"])
        cells[key].append(sample)

assert cells, source
header = ["variant", "geometry", "placement", "maximum", "rows", "operation", "samples",
          "median_ns", "min_ns", "max_ns", "vs_planes", "trial0_ratio", "trial1_ratio",
          "metadata_bytes_per_row", "owned_bytes_per_row", "capacity_bytes_per_row",
          "body_bytes_per_row", "control_bytes_per_row", "issued_bytes_per_row", "items_per_iteration", "median_ns_per_item"]
def median(samples):
    return statistics.median(s["ns_per_row"] for s in samples)
with (output / args.output_name).open("w") as file:
    writer = csv.writer(file)
    writer.writerow(header)
    for key, samples in sorted(cells.items()):
        assert len(samples) == 4, (key, len(samples))
        variant, geometry, placement, maximum, rows, operation = key
        reference = cells[(variant, geometry, "planes", maximum, rows, operation)]
        ratios = [median([s for s in samples if s["trial"] == t]) /
                  median([s for s in reference if s["trial"] == t]) for t in (0, 1)]
        sizes = []
        for field in ("metadata_bytes", "metadata_owned_bytes", "metadata_capacity_bytes", "body_bytes", "control_bytes"):
            values = {int(s[field]) for s in samples}
            assert len(values) == 1, (key, field, values)
            sizes.append(values.pop() / rows)
        issued = ""
        if operation == "metadata-update":
            values = {int(s["result"]) for s in samples}
            assert len(values) == 1, (key, values)
            issued = values.pop() / rows
        times = [s["ns_per_row"] for s in samples]
        values = [*key, len(samples), median(samples), min(times), max(times),
                  median(samples) / median(reference), *ratios, *sizes, issued,
                  int(samples[0].get("items_per_iteration", rows)),
                  statistics.median(float(s.get("ns_per_item", s["ns_per_row"])) for s in samples)]
        writer.writerow([f"{x:.6f}" if isinstance(x, float) else x for x in values])
print(f"{len(cells)} cells, {sum(map(len, cells.values()))} samples -> {output / args.output_name}")
