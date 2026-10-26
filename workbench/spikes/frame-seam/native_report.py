#!/usr/bin/env python3
"""Audit complete native cohorts and emit the per-shape scorecard."""
import argparse
import collections
import csv
import json
import math
from pathlib import Path
import statistics


def read_rows(path):
    return [json.loads(line) for line in path.read_text().splitlines()]


def geometric_mean(values):
    return math.exp(statistics.fmean(math.log(value) for value in values))


def audit_profile(root, isa):
    rows = read_rows(root / isa / "span.jsonl")
    keys = {(r["arm"], r["round"], r["column"], r["op"]) for r in rows}
    assert {r["round"] for r in rows} == set(range(5))
    arms = ("base_current", "candidate_current", "candidate_span")
    if any(r["arm"] == "qualified_span" for r in rows):
        arms += ("qualified_span",)
    assert len(rows) == len(keys) == 216 * len(arms) * 5
    assert {r["arm"] for r in rows} == set(arms)
    cells = collections.defaultdict(list)
    images = collections.defaultdict(set)
    for row in rows:
        assert row["ns"] > 0
        cells[row["column"], row["op"], row["arm"]].append(row["ns"])
        images[row["column"]].add((row["image_bytes"], row["image_hash"]))
    assert len(images) == 24 and all(len(v) == 1 for v in images.values())
    shapes, details = [], []
    for op in sorted({r["op"] for r in rows}):
        ratios = []
        for column in sorted(images):
            assert all(len(cells[column, op, arm]) == 5 for arm in arms)
            medians = {arm: statistics.median(cells[column, op, arm]) for arm in arms}
            ratio = medians["candidate_span"] / medians["base_current"]
            ratios.append((ratio, column))
            detail = dict(isa=isa, column=column, op=op, **medians, ratio=ratio)
            if "qualified_span" in medians:
                detail["ratio_qualified"] = medians["candidate_span"] / medians["qualified_span"]
            details.append(detail)
        worst, worst_case = max(ratios)
        shapes.append(dict(
            isa=isa, op=op,
            geomean_ratio=geometric_mean(r[0] for r in ratios),
            median_ratio=statistics.median(r[0] for r in ratios),
            worst_ratio=worst, worst_case=worst_case,
        ))

    rows = read_rows(root / isa / "regression.jsonl")
    keys = {(r["column"], r["op"], r["conditioning"], r["arm"], r["round"])
            for r in rows}
    assert {r["round"] for r in rows} == set(range(5))
    regression_arms = {"base", "candidate"}
    if any(r["arm"] == "qualified" for r in rows):
        regression_arms.add("qualified")
    assert {r["arm"] for r in rows} == regression_arms
    assert len(rows) == len(keys) == 76 * len(regression_arms) * 5
    cells = collections.defaultdict(list)
    for row in rows:
        assert row["ns"] > 0
        cells[row["column"], row["op"], row["conditioning"], row["arm"]].append(row["ns"])
    regressions = []
    for column, op, conditioning, arm in sorted(cells):
        if arm != "candidate":
            continue
        before = cells[column, op, conditioning, "base"]
        after = cells[column, op, conditioning, "candidate"]
        assert len(before) == len(after) == 5
        base, candidate = statistics.median(before), statistics.median(after)
        detail = dict(
            isa=isa, column=column, op=op, conditioning=conditioning,
            base_ns=base, candidate_ns=candidate, ratio=candidate / base,
        )
        if "qualified" in regression_arms:
            qualified = cells[column, op, conditioning, "qualified"]
            assert len(qualified) == 5
            detail["qualified_ns"] = statistics.median(qualified)
            detail["ratio_qualified"] = candidate / detail["qualified_ns"]
        regressions.append(detail)
    return shapes, details, regressions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("evidence", type=Path)
    parser.add_argument("out", type=Path)
    args = parser.parse_args()
    shapes, details, regressions = [], [], []
    isas = [isa for isa in ("avx512", "avx2", "neon") if (args.evidence / isa / "span.jsonl").is_file()]
    if not isas:
        parser.error("no complete span cohort found")
    for isa in isas:
        s, d, r = audit_profile(args.evidence, isa)
        shapes.extend(s)
        details.extend(d)
        regressions.extend(r)
    args.out.mkdir(parents=True, exist_ok=True)
    for name, rows in (("span-shapes", shapes), ("span-cases", details),
                       ("regression-cases", regressions)):
        with (args.out / (name + ".csv")).open("w", newline="") as output:
            writer = csv.DictWriter(output, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
    print("| SUM shape | " + " | ".join(isa + " time/base" for isa in isas) + " |")
    print("| --- | " + " | ".join("---:" for _ in isas) + " |")
    for op in sorted({r["op"] for r in shapes}):
        by_isa = {r["isa"]: r["geomean_ratio"] for r in shapes if r["op"] == op}
        print("| " + op + " | " + " | ".join(f"{by_isa[isa]:.2f}" for isa in isas) + " |")
    print("\nRegressions >5% vs original or qualified, median ns/query:")
    for row in sorted(regressions, key=lambda r: r["ratio"], reverse=True):
        if row["ratio"] > 1.05 or row.get("ratio_qualified", 0) > 1.05:
            print(row)


if __name__ == "__main__":
    main()
