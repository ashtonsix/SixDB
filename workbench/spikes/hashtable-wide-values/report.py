#!/usr/bin/env python3
import csv
import json
from pathlib import Path
import statistics
import sys


def main():
    root = Path(sys.argv[1])
    rows = []
    for path in sorted((root / "samples").glob("*.csv")):
        rows.extend(csv.DictReader(path.open()))
    groups = {}
    fields = ["impl", "w", "b", "v", "population", "temperature", "hint", "prefetch", "pipeline"]
    if any("loom" in row for row in rows): fields += ["loom"]
    if any("staged" in row for row in rows): fields += ["staged"]
    fields += ["operation", "pattern", "consumed"]
    for row in rows:
        key = tuple(row[f] for f in fields)
        groups.setdefault(key, []).append(row)
    with (root / "summary.csv").open("w") as output:
        writer = csv.DictWriter(output, fieldnames=fields + ["samples", "seeds", "ns", "min_ns", "max_ns", "bytes", "peak_bytes", "source_bytes"])
        writer.writeheader()
        for key, values in sorted(groups.items()):
            ns = [float(row["ns"]) for row in values]
            result = dict(zip(fields, key))
            result.update(samples=len(values), seeds=len({row["seed"] for row in values}), ns=statistics.median(ns), min_ns=min(ns), max_ns=max(ns))
            result.update({f: int(statistics.median(int(row[f]) for row in values)) for f in ["bytes", "peak_bytes", "source_bytes"]})
            writer.writerow(result)
    (root / "coverage.json").write_text(json.dumps({"raw_rows": len(rows), "cases": len(groups), "implementations": sorted({row["impl"] for row in rows}), "values": sorted({int(row["v"]) for row in rows})}, indent=2) + "\n")


if __name__ == "__main__":
    main()
