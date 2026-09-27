"""Matched strict/global-release and candidate/local-release histories.

Synthetic protocol simplification experiment on the corrected contention fold.
This does not rehabilitate the historical early minimum or partial-read gate.
"""
from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys
import traceback

from campaigns import cohort
from fidelity_campaign import identity, telemetry
from local_release import build
from replicated_contention import audit as replicated_audit
from traffic import Config, Strategy, audit, diagnostics


HERE = Path(__file__).resolve().parent
SOURCES = ("release_campaign.py", "local_release.py", "traffic.py", "replicated_contention.py",
           "fidelity_campaign.py", "campaigns.py", "kernel.py", "sim.py")


def hashes():
    return {name: hashlib.sha256((HERE / name).read_bytes()).hexdigest() for name in SOURCES}


def cases():
    wan = dict(topology="wan", count=100, interval_ns=2_000_000,
               slow_every=100, drain_ns=800_000_000)
    result = [dict(name=name, config=dict(wan, workload=shape),
                   strategy=dict(retry_ns=10_000_000, read_floor_first=False))
              for name, shape in (("wan-independent", "wan-independent"),
                                  ("wan-dependent", "wan-mix"),
                                  ("wan-broad-read-narrow-output", "global-scan"))]
    result.extend([
        dict(name="man-hot", config=dict(workload="hot", count=64, interval_ns=5_000)),
        dict(name="man-bridge", config=dict(workload="bridge", count=64,
             interval_ns=8_000, slow_every=8, point_rmw=True)),
        dict(name="replicated-lan-transfer", replicated=True, config=dict(workload="transfer",
             count=8, width=4, shards=2, topology="lan", drain_ns=4_000_000)),
        dict(name="replicated-lan-scan", replicated=True, config=dict(workload="scan",
             count=8, width=4, shards=2, topology="lan", slow_every=4, drain_ns=4_000_000)),
        dict(name="fix-power-recovery", fault="fix-before-callback", config=dict(workload="transfer",
             count=4, shards=2, width=4, topology="lan", drain_ns=4_000_000)),
        dict(name="replicated-fix-power-recovery", replicated=True, fault="fix-before-callback",
             config=dict(workload="transfer", count=4, shards=2, width=4,
                         topology="lan", drain_ns=4_000_000)),
        dict(name="loss-and-duplicates", config=dict(workload="transfer", count=12, shards=2,
             width=4, topology="man", loss=.04, duplicate=.3, drain_ns=30_000_000)),
    ])
    return result


def evaluate(request, output):
    source = hashes()
    case, seed, early = request["case"], request["seed"], request["early"]
    config, strategy = Config(**case["config"]), Strategy(**case.get("strategy", {}))
    replicated = case.get("replicated", False)
    result = dict(request, source_identity=identity(source), source_hashes=source,
                  config=asdict(config), strategy=asdict(strategy),
                  status="error", exception=None, python=platform.python_version())
    world, plans, until = build(config, strategy, seed, early=early, replicated=replicated)
    result["authored_plans_hash"] = identity(plans)
    if case.get("fault") == "fix-before-callback":
        def fixed(row):
            prefix = "log/" if replicated else "journal/"
            if row["actor"] != "shard0" or not row["key"].startswith(prefix):
                return False
            requests = row["value"]["requests"] if replicated else row["value"]
            return any(r["kind"] == "fix" for r in requests)
        world.when("durable_write", fixed, "power_loss", host="h0")
        world.fault(1_000_000, "power_on", host="h0")
    try:
        world.run(until=until, max_events=1_000_000)
        violations = replicated_audit(world) if replicated else audit(world, plans, config)
        responses, refused = {}, {}
        for e in world.trace:
            if e["kind"] == "traffic_response": responses.setdefault(str(e["op"]), e["time"])
            if e["kind"] == "traffic_refused": refused.setdefault(str(e["op"]), e["time"])
        end = max(p["at"] for p in plans) + config.interval_ns
        groups = {}
        for name in sorted({p["cohort"] for p in plans}):
            arrivals = {str(p["id"]): p["at"] for p in plans if p["cohort"] == name}
            c = cohort(arrivals, {k:v for k,v in responses.items() if k in arrivals},
                       {k:v for k,v in refused.items() if k in arrivals}, offered_until=end, until=until)
            groups[name] = {k:c[k] for k in ("offered", "completed", "refused", "unfinished", "completed_in_window")}
            groups[name].update(max_latency_ns=max(c["latency_ns"].values(), default=None),
                               oldest_unfinished_ns=max(c["unfinished_age_ns"].values(), default=None))
            for origin in (0, 1):
                keys = {str(p["id"]) for p in plans if p["cohort"] == name and p["origin"] == origin}
                groups[name][f"origin{origin}"] = dict(offered=len(keys),
                    completed=sum(k in keys for k in c["completions_ns"]),
                    refused=sum(k in keys for k in c["refusals_ns"]),
                    unfinished=sum(k in keys for k in c["unfinished_age_ns"]),
                    max_latency_ns=max((v for k,v in c["latency_ns"].items() if k in keys), default=None))
        counts = Counter(e["kind"] for e in world.trace)
        if case.get("fault") and counts["power_loss"] != 1:
            violations.append("declared fix-boundary power loss did not fire exactly once")
        observed = telemetry(world)
        observed["transitions"] = dict(Counter(e["request"]["kind"] for e in world.trace
            if e["kind"] == ("replicated_transition" if replicated else "traffic_transition")
            and (not replicated or e["replica"] == 0)))
        result.update(status="ok", violations=violations, cohorts=groups, telemetry=observed,
            metrics=dict(offered=len(plans), completed=len(responses), refused=len(refused),
                unfinished=len(plans)-len(responses)-len(refused),
                backlog_at_offer_end=len(plans)-sum(t<end for t in responses.values())-sum(t<end for t in refused.values()),
                drain_ns=max(0, max(responses.values(), default=0)-end) if len(responses)+len(refused)==len(plans) else None,
                wire_bytes=sum(e["size"] for e in world.trace if e["kind"]=="wire_transmitted"),
                retained_bytes=sum(h.used for h in world.hosts.values()),
                durable_bytes=sum(h.storage_used for h in world.hosts.values()),
                power_losses=counts["power_loss"], replica_fixpoints=counts["replicated_fixpoint"],
                events=len(world.decisions)))
        if violations or result["metrics"]["unfinished"]:
            diagnostics(world, output.parent / (output.stem + "-diagnostics"))
    except Exception:
        result["exception"] = traceback.format_exc()
        diagnostics(world, output.parent / (output.stem + "-diagnostics"))
    if hashes() != source:
        result.update(status="source-changed", exception="Source changed during this history")
    output.write_text(json.dumps(result, sort_keys=True, separators=(",", ":")) + "\n")


def run(args):
    source = hashes()
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []
    for case in cases():
        if args.case and case["name"] not in args.case:
            continue
        for early in (False, True):
            for seed in args.seeds:
                request = dict(case=case, early=early, seed=seed)
                run_id = identity(dict(request, source=source))
                path = args.output / (run_id + ".json")
                if not path.exists():
                    input_path = args.output / (run_id + "-request.json")
                    input_path.write_text(json.dumps(request))
                    subprocess.run([sys.executable, str(Path(__file__).resolve()), "--worker",
                                    "--request", str(input_path), "--output", str(path)], check=True, timeout=300)
                row = json.loads(path.read_text())
                if row["source_hashes"] != source or hashes() != source:
                    raise RuntimeError("Source changed during campaign; do not publish mixed-source results")
                rows.append({k:v for k,v in row.items() if k != "source_hashes"})
                print(case["name"], "local" if early else "strict", seed, row["status"], flush=True)
    pairs = []
    for old in (r for r in rows if not r["early"]):
        new = next(r for r in rows if r["early"] and r["seed"]==old["seed"] and r["case"]==old["case"])
        pairs.append(dict(name=old["case"]["name"], seed=old["seed"],
            matched_inputs=all(old[k]==new[k] for k in ("config", "strategy", "authored_plans_hash"))))
    summary = dict(sources=source, runs=len(rows), pairs=pairs, rows=rows,
        limits="Synthetic matched histories of a simplification candidate, not a BRIEF change or capacity estimate. Strict and local release share corrected acquire/minimum/read rules. All fix acknowledgements still precede compute. Replicated cases use prepared fixed authorities and full-body quorum journals, not the complete producer/frontier path. No handoff, history reclamation or post-admission abort. Matching seeds do not couple different protocols' physical schedules. Maxima are finite cohorts, not measured production tails.")
    target = args.evidence or args.output / "summary.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(summary, sort_keys=True, separators=(",", ":")) + "\n")
    print(json.dumps(dict(runs=len(rows), errors=sum(r["status"]!="ok" for r in rows),
        violations=sum(bool(r.get("violations")) for r in rows),
        matched_pairs=sum(p["matched_inputs"] for p in pairs))))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--evidence", type=Path)
    parser.add_argument("--seeds", type=int, nargs="+", default=[1, 7, 19])
    parser.add_argument("--case", action="append")
    parser.add_argument("--worker", action="store_true")
    parser.add_argument("--request", type=Path)
    args = parser.parse_args()
    if args.worker:
        evaluate(json.loads(args.request.read_text()), args.output)
    else:
        run(args)
