"""Challenge a scalar same-host latency fit using native retained measurements.

This is a deliberately restricted candidate model, not a new simulator or a
calibration of sim.py. Fit only low-load spinning single-item cases; test changed
batch/wait policies independently. Per-run quantiles are never summed or pooled.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
from statistics import median


HERE = Path(__file__).resolve().parent
EVIDENCE = HERE.parent / "orbital-local-handoff" / "evidence"


def read(path):
    with path.open() as stream:
        rows = list(csv.DictReader(stream))
    for row in rows:
        offered, completed, refused, unfinished = (int(row[k]) for k in ("offered", "completed", "refused", "unfinished"))
        if offered != completed + refused + unfinished or int(row["errors"]):
            raise ValueError("native observation violates accounting or payload/order checks")
    return rows


def case(rows, name):
    selected = [row for row in rows if row["case"] == name]
    if len(selected) != 3:
        raise ValueError(f"expected three repeats of {name}")
    def measured(field):
        values = [float(row[field]) for row in selected if row[field] != ""]
        return dict(median=median(values), minimum=min(values), maximum=max(values)) if values else None
    return dict(name=name, bytes=int(selected[0]["bytes"]), batch_cap=int(selected[0]["batch"]),
        policy=selected[0]["policy"], offered=sum(int(r["offered"]) for r in selected),
        completed=sum(int(r["completed"]) for r in selected),
        refused=sum(int(r["refused"]) for r in selected), unfinished=sum(int(r["unfinished"]) for r in selected),
        prepare_to_done_p50_ns=measured("prepare_to_done_p50_ns"),
        offer_to_done_p50_ns=measured("offer_to_done_p50_ns"),
        consumer_cpu_ns=measured("consumer_cpu_ns"), offer_window_ns=int(selected[0]["offer_window_ns"]),
        mean_publication_items=measured("mean_publication_items"))


def evaluate():
    paths = [EVIDENCE / cohort / "summary.csv" for cohort in ("20260927-main", "20260927-confirmation")]
    main, confirmation = map(read, paths)
    training = [case(main, name) for name in (
        "spin-steady-b1", "payload1024-steady-b1", "payload16384-steady-b1")]
    xs = [row["bytes"] for row in training]
    ys = [row["prepare_to_done_p50_ns"]["median"] for row in training]
    mx, my = sum(xs)/len(xs), sum(ys)/len(ys)
    slope = sum((x-mx)*(y-my) for x,y in zip(xs,ys)) / sum((x-mx)**2 for x in xs)
    intercept = my - slope*mx
    heldout = [case(main, name) for name in (
        "spin-sparse-b1", "spin-burst-b1", "spin-burst-b32", "wait-sparse-b1",
        "payload1024-burst-b32", "payload16384-burst-b32")]
    for row in training + heldout:
        prediction = intercept + slope * row["bytes"]
        measured = row["prepare_to_done_p50_ns"]["median"]
        row.update(scalar_prediction_ns=prediction, measured_to_prediction=measured/prediction,
                   absolute_relative_error=abs(prediction-measured)/measured)
    # A single-valued size-only curve cannot cover disjoint repeat ranges at the
    # same payload size. This rejection does not depend on a chosen error budget.
    same_size = [row for row in training + heldout if row["bytes"] == 64]
    lower = max(row["prepare_to_done_p50_ns"]["minimum"] for row in same_size)
    upper = min(row["prepare_to_done_p50_ns"]["maximum"] for row in same_size)
    totals = {k:sum(int(row[k]) for row in main + confirmation)
              for k in ("offered", "completed", "refused", "unfinished", "errors")}
    return dict(candidate="preparation-to-consumption median = intercept + slope * payload bytes",
        disposition="rejected as a general same-host transport model" if lower > upper else "not rejected by repeat-range intersection",
        coefficients=dict(intercept_ns=intercept, ns_per_byte=slope,
                          usage="diagnostic fit only; do not install as simulator constants"),
        training=training, heldout=heldout, same_size_repeat_range_intersection_ns=[lower,upper],
        independent_native_totals=totals,
        sources={str(path.relative_to(HERE.parent)): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths},
        analysis_source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        interpretation="Native conditional quantiles, repeat ranges and offered cohorts remain separate. This does not identify active service, polling occupancy, wakeup cost or instrumentation-free capacity.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = evaluate()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k:result[k] for k in ("disposition", "coefficients", "independent_native_totals")}))
