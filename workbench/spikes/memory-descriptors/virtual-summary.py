#!/usr/bin/env python3
"""Check independent growth histories and summarize both orders with costs."""
import argparse
import csv
import hashlib
import math
from pathlib import Path
import statistics

KEYS = ["placement", "capacity_policy", "history"]
CONTEXT = ["native_page_bytes", "test_window_bytes", "requested_bytes",
           "logical_readable_bytes", "prepared_capacity_bytes",
           "current_reserved_va_bytes", "current_rw_mapped_bytes",
           "retained_old_mapped_bytes", "peak_reserved_va_bytes", "peak_rw_mapped_bytes"]
COSTS = ["copied_bytes", "appended_initialized_bytes", "explicit_page_touches",
         "timed_mmap_calls", "timed_mprotect_calls", "timed_mremap_calls",
         "timed_munmap_calls", "payload_moves", "held_copy_fallbacks",
         "process_minor_faults", "process_major_faults"]

def require(condition, message):
    if not condition:
        raise ValueError(message)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    directory = parser.parse_args().directory
    expected = {(p, c, h) for p in ("stable_window", "copy_binding", "mremap_binding")
                for c in ("exact_pages", "geometric")
                for h in ("interleaved_16x1MiB", "within_page_64x16B_append")}
    grouped = {}
    for order in ("forward", "reverse"):
        path = directory / f"virtual-{order}.csv"
        rows = list(csv.DictReader(path.open(newline="")))
        seen = {}
        for row in rows:
            require(None not in row and all(v is not None for v in row.values()), "malformed row")
            key = tuple(row[k] for k in KEYS)
            require(key in expected, f"unexpected case: {key}")
            seen.setdefault(key, []).append(int(row["rep"]))
            cycles = int(row["cycles"])
            require(cycles > 0, "empty timing sample")
            interleaved = row["history"] == "interleaved_16x1MiB"
            per_cycle = 352 if interleaved else 4096
            growths = int(row["growth_calls"])
            require(growths == cycles * per_cycle, "incorrect growth count")
            checksum = 2139095040 if interleaved else 16711680
            require(int(row["checksum"]) == cycles * checksum, "incorrect independent checksum")
            initialized = 16 * 1024**2 if interleaved else 64 * 2048
            appended = 16 * 1024**2 if interleaved else 64 * 1024
            require(int(row["requested_bytes"]) == initialized and
                    int(row["logical_readable_bytes"]) == initialized, "wrong readable end state")
            require(int(row["appended_initialized_bytes"]) == cycles * appended,
                    "wrong initialized-byte work")
            require(int(row["native_page_bytes"]) == 4096, "comparison expects native 4KiB pages")
            prepared = 16 * 1024**2 if interleaved else 64 * 4096
            require(int(row["prepared_capacity_bytes"]) == prepared and
                    int(row["current_rw_mapped_bytes"]) == prepared and
                    int(row["retained_old_mapped_bytes"]) == 0, "wrong prepared end state")
            stable = row["placement"] == "stable_window"
            count = 16 if interleaved else 64
            va = count * 2 * 1024**2 if stable else prepared
            require(int(row["current_reserved_va_bytes"]) == va, "wrong VA charge")
            require(int(row["test_window_bytes"]) == (2 * 1024**2 if stable else 0), "wrong window label")
            for field in CONTEXT + COSTS:
                require(int(row[field]) >= 0, f"negative {field}")
            require(int(row["peak_reserved_va_bytes"]) >= va and
                    int(row["peak_rw_mapped_bytes"]) >= prepared, "peak below current state")
            for field in ("ready_ns_per_growth", "ready_ns_per_cycle"):
                require(math.isfinite(float(row[field])) and float(row[field]) > 0, "invalid timing")
            require(math.isclose(float(row["ready_ns_per_cycle"]),
                                 float(row["ready_ns_per_growth"]) * per_cycle, rel_tol=1e-8),
                    "inconsistent timing units")
            if not interleaved:
                require(all(int(row[field]) == 0 for field in COSTS[:1] + COSTS[2:9]),
                        "prepared append changed backing")
            if stable:
                require(int(row["copied_bytes"]) == 0 and int(row["payload_moves"]) == 0 and
                        int(row["timed_mremap_calls"]) == 0, "stable window moved/copied")
            require(int(row["held_copy_fallbacks"]) == 0, "timed histories have no grants")
            grouped.setdefault(key, {}).setdefault(order, []).append(row)
        require(set(seen) == expected and all(sorted(reps) == [0, 1, 2] for reps in seen.values()),
                f"missing/duplicate cases: {path}")
        print(f"{path.name}: {len(rows)} samples sha256={hashlib.sha256(path.read_bytes()).hexdigest()}")
    fields = KEYS + ["samples", "ns_median", "ns_min", "ns_max", "forward_ns_median", "reverse_ns_median"] + CONTEXT
    for cost in COSTS:
        fields += [cost + "_per_cycle_median", cost + "_per_cycle_min", cost + "_per_cycle_max"]
    with (directory / "virtual-summary.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for key, orders in sorted(grouped.items()):
            rows = orders["forward"] + orders["reverse"]
            times = [float(row["ready_ns_per_growth"]) for row in rows]
            result = dict(zip(KEYS, key), samples=len(rows), ns_median=statistics.median(times),
                          ns_min=min(times), ns_max=max(times))
            for order, samples in orders.items():
                result[order + "_ns_median"] = statistics.median(float(r["ready_ns_per_growth"]) for r in samples)
            for field in CONTEXT:
                values = {row[field] for row in rows}
                require(len(values) == 1, f"changing end state or peak: {key}/{field}")
                result[field] = values.pop()
            for cost in COSTS:
                values = [int(r[cost]) / int(r["cycles"]) for r in rows]
                result[cost + "_per_cycle_median"] = statistics.median(values)
                result[cost + "_per_cycle_min"] = min(values)
                result[cost + "_per_cycle_max"] = max(values)
            writer.writerow(result)
    print("virtual-summary.csv: 12 cases, both orders, six samples/case")

if __name__ == "__main__":
    main()
