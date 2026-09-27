"""Retain every competitor and failure, with selected causal timing examples.

Raw per-transaction histories and exact choices remain in the recoverable bundle.
This selection is for comparing cases, not replacing the original observation.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def regional(result):
    out = {k: v for k, v in result.items()
           if k not in {"plans", "milestones", "completed_at", "latest_values"}}
    plans = {p["tx"]: p for p in result["plans"]}
    phases = {p["tx"]: p for p in result["milestones"]}
    completions = {int(k): v for k, v in result["completed_at"].items()}
    selected = {1, 2, 3, 100, 101, 102, 103}
    for cohort in result["cohorts"]:
        completed = [tx for tx in completions if plans[tx]["cohort"] == cohort]
        if completed:
            selected.add(max(completed, key=lambda tx: completions[tx] - plans[tx]["at"]))
    out["phase_samples"] = [dict(plan=plans[tx], phases=phases.get(tx),
                                  completed_at=completions.get(tx))
                            for tx in sorted(selected) if tx in plans]
    out["phase_maxima_ns"] = {}
    for tx, phase in phases.items():
        cohort = out["phase_maxima_ns"].setdefault(plans[tx]["cohort"], {})
        pairs = [("offer_to_durable_input", plans[tx]["at"], phase["input_durable_ns"])]
        for participant in phase["participants"]:
            pairs.extend((label, participant[a], participant[b]) for label, a, b in (
                ("reservation_wait", "acquire_input_ns", "granted_ns"),
                ("reservation_held", "granted_ns", "fixed_ns"),
                ("fixed_to_resolved", "fixed_ns", "resolved_ns")))
        for label, begin, end in pairs:
            if begin is not None and end is not None:
                if end < begin:
                    raise ValueError(f"negative {label} for transaction {tx}")
                cohort[label] = max(cohort.get(label, 0), end - begin)
    return out


def select(source, output):
    raw = source.read_bytes()
    summary = json.loads(raw)
    selected = dict(summary)
    selected["selection"] = {
        "input_sha256": hashlib.sha256(raw).hexdigest(),
        "selector_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "rule": "Keep every trial, parameter, cohort, failure and execution status. "
                "For ordering cases, keep per-cohort phase maxima, initial holders/broad waiter/WAN/first local "
                "wave and slowest completed member of each cohort; archive all other per-transaction rows.",
    }
    selected["trials"] = []
    for original in summary["trials"]:
        row = dict(original)
        result = original["result"]
        if result.get("experiment") in {"regional-localisation-v1", "alternating-holder-progress-v1"}:
            row["result"] = regional(result)
        selected["trials"].append(row)
    output.write_text(json.dumps(selected, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    select(args.source, args.output)
