#!/usr/bin/env python3
"""Derive tables from raw counters and every benchmark repetition; no fitted model."""

from __future__ import annotations

import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from evidence import read_measurements


def analyse(directory: Path):
    receipt, data = read_measurements(directory)
    if receipt["status"] not in {"running", "complete"}:
        raise ValueError("run failed; inspect its receipt before analysing")
    samples = defaultdict(list)
    for row in data["benchmarks"]:
        if row.get("error_occurred"):
            raise ValueError(f"benchmark error: {row}")
        if row.get("run_type") != "iteration":
            continue
        key = tuple(row["name"].split("/")[:2])
        scale = {"ns": 1, "us": 1000, "ms": 1000000, "s": 1000000000}[row["time_unit"]]
        samples[key].append(row["real_time"] * scale)
    with (directory / "accounting.csv").open() as handle:
        accounting = {(row["case"], row["method"]): row for row in csv.DictReader(handle)}
    selected = receipt["config"].get("cases")
    expected = {key for key in accounting if not selected or key[0] in selected}
    if not expected or set(samples) != expected:
        raise ValueError(f"missing/unexpected benchmark cases: expected {sorted(expected)}, got {sorted(samples)}")
    if selected and set(selected) != {key[0] for key in expected}:
        raise ValueError("unknown selected case")
    result = []
    for (case, method), times in samples.items():
        if len(times) != receipt["config"]["repetitions"]:
            raise ValueError(f"incomplete repetitions for {case}/{method}")
        raw = accounting[(case, method)]
        if raw["correct"] != "1" or raw["pending_entries_at_end"] != "0":
            raise ValueError("invalid or incompletely drained accounting result")
        eager = accounting[(case, "eager")]
        if raw["checksum"] != eager["checksum"]:
            raise ValueError("checksums differ between methods")
        mutations = int(raw["updates"])
        median = statistics.median(times)
        eager_time = statistics.median(samples[(case, "eager")])
        item = dict(raw)
        item.update(
            cycle_ns_per_mutation=median / mutations,
            cycle_ratio_to_eager=median / eager_time,
            repetition_spread_pct=100 * (max(times) - min(times)) / median,
            base_updates_per_mutation=int(raw["base_updates"]) / mutations,
            base_updates_ratio_to_eager=int(raw["base_updates"]) / int(eager["base_updates"]),
            ancestor_updates_per_mutation=(int(raw["base_updates"]) - int(raw["leaf_base_updates"])) / mutations,
            ancestor_updates_ratio_to_eager=(int(raw["base_updates"]) - int(raw["leaf_base_updates"])) /
                (int(eager["base_updates"]) - int(eager["leaf_base_updates"])),
            foreground_entries_per_mutation=int(raw["foreground_entries"]) / mutations,
            run_entries_per_mutation=int(raw["run_entries_written"]) / mutations,
        )
        result.append(item)
    with (directory / "summary.csv").open("w") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(result[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(result)
    by_case = defaultdict(dict)
    for item in result:
        by_case[item["case"]][item["method"]] = item
    text = ["# Aggregate delta probe: measured cycle costs", "",
        "Generated from accounting.csv and all recorded benchmark repetitions. "
        "Times are median wall time for apply + queries + folds + final drain, "
        "divided by mutations; empty-base setup and trace/oracle generation are excluded.", "",
        "These are resident-memory algorithm measurements on the recorded host. "
        "Base updates are logical Value-cell adjustments, not storage writes. "
        "No durability, historical-reader, or cache-residence claim is made.", "",
        "| Case | Eager ns/mutation | Batched / eager | Ancestor runs / eager | Location runs / eager | Location ancestor updates / eager |",
        "| --- | ---: | ---: | ---: | ---: | ---: |"]
    for case, rows in by_case.items():
        text.append(f"| {case} | {rows['eager']['cycle_ns_per_mutation']:.1f} | "
            f"{rows['eager_batch']['cycle_ratio_to_eager']:.2f} | "
            f"{rows['ancestor_runs']['cycle_ratio_to_eager']:.2f} | "
            f"{rows['location_runs']['cycle_ratio_to_eager']:.2f} | "
            f"{rows['location_runs']['ancestor_updates_ratio_to_eager']:.3f} |")
    text += ["", "Ratios below 1 mean less cost than eager in that case. "
        "Inspect summary.csv for repetition spread, sorting work, pending payload, "
        "and query probes. Different query counts change the work per mutation.", ""]
    (directory / "summary.md").write_text("\n".join(text))
    print(f"Verified and summarised {len(result)} case/method pairs.")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: analyze.py RUN_DIRECTORY")
    analyse(Path(sys.argv[1]))
