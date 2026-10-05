#!/usr/bin/env python3
"""Validate operation oracles, work counts and coverage before summarizing timings."""
import argparse
import csv
import json
import statistics
from collections import defaultdict
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--repetitions", type=int, default=3)
    args = parser.parse_args()
    groups = defaultdict(list)
    cases = {(s, h, c) for s in ("private", "shared", "reuse")
             for h in ("resident", "file_socket", "late_old", "retained_old") for c in (0, 8)}
    cases.add(("direct", "native_filter", 0))
    counters = ["maps", "unmaps", "copy_bytes", "file_read_bytes", "historical_fetch_bytes",
                "physical_records", "reused_pages", "replacement_pages", "discarded_bytes",
                "zeroed_bytes"]
    for order in ("forward", "reverse"):
        rows = list(csv.DictReader((args.directory / f"composition-{order}.csv").open()))
        seen = set()
        for row in rows:
            key = (row["strategy"], row["history"], int(row["cache_pages"]))
            repeat = int(row["repeat"])
            assert key in cases and 0 <= repeat < args.repetitions, ("case", key, repeat)
            assert (key, repeat) not in seen, ("duplicate", key, repeat)
            seen.add((key, repeat))
            n, page = int(row["operations"]), int(row["page_bytes"])
            assert n > 0 and page >= 4096
            assert int(row["image_bytes"]) == 4 * page and int(row["fixture_file_bytes"]) == 8 * page
            assert int(row["checksum"]) == int(row["expected_checksum"]) == n * 1396
            assert abs(float(row["ns_per_operation"]) - int(row["elapsed_ns"]) / n) < 0.001
            s, h, cache = key
            assert int(row["prepared_pages"]) == (1 if h == "file_socket" else 4)
            assert int(row["socket_bytes"]) == (page if h == "file_socket" else 0)
            assert int(row["write_bytes"]) == (0 if s == "direct" else 72)
            assert int(row["end_active_home_bytes"]) == (4 * page if s == "direct" else 0)
            assert int(row["end_cached_bytes"]) == cache * page
            if s == "direct":
                assert all(int(row[c]) == 0 for c in counters)
                assert int(row["peak_charged_bytes"]) == int(row["peak_active_home_bytes"]) == 4 * page
                assert int(row["peak_active_records"]) == 4
            else:
                copies = {"resident": {"private": 4 * page, "shared": 2 * page, "reuse": 72},
                          "file_socket": {"private": 3 * page, "shared": page, "reuse": 8},
                          "retained_old": {"private": 4 * page, "shared": 2 * page, "reuse": page + 8},
                          "late_old": {"private": 4 * page, "shared": 2 * page, "reuse": 72}}
                fetched = 3 * page if h == "file_socket" else page if h == "late_old" else 0
                new_pages = {"private": 4, "shared": 2, "reuse": 0}[s]
                if h == "file_socket":
                    new_pages = {"private": 6, "shared": 4, "reuse": 3}[s]
                elif h == "late_old":
                    new_pages += 1
                elif h == "retained_old" and s == "reuse":
                    new_pages = 1
                active_peak = ({"private": 7, "shared": 5, "reuse": 4}[s]
                               if h == "file_socket" else {"private": 8, "shared": 6, "reuse": 4}[s])
                if h in ("late_old", "retained_old") and s == "reuse":
                    active_peak = 5
                assert int(row["copy_bytes"]) == copies[h][s] * n, ("copies", key)
                assert int(row["file_read_bytes"]) == fetched * n, ("file bytes", key)
                assert int(row["historical_fetch_bytes"]) == (page * n if h == "late_old" else 0)
                assert int(row["zeroed_bytes"]) == new_pages * page * n
                assert int(row["peak_active_home_bytes"]) == active_peak * page
                assert int(row["maps"]) == (0 if cache else new_pages * n)
                prepared = 1 if h == "file_socket" else 4
                assert int(row["unmaps"]) == (0 if cache else (new_pages + prepared) * n)
                assert int(row["discarded_bytes"]) == int(row["unmaps"]) * page
                records = {"resident": {"private": 11, "shared": 9, "reuse": 7},
                           "file_socket": {"private": 11, "shared": 9, "reuse": 8},
                           "late_old": {"private": 12, "shared": 10, "reuse": 8},
                           "retained_old": {"private": 11, "shared": 9, "reuse": 8}}
                assert int(row["physical_records"]) == records[h][s] * n
                assert int(row["reused_pages"]) == ((1 if h in ("file_socket", "retained_old") else 2) * n
                                                      if s == "reuse" else 0)
                assert int(row["replacement_pages"]) == ((1 if h in ("file_socket", "retained_old") else 0) * n
                                                           if s == "reuse" else 2 * n)
            row["order"] = order
            groups[key].append(row)
        assert len(seen) == len(cases) * args.repetitions, ("coverage", order, len(seen))
    fields = ["strategy", "history", "cache_pages", "samples", "median_ns", "min_ns", "max_ns",
              "forward_median_ns", "reverse_median_ns", "peak_charged_bytes", "peak_active_home_bytes",
              "end_cached_bytes"] + [f"{c}_per_operation" for c in counters]
    with (args.directory / "composition-summary.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fields)
        writer.writeheader()
        for key, rows in sorted(groups.items()):
            times = [float(r["ns_per_operation"]) for r in rows]
            out = dict(zip(fields[:3], key))
            out.update(samples=len(rows), median_ns=statistics.median(times),
                       min_ns=min(times), max_ns=max(times))
            for order in ("forward", "reverse"):
                out[f"{order}_median_ns"] = statistics.median(
                    float(r["ns_per_operation"]) for r in rows if r["order"] == order)
            for c in ("peak_charged_bytes", "peak_active_home_bytes", "end_cached_bytes"):
                values = {int(r[c]) for r in rows}
                assert len(values) == 1, (key, c, values)
                out[c] = values.pop()
            for c in counters:
                values = [int(r[c]) / int(r["operations"]) for r in rows]
                assert max(values) - min(values) < 1e-9, (key, c)
                out[f"{c}_per_operation"] = values[0]
            writer.writerow(out)
    print(json.dumps({"cases": len(groups), "samples": sum(map(len, groups.values())),
                      "result": "pass", "fault_scope": "process trial including untimed setup"}))


if __name__ == "__main__":
    main()
