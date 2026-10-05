#!/usr/bin/env python3
"""Summarize the four retained benchmark passes without hiding order effects."""

import argparse
import csv
import hashlib
from pathlib import Path
import statistics


def summarize(directory):
    groups = {}
    coverage = []
    for mode, prefix in (("idle", ""), ("writer", "writer-")):
        for order in ("forward", "reverse"):
            path = directory / f"{prefix}{order}.csv"
            rows = list(csv.DictReader(path.open()))
            seen = {}
            for row in rows:
                key = (int(row["allocations"]), row["layout"], row["path"],
                       row["workload"])
                seen.setdefault(key, []).append(int(row["repetition"]))
                if float(row["ns_per_op"]) <= 0 or int(row["iterations"]) <= 0:
                    raise ValueError(f"invalid sample in {path}")
                writer = int(row["metadata_writer_cpu"]) >= 0
                if writer != (mode == "writer"):
                    raise ValueError(f"wrong writer mode in {path}")
                if writer and int(row["metadata_updates"]) <= 0:
                    raise ValueError(f"inactive metadata writer in {path}")
                groups.setdefault((mode, *key), {}).setdefault(order, []).append(row)
            if len(seen) != 48 or any(sorted(v) != list(range(5)) for v in seen.values()):
                raise ValueError(f"expected 48 cases with five repetitions in {path}")
            coverage.append(set(seen))
    if any(keys != coverage[0] for keys in coverage):
        raise ValueError("benchmark passes have different case coverage")

    output = directory / "summary.csv"
    fields = ["mode", "allocations", "layout", "path", "workload",
              "samples", "ns_median", "ns_min", "ns_max",
              "forward_ns_median", "reverse_ns_median",
              "metadata_updates_per_second_median", "mapped_page_bytes",
              "reserved_va_bytes", "owner_record_bytes"]
    with output.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for key, orders in sorted(groups.items()):
            mode, count, layout, path, workload = key
            rows = orders["forward"] + orders["reverse"]
            values = [float(row["ns_per_op"]) for row in rows]
            result = dict(zip(fields[:5], key))
            result.update(samples=len(rows), ns_median=statistics.median(values),
                          ns_min=min(values), ns_max=max(values))
            for order in ("forward", "reverse"):
                result[f"{order}_ns_median"] = statistics.median(
                    float(row["ns_per_op"]) for row in orders[order])
            result["metadata_updates_per_second_median"] = statistics.median(
                float(row["metadata_updates_per_second"]) for row in rows)
            for field in ("mapped_page_bytes", "reserved_va_bytes", "owner_record_bytes"):
                values = {int(row[field]) for row in rows}
                if len(values) != 1:
                    raise ValueError(f"changing footprint in {key}")
                result[field] = values.pop()
            writer.writerow(result)
    for prefix in ("", "writer-"):
        for order in ("forward", "reverse"):
            path = directory / f"{prefix}{order}.csv"
            print(f"{path.name}: sha256={hashlib.sha256(path.read_bytes()).hexdigest()}")
    print(f"{output}: 96 cases, 960 checked timing samples")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    summarize(parser.parse_args().directory)
