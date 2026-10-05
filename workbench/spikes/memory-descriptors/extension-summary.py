#!/usr/bin/env python3
"""Retain all extension competitors and both orders, with footprint context."""
import argparse
import csv
import hashlib
import math
from pathlib import Path
import statistics

LAYOUTS = ("grid_major", "class_major", "class_major_reordered", "class_aligned")
LIBRARIES = ("libc", "mimalloc_3.5.3", "snmalloc_0.7.5")
POOLS = ("local_freelist", "local_bitmap", "shared_mutex_bitmap", "shared_atomic_bitmap", "region_bulk_reset")

def expected_grouped():
    cases = set()
    for granule in (4096, 16384):
        for width in (16, 64, 256, 1024):
            for layout in LAYOUTS:
                for count in (64, 4096):
                    for path in ("direct", "grouped_resolve"):
                        for workload in ("scan64", "random_point8", "dependent_chase16"):
                            cases.add(tuple(map(str, (granule, width, layout, count, 64, path, workload, "allocation_access"))))
                for size in (16, 64, 96):
                    cases.add(tuple(map(str, (granule, width, layout, 1, size, "prepared_grid", "allocate_touch8_release", "allocation_free_pair"))))
    return cases

def expected_allocation():
    cases = {(name, "single", str(size), str(live)) for name in LIBRARIES + POOLS
             for size in (16, 64, 96) for live in (1, 64, 1024)}
    cases |= {(name, "two_core_shared", str(size), "128")
              for name in LIBRARIES + ("shared_mutex_bitmap", "shared_atomic_bitmap")
              for size in (16, 64, 96)}
    return cases

def summarize(directory, prefix, keys, metric, rep, expected, counters):
    groups = {}
    for order in ("forward", "reverse"):
        path = directory / f"{prefix}-{order}.csv"
        with path.open() as handle:
            rows = list(csv.DictReader(handle))
        seen = {}
        for row in rows:
            key = tuple(row[field] for field in keys)
            seen.setdefault(key, []).append(int(row[rep]))
            if not math.isfinite(float(row[metric])) or float(row[metric]) <= 0:
                raise ValueError(f"invalid timing: {path}")
            operations = int(row["iterations" if prefix == "grouped" else "pairs"])
            if operations <= 0:
                raise ValueError(f"invalid operation count: {path}")
            if prefix == "grouped":
                if int(row["native_page_bytes"]) != 4096:
                    raise ValueError("summary recipe is for this 4KiB-host comparison")
                mode = "native" if row["mapping_granule_bytes"] == "4096" else "modeled_mapping_granule"
                if row["mapping_granule_mode"] != mode:
                    raise ValueError("wrong native/modeled label")
                if row["unit"] == "allocation_free_pair" and row["ns_per_pair"] != row[metric]:
                    raise ValueError("pair metric disagrees")
            groups.setdefault(key, {}).setdefault(order, []).append(row)
        if set(seen) != expected or any(sorted(reps) != [0, 1, 2] for reps in seen.values()):
            raise ValueError(f"missing/duplicate cases or repetitions: {path}")
        print(f"{path.name}: {len(rows)} samples sha256={hashlib.sha256(path.read_bytes()).hexdigest()}")
    output = directory / f"{prefix}-summary.csv"
    fields = keys + ["samples", "ns_median", "ns_min", "ns_max", "forward_ns_median", "reverse_ns_median"] + counters
    if prefix == "allocation":
        fields += ["initial_usable_live_bytes_min", "initial_usable_live_bytes_max"]
    with output.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for key, orders in sorted(groups.items()):
            rows = orders["forward"] + orders["reverse"]
            values = [float(row[metric]) for row in rows]
            result = dict(zip(keys, key))
            result.update(samples=len(rows), ns_median=statistics.median(values), ns_min=min(values), ns_max=max(values))
            for order in ("forward", "reverse"):
                result[f"{order}_ns_median"] = statistics.median(float(row[metric]) for row in orders[order])
            for field in counters:
                values = {row[field] for row in rows}
                if field == "initial_usable_live_bytes":
                    measured = [int(row[field]) for row in rows]
                    result[field] = statistics.median(measured)
                    result[field + "_min"] = min(measured)
                    result[field + "_max"] = max(measured)
                    continue
                if len(values) != 1:
                    raise ValueError(f"changing context {field} for {key}")
                result[field] = values.pop()
            writer.writerow(result)
    print(f"{output.name}: {len(groups)} cases, both orders, six samples/case")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    d = parser.parse_args().directory
    summarize(d, "grouped", ["mapping_granule_bytes", "trampolines_per_group", "layout", "allocations",
        "payload_bytes", "path", "workload", "unit"], "ns_per_op", "repetition", expected_grouped(),
        ["native_page_bytes", "mapping_granule_mode", "reserved_va_bytes", "mapped_page_bytes",
         "owner_record_bytes", "active_head_array_bytes", "total_head_array_bytes", "order_array_bytes",
         "cached_empty_granules", "map_calls", "source_context", "compiler", "architecture"])
    summarize(d, "allocation", ["allocator", "workload", "bytes", "live"], "ns_per_pair", "rep",
        expected_allocation(), ["requested_live_bytes", "initial_usable_live_bytes", "prepared_capacity_bytes", "policy_metadata_bytes"])

if __name__ == "__main__":
    main()
