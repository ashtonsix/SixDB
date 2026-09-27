"""Small, matched old/current traffic comparison in isolated Python processes.

This compares corrections to a synthetic execution model, not measured database
performance. Both versions retain prepared single authorities and growing history.
The authored workload, knobs and seed match; changed protocols need not consume
the same random decisions or assign the same valid serialization order.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys
import traceback


HERE = Path(__file__).resolve().parent
SOURCES = ("traffic.py", "sim.py", "kernel.py", "campaigns.py")


def identity(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def sources(path):
    return {name: hashlib.sha256((path / name).read_bytes()).hexdigest() for name in SOURCES}


def cases():
    wan = dict(topology="wan", count=100, interval_ns=2_000_000,
               slow_every=100, drain_ns=800_000_000)
    wan_strategy = dict(retry_ns=10_000_000, read_floor_first=False)
    result = [dict(name=name, config=dict(wan, workload=shape), strategy=wan_strategy)
              for name, shape in (("wan-independent", "wan-independent"),
                                  ("wan-dependent", "wan-mix"),
                                  ("wan-broad-read-narrow-output", "global-scan"))]
    result.extend([
        dict(name="hot-steady", config=dict(workload="hot", count=64, interval_ns=5_000)),
        dict(name="hot-burst", config=dict(workload="hot", count=64, interval_ns=1_000, burst=4)),
        dict(name="overlapping-bridge", config=dict(workload="bridge", count=64,
             interval_ns=8_000, slow_every=8, point_rmw=True)),
        dict(name="broad-possible-output", config=dict(workload="max-update", count=48,
             slow_every=12, point_rmw=True)),
        dict(name="overwrite-waits", config=dict(workload="overwrite", count=48,
             slow_every=48, slow_ns=2_000_000), strategy=dict(supersede=False)),
        dict(name="overwrite-supersedes", config=dict(workload="overwrite", count=48,
             slow_every=48, slow_ns=2_000_000), strategy=dict(supersede=True)),
        dict(name="continuing-point-stream", config=dict(workload="points", count=500,
             interval_ns=20_000, drain_ns=8_000_000)),
        dict(name="power-recovery", config=dict(workload="transfer", count=32,
             interval_ns=20_000, incident="power", fault_at_ns=300_000,
             fault_duration_ns=400_000, drain_ns=30_000_000)),
        dict(name="loss-and-duplicate-responses", config=dict(workload="transfer", count=32,
             interval_ns=20_000, loss=.1, duplicate=.25, drain_ns=30_000_000)),
    ])
    return result


def telemetry(world):
    waits, totals = {}, defaultdict(lambda: dict(completed_intervals=0, elapsed_ns=0, max_interval_ns=0))
    packets, packet_drops, terminal_responses_lost = {}, Counter(), 0
    transitions = Counter()
    for event in world.trace:
        kind = event["kind"]
        key = (event.get("actor"), str(event.get("op")), event.get("incarnation"))
        if kind in ("wait", "wait_cleared"):
            previous = waits.get(key)
            if previous and (kind == "wait_cleared" or previous[0] != event["reason"]):
                reason, start = waits.pop(key)
                elapsed = event["time"] - start
                row = totals[reason]
                row["completed_intervals"] += 1
                row["elapsed_ns"] += elapsed
                row["max_interval_ns"] = max(row["max_interval_ns"], elapsed)
            if kind == "wait" and key not in waits:
                waits[key] = (event["reason"], event["time"])
        if kind == "packet_departed":
            packets[tuple(event["packet"])] = (event["actor"], event["target"])
        elif kind == "packet_drop":
            packet_drops[event["reason"]] += 1
            source, target = packets.get(tuple(event["packet"]), (None, None))
            if target == "client" and source and source.startswith("coordinator"):
                terminal_responses_lost += 1
        elif kind == "traffic_transition":
            transitions[event["transition"]] += 1
    return dict(wait_intervals=dict(totals),
                wait_measurement="Sum across actor/operation intervals; overlapping waits are not critical-path latency; open intervals excluded.",
                outstanding_waits=dict(Counter(w["reason"] for w in world.report()["waits"])),
                transitions=dict(transitions), packet_drops=dict(packet_drops),
                terminal_response_packet_drops=terminal_responses_lost,
                trace_hash=world.report()["trace_hash"], choices_hash=world.report()["choices_hash"],
                hosts=world.report()["hosts"])


def worker(source, request_path, output):
    # A fresh process imports every simulator dependency from precisely one tree.
    sys.path.insert(0, str(source))
    import traffic
    from campaigns import _validate
    for name in SOURCES:
        module = sys.modules.get(name[:-3])
        if module is not None and Path(module.__file__).resolve() != (source / name).resolve():
            raise RuntimeError(f"mixed source import: {name}")
    request = json.loads(request_path.read_text())
    provenance = sources(source)
    result = dict(request, source_hashes=provenance, source_identity=identity(provenance),
                  python=platform.python_version(), status="error", exception=None)
    world_holder = {}
    original = traffic.build
    def capture(*args, **kwargs):
        world, plans, until = original(*args, **kwargs)
        world_holder.update(world=world, plans=plans, until=until)
        return world, plans, until
    traffic.build = capture
    try:
        case = request["case"]
        observation = traffic.evaluate(case, request["seed"], output.parent / (output.stem + "-diagnostics"))
        raw = _validate(observation)
        result.update(status="ok", observation=raw, telemetry=telemetry(world_holder["world"]),
                      authored_plans_hash=identity(world_holder["plans"]))
    except Exception:
        result["exception"] = traceback.format_exc()
    if sources(source) != provenance:
        result.update(status="source-changed", exception="Source changed during this history; not a valid comparison.")
    output.write_text(json.dumps(result, sort_keys=True, separators=(",", ":")) + "\n")


def compact(record):
    result = {k: record[k] for k in ("version", "name", "seed", "case", "status", "exception", "source_identity")}
    if "observation" not in record:
        return result
    o = record["observation"]
    result.update(config=o["details"]["config"], strategy=o["details"]["strategy"],
                  metrics=o["metrics"], violations=o["violations"], telemetry=record["telemetry"],
                  authored_plans_hash=record["authored_plans_hash"], cohorts={})
    for name, c in o["cohorts"].items():
        result["cohorts"][name] = {k: c[k] for k in ("offered", "completed", "refused", "unfinished", "completed_in_window")}
        result["cohorts"][name]["oldest_unfinished_ns"] = max(c["unfinished_age_ns"].values(), default=None)
        for origin in (0, 1):
            keys = {k for k in c["arrivals_ns"] if (int(k)-1) % 2 == origin}
            latency = [v for k, v in c["latency_ns"].items() if k in keys]
            result["cohorts"][name][f"origin{origin}"] = dict(offered=len(keys),
                completed=sum(k in keys for k in c["completions_ns"]),
                refused=sum(k in keys for k in c["refusals_ns"]),
                unfinished=sum(k in keys for k in c["unfinished_age_ns"]),
                max_latency_ns=max(latency, default=None))
    return result


def run(args):
    args.output.mkdir(parents=True, exist_ok=True)
    paths = {"old": args.old_source.resolve(), "current": args.current_source.resolve()}
    available, unavailable = {}, {}
    for version in args.versions:
        try:
            available[version] = sources(paths[version])
        except OSError as error:
            unavailable[version] = str(error)
    rows = []
    for version, provenance in available.items():
        for definition in cases():
            if args.case and definition["name"] not in args.case:
                continue
            name = definition["name"]
            case = {k: v for k, v in definition.items() if k != "name"}
            for seed in args.seeds:
                request = dict(version=version, name=name, case=case, seed=seed)
                run_id = identity(dict(request, source=provenance))
                path = args.output / (run_id + ".json")
                if not path.exists():
                    request_path = args.output / (run_id + "-request.json")
                    request_path.write_text(json.dumps(request))
                    subprocess.run([sys.executable, str(Path(__file__).resolve()), "--worker",
                        "--current-source", str(paths[version]), "--request", str(request_path),
                        "--output", str(path)], check=True, timeout=300)
                record = json.loads(path.read_text())
                if record["source_hashes"] != provenance:
                    raise RuntimeError("Recorded source differs from campaign source")
                rows.append(compact(record))
                print(version, name, seed, record["status"], flush=True)
        if sources(paths[version]) != provenance:
            raise RuntimeError(f"{version} source changed during campaign; do not publish a mixed-source summary")
    pairs = []
    for old in (r for r in rows if r["version"] == "old"):
        new = next((r for r in rows if r["version"] == "current" and (r["name"], r["seed"]) == (old["name"], old["seed"])), None)
        if new:
            matched = old["status"] == new["status"] == "ok" and all(old[k] == new[k] for k in ("config", "strategy", "authored_plans_hash"))
            pairs.append(dict(name=old["name"], seed=old["seed"], matched_inputs=matched))
    summary = dict(kind="synthetic-traffic-fidelity-comparison", source_hashes=available,
        runner_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        unavailable=unavailable, python=platform.python_version(), seeds=args.seeds,
        interpretation="Matched offered plans/configurations, not a coupled packet schedule. Corrected protocol adds announce and release transitions and queue fairness. Prepared single authorities, synthetic costs, no reclamation or universal capacity claim. Small cohorts report maxima, not production tail estimates.",
        runs=len(rows), pairs=pairs, rows=rows)
    target = args.evidence or args.output / "summary.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(summary, sort_keys=True, separators=(",", ":")) + "\n")
    print(json.dumps(dict(runs=len(rows), matched_pairs=sum(p["matched_inputs"] for p in pairs),
                         errors=sum(r["status"] != "ok" for r in rows),
                         violations=sum(bool(r.get("violations")) for r in rows), unavailable=unavailable)))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--old-source", type=Path, default=HERE.parents[2] / "build/captures/orbital-gauntlet/workbench/spikes/orbital-simulator")
    parser.add_argument("--current-source", type=Path, default=HERE)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--evidence", type=Path)
    parser.add_argument("--versions", choices=("old", "current"), nargs="+", default=["old", "current"])
    parser.add_argument("--seeds", type=int, nargs="+", default=[1, 7, 19])
    parser.add_argument("--case", action="append")
    parser.add_argument("--worker", action="store_true")
    parser.add_argument("--request", type=Path)
    args = parser.parse_args()
    if args.worker:
        worker(args.current_source.resolve(), args.request, args.output)
    else:
        run(args)
