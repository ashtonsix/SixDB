"""Probe composition and authoring boundaries, not production performance.

Run from an immutable source capture. Missing/mismatching required checkers are
explicit incomplete-progress controls: safe nonpublication is not agreed abort.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict
import hashlib
import json
from pathlib import Path

from campaigns import Observation, Runner
from demanding_case import Case, run_case
from kernel import digest


HERE = Path(__file__).resolve().parent
SOURCES = ("demanding_campaign.py", "demanding_case.py", "scenario.py", "local_release.py",
           "replicated_contention.py", "traffic.py", "sim.py", "kernel.py", "campaigns.py")


def provenance():
    return dict(kind="checked-old-cut-composition", sources={
        name: hashlib.sha256((HERE / name).read_bytes()).hexdigest() for name in SOURCES},
        limits="Authored costs and finite histories. Static authority, local coordinator decision, "
               "fixture transcript digests, retained history without reclamation. "
               "No native runtime, authority handoff, agreed abort or steady-state throughput claim.")


def cases():
    rows = []
    for replicated in (False, True):
        base = dict(replicated=replicated, point_count=12, until_ns=6_000_000)
        variants = [("delayed-first-read", {}),
                    ("checker-process-restart", dict(restart_checker=True)),
                    ("source-device-reset", dict(source_reset=True)),
                    ("checker-and-source-recovery", dict(restart_checker=True, source_reset=True)),
                    ("missing-required-checker", dict(missing_checker=True)),
                    ("mismatching-required-checker", dict(mismatch_checker=True))]
        if replicated:
            variants += [(incident, dict(incident=incident)) for incident in
                         ("coordinator-verification-reset", "coordinator-outcome-reset")]
        variants += [(f"memory-{budget}-retain-{retain}",
                      dict(checker_memory_bytes=budget, retain_inputs=retain))
                     for budget in (8192, 12288) for retain in (False, True)]
        for name, variant in variants:
            rows.append(dict(name=("quorum/" if replicated else "standalone/") + name,
                             case=asdict(Case(**(base | variant)))))
    return rows


def evaluate(request, seed, output=None):
    row, world = run_case(Case(**request["case"]), seed=seed)
    groups = row["cohorts"]
    metrics = dict(missing_milestones=len(row["coverage"]["missing"]),
                   checked_complete=groups["checked"]["completed"],
                   checker_memory_peak=max(v["memory_peak"] for k, v in row["hosts"].items()
                                           if k.startswith("check_host")))
    for name, group in groups.items():
        metrics[name + "_max_latency_ns"] = max(group["latency_ns"].values(), default=None)
        metrics[name + "_untriggered"] = group.get("declared_but_untriggered", 0)
    selected = [e for e in world.trace if e["kind"] in (
        "scenario_trigger", "checked_context_ready", "checker_private_read", "checked_mismatch",
        "checked_verification_durable", "checked_recovery_rejected", "process_crash", "power_loss")]
    if output is not None and (row["violations"] or row["coverage"]["missing"] or
                              (seed == 7 and any(g["unfinished"] for g in groups.values()))):
        output.mkdir(parents=True, exist_ok=False)
        (output / "trace.json").write_text(json.dumps(world.trace, separators=(",", ":")) + "\n")
        (output / "choices.json").write_text(json.dumps(world.decisions, separators=(",", ":")) + "\n")
    return Observation(groups, metrics, row["violations"], dict(row=row, milestones=selected))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--seeds", type=int, nargs="+", default=[1, 7, 19])
    args = parser.parse_args()
    source = provenance()
    runner = Runner(evaluate, output=args.output, provenance=source)
    records = runner.matched(cases(), args.seeds)
    if provenance() != source:
        raise RuntimeError("source changed during campaign; preserve results but reject summary")
    summary = dict(provenance=source, source_identity=digest(source["sources"]),
                   records=[dict(id=r["id"], name=r["case"]["name"], seed=r["seed"], status=r["status"],
                       exception=r["exception"], observation=r["observation"]) for r in records])
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    print(json.dumps(dict(histories=len(records), errors=sum(r["status"] != "ok" for r in records),
        violations=sum(len(r["observation"]["violations"]) for r in records if r["status"] == "ok"),
        output=str(args.output))))


if __name__ == "__main__":
    main()
