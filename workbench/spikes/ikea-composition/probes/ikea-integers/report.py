#!/usr/bin/env python3
"""Validate benchmark CSVs and report within-run medians and selected ratios.

Each input file is a separate run: targets and source revisions are never pooled.
Ratios greater than one favour selected. Fixed-payload plain comparisons can
have different logical counts and traces; only equal-extent traces are matched.
"""
import argparse
import csv
from collections import defaultdict
from pathlib import Path
from statistics import median
import sys


CONTEXT = ("suite", "operation", "call", "capacity_mode", "requested_payload_bytes",
           "width", "operations", "values_per_operation", "passes", "cpu", "data_seed")
GROUP = CONTEXT + ("logical_values", "provider", "layout")
RESULT = ("checksum0", "checksum1", "final_state", "address_sum", "trace_hash")


def read_and_validate(path):
    with path.open(newline="") as source:
        rows = list(csv.DictReader(source))
    if not rows:
        raise ValueError(f"{path}: no measurements")
    traces = {}
    identities = set()
    for row in rows:
        row.setdefault("capacity_mode", "fixed_logical" if row["suite"] == "capacity" else "not_applicable")
        row.setdefault("requested_payload_bytes", "0")
        identity = tuple(row[k] for k in GROUP + ("repetition", "seed"))
        if identity in identities:
            raise ValueError(f"{path}: duplicate measurement {identity}")
        identities.add(identity)
        if float(row["elapsed_ns"]) <= 0:
            raise ValueError(f"{path}: nonpositive timing")
        if row["suite"] == "capacity":
            values, payload = int(row["logical_values"]), int(row["payload_bytes"])
            if values < 256 or values & (values - 1):
                raise ValueError(f"{path}: logical count is not a complete power-of-two extent")
            expected = values if row["provider"] == "plain" else values * int(row["width"]) // 8
            if payload != expected or int(row["allocation_bytes"]) != (payload + 63) // 64 * 64:
                raise ValueError(f"{path}: payload/allocation accounting differs from representation")
            budget = int(row["requested_payload_bytes"])
            if row["capacity_mode"] == "fixed_payload":
                if not 256 <= budget <= 1 << 30 or int(row["allocation_bytes"]) > budget:
                    raise ValueError(f"{path}: invalid or exceeded fixed-payload budget")
                if values > 1 << 33 or (values < 1 << 33 and 2 * payload <= budget):
                    raise ValueError(f"{path}: fixed-payload extent is not the largest admitted power of two")
            elif row["capacity_mode"] != "fixed_logical" or budget or values > 1 << 30:
                raise ValueError(f"{path}: invalid fixed-logical mode")
        # The seed includes extent: different fixed-payload logical counts do
        # not belong in one trace-equivalence check.
        key = tuple(row[k] for k in CONTEXT + ("logical_values", "repetition", "seed"))
        result = tuple(row[k] for k in RESULT)
        if key in traces and traces[key] != result:
            raise ValueError(f"{path}: matched-extent checksum/trace mismatch for {key}")
        traces[key] = result
    print(f"{path}: validated {len(rows)} measurements and {len(traces)} extent/seed groups", file=sys.stderr)
    return rows


def summaries(path, rows):
    groups = defaultdict(list)
    for row in rows:
        groups[tuple(row[k] for k in GROUP)].append(row)
    groups = [(dict(zip(GROUP, key)), values) for key, values in groups.items()]

    def baseline(case, repetitions, provider):
        matches = []
        for candidate, values in groups:
            if candidate["provider"] != provider or any(candidate[k] != case[k] for k in CONTEXT):
                continue
            if provider == "prior" and candidate["layout"] != case["layout"]:
                continue
            if (provider == "prior" or case["capacity_mode"] != "fixed_payload") and \
                    candidate["logical_values"] != case["logical_values"]:
                continue
            if {v["repetition"] for v in values} != repetitions:
                continue
            matches.append((candidate, values))
        if len(matches) > 1:
            raise ValueError(f"{path}: ambiguous {provider} baseline for {case}")
        return matches[0] if matches else None

    for case, values in groups:
        timings = [float(v["ns_per_operation"]) for v in values]
        centre = median(timings)
        result = {"source": str(path), **case, "repetitions": len(values),
                  "median_ns_per_operation": centre, "min_ns_per_operation": min(timings),
                  "max_ns_per_operation": max(timings),
                  "median_ns_per_value": median(float(v["ns_per_value"]) for v in values),
                  "prior_over_selected": "NA", "plain_over_selected": "NA", "plain_comparison": "NA",
                  "pmu_status": "/".join(sorted({v["pmu_status"] for v in values}))}
        if case["provider"] == "selected":
            repetitions = {v["repetition"] for v in values}
            for provider in ("prior", "plain"):
                matched = baseline(case, repetitions, provider)
                if matched:
                    other, samples = matched
                    result[f"{provider}_over_selected"] = median(float(v["ns_per_operation"]) for v in samples) / centre
                    if provider == "plain":
                        result["plain_comparison"] = "different_logical_counts" if other["logical_values"] != case["logical_values"] else "equal_logical_counts"
        yield result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", nargs="+", type=Path)
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args()
    writer = None
    for path in args.csv:
        if path.is_dir():
            if (path / "provenance.json").exists():
                sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools"))
                from evidence import verify_compact
                verify_compact(path)
            path = path / ("samples.csv" if (path / "samples.csv").exists() else "timings.csv")
        rows = read_and_validate(path)
        if args.validate_only:
            continue
        for result in summaries(path, rows):
            if writer is None:
                writer = csv.DictWriter(sys.stdout, fieldnames=list(result))
                writer.writeheader()
            writer.writerow(result)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError) as error:
        print(f"report failed: {error}", file=sys.stderr)
        raise SystemExit(1)
