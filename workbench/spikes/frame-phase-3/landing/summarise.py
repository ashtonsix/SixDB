#!/usr/bin/env python3
"""Summarise one profile; timings are ns per whole query, not ns per value."""
import argparse
import json
import statistics
from collections import defaultdict
from pathlib import Path


def summarise(path):
    groups = defaultdict(list)
    for line in path.read_text().splitlines():
        row = json.loads(line)
        key = (row["column"], row["form"], row["op"], row["conditioning"])
        groups[key].append(row)
    result = []
    for key, rows in sorted(groups.items()):
        arms = defaultdict(list)
        for row in rows:
            arms[row["arm"]].append(row["ns"])
        assert set(arms) == {"ikea", "spike", "native_typed_for"}, key
        assert len({len(samples) for samples in arms.values()}) == 1, key
        entry = dict(zip(("column", "form", "op", "conditioning"), key))
        entry.update({k: rows[0][k] for k in (
            "rows", "requests", "ikea_image_bytes", "spike_image_bytes", "native_value_bytes")})
        entry["timings"] = {
            arm: {"n": len(samples), "min_ns": min(samples),
                  "median_ns": statistics.median(samples), "max_ns": max(samples)}
            for arm, samples in arms.items()
        }
        ikea = entry["timings"]["ikea"]["median_ns"]
        entry["ikea_over_spike"] = ikea / entry["timings"]["spike"]["median_ns"]
        entry["native_over_ikea"] = entry["timings"]["native_typed_for"]["median_ns"] / ikea
        result.append(entry)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("measure", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    rows = summarise(args.measure)
    if args.json:
        print(json.dumps(rows, indent=2))
    else:
        print("| Column | Operation | Conditioning | Ikea ns/query | Ikea/spike | Native/Ikea |")
        print("| --- | --- | --- | ---: | ---: | ---: |")
        for row in rows:
            print(f'| {row["column"]} | {row["op"]} | {row["conditioning"]} | '
                  f'{row["timings"]["ikea"]["median_ns"]:.1f} | '
                  f'{row["ikea_over_spike"]:.2f} | {row["native_over_ikea"]:.2f} |')
