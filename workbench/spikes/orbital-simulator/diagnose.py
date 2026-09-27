"""Find censored/quiet service cohorts in an experiment receipt.

A quiet cohort is a symptom to inspect, not a proof of deadlock: the authored
incident or a legitimate long dependency can explain it. Retry traffic is not
counted as useful completion. World.explain and retained traces locate causes.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict, is_dataclass
import json
from pathlib import Path


def symptoms(observation, quiet_ns):
    if quiet_ns <= 0:
        raise ValueError("choose a positive observation interval")
    if is_dataclass(observation):
        observation = asdict(observation)
    result = {}
    for name, group in observation["cohorts"].items():
        arrivals = group.get("arrivals_ns", {})
        ages = group.get("unfinished_age_ns", {})
        if group["unfinished"] and len(ages) != group["unfinished"]:
            result[name] = dict(status="insufficient_timing_evidence", unfinished=group["unfinished"])
            continue
        untils = {arrivals[k] + age for k, age in ages.items()}
        if len(untils) > 1:
            raise ValueError("unfinished ages disagree about observation cutoff")
        until = next(iter(untils), None)
        completions = group.get("completions_ns", {})
        last = max(completions.values(), default=None)
        quiet_for = (until - last if last is not None else until - min(arrivals.values())) if until is not None else 0
        status = "complete" if not group["unfinished"] and not group["refused"] else "refused" if not ages else "unfinished"
        if ages and quiet_for >= quiet_ns:
            status = "quiet_with_unfinished_work"
        result[name] = dict(status=status, offered=group["offered"], completed=group["completed"],
                            refused=group["refused"], unfinished=group["unfinished"],
                            since_last_completion_ns=quiet_for,
                            aged_obligations={k: age for k, age in ages.items() if age >= quiet_ns})
    return dict(violations=observation.get("violations", []), cohorts=result,
                interpretation="Finite observation; quiet or aged work is not a deadlock proof.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("receipt", type=Path)
    parser.add_argument("--quiet-ns", type=int, required=True)
    args = parser.parse_args()
    record = json.loads(args.receipt.read_text())
    if record.get("status") == "error":
        print(record["exception"])
    else:
        print(json.dumps(symptoms(record.get("observation", record), args.quiet_ns), indent=2))
