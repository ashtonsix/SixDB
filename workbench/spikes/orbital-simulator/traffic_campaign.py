"""Matched execution experiments, overload ramps and constrained policy search."""
from __future__ import annotations

import argparse
from dataclasses import asdict
import hashlib
import json
from pathlib import Path

from campaigns import Observation, Runner, combinations, hill_climb, ramp
from traffic import Strategy, evaluate


HERE = Path(__file__).resolve().parent


def evaluator(case, seed, output):
    params = dict(case)
    if "panel" in params:
        panel = params.pop("panel")
        observations = [evaluator(dict(params, config=dict(params.get("config", {}), **patch)),
                                  seed, output / str(i) if output else None) for i, patch in enumerate(panel)]
        groups = {f"{i}/{name}": group for i, o in enumerate(observations) for name, group in o.cohorts.items()}
        metrics = {}
        for name in set().union(*(o.metrics for o in observations)):
            values = [o.metrics[name] for o in observations if name in o.metrics]
            metrics[name] = None if any(v is None for v in values) else sum(values) if name == "wire_bytes" else max(values)
        return Observation(groups, metrics, [e for o in observations for e in o.violations],
                           dict(panel=[o.details for o in observations], aggregation="worst across training workloads; total wire bytes"))
    config = dict(params.pop("config", {}))
    if "load_interval_ns" in params:
        interval = params.pop("load_interval_ns")
        window = params.pop("offering_ns", 1_000_000)
        config.update(interval_ns=interval, count=max(1, window // interval))
    if "severity" in params:
        config["severity"] = params.pop("severity")
    if "memory_bytes" in params:
        config["memory_bytes"] = params.pop("memory_bytes")
    return evaluate(dict(params, config=config), seed, output)


def provenance():
    return dict(kind="synthetic-execution-campaign", sources={name: hashlib.sha256((HERE / name).read_bytes()).hexdigest()
        for name in ("kernel.py", "sim.py", "traffic.py", "campaigns.py", "traffic_campaign.py")})


def run_study(output):
    source = provenance()
    runner = Runner(evaluator, output, source)
    seeds = (1, 7)
    comparisons = []
    for workload in ("scan", "max-update", "bulk-rmw", "bulk-blind", "conditional", "bridge", "hot", "overwrite"):
        for mode in ("mv", "protection", "shard"):
            comparisons.append(dict(config=dict(workload=workload, count=48, slow_every=12, point_rmw=True),
                                    strategy=dict(ordering=mode)))
    # Rare remote work overlaps a continuing local stream for hundreds of ms.
    # A tiny burst ending before the first WAN response would hide the interference.
    for workload in ("wan-independent", "wan-mix", "global-scan"):
        for mode in ("mv", "protection", "shard"):
            comparisons.append(dict(config=dict(workload=workload, topology="wan", count=100,
                interval_ns=2_000_000, slow_every=100, drain_ns=800_000_000),
                strategy=dict(ordering=mode, retry_ns=10_000_000, read_floor_first=False)))
    shape_records = runner.matched(comparisons, seeds)
    print("shapes", len(runner.records), flush=True)

    load = [ramp(runner, dict(config=dict(workload=shape, drain_ns=8_000_000), strategy=strategy),
                 "load_interval_ns", [100_000, 20_000, 5_000, 2_500], seeds,
                 limits={f'{"hot" if shape == "hot" else "point"}_max_ns':500_000})
            for shape, strategy in (("points", dict(batch=1)), ("points", dict(batch=16)),
                                     ("hot", dict(wake_waiters=False)), ("hot", dict(wake_waiters=True)))]
    print("load", len(runner.records), flush=True)

    health = [ramp(runner, dict(config=dict(workload="scan", count=64, incident=incident,
                            fault_at_ns=700_000, fault_duration_ns=150_000), strategy=dict(backoff=backoff)),
                   "severity", [1, 4, 16, 64], seeds, limits={"point_max_ns":500_000})
              for incident in ("pause", "control", "power") for backoff in (False, True)]
    memory = ramp(runner, dict(config=dict(workload="scan", count=64)), "memory_bytes",
                  [512_000, 128_000, 32_000, 8_000], seeds)
    print("health", len(runner.records), flush=True)

    mixes = combinations(dict(workload=["points", "scan", "transfer", "overwrite"],
                              topology=["lan", "man", "asymmetric"], burst=[1, 8],
                              incident=["none", "pause", "partition"],
                              duplicate=[0, .4], metadata_ns_per_entry=[0, 30]), mode="pairwise")
    mixed = runner.matched([dict(config=dict(c, count=40, drain_ns=150_000_000),
                                  strategy=dict(supersede=True)) for c in mixes], seeds)
    print("mixes", len(runner.records), flush=True)

    # Distinguish output coverage, shared CPU and unfinished work; keep controls
    # that leave default decisions correct but deliberately slower.
    controls = runner.matched([dict(config=dict(workload="overwrite", count=48, slow_every=48,
                                      slow_ns=2_000_000, metadata_ns_per_entry=metadata),
                                  strategy=dict(supersede=supersede, quantum_ns=quantum))
                               for supersede in (False, True) for quantum in (0, 20_000)
                               for metadata in (0, 5, 30)], seeds)

    base = dict(config=dict(count=64, interval_ns=8_000, slow_every=16), panel=[
        dict(workload="scan"), dict(workload="points", count=120, interval_ns=5_000),
        dict(workload="hot", interval_ns=5_000)])
    # Resolve defaults before searching: spelling a default explicitly must not
    # spend a candidate or create another apparently independent frontier entry.
    starts = [dict(base, strategy=asdict(Strategy(ordering=mode))) for mode in ("mv", "protection", "shard")]
    def neighbors(case):
        for name, choices in dict(batch=[1, 8, 16], window=[4, 16, 32], quantum_ns=[0, 20_000],
                                  wake_waiters=[False, True], lanes=["origin", "class"],
                                  retry_ns=[50_000, 200_000], read_floor_first=[False, True]).items():
            for value in choices:
                if case["strategy"].get(name) != value:
                    yield dict(case, strategy=dict(case["strategy"], **{name:value}))

    def validate(case):
        for patch in (dict(workload="max-update", point_rmw=True, burst=4),
                      dict(workload="scan", metadata_ns_per_entry=30, handler_ns=500),
                      dict(workload="scan", incident="pause", fault_at_ns=180_000, fault_duration_ns=400_000),
                      dict(workload="wan-independent", topology="asymmetric", drain_ns=150_000_000,
                           interval_ns=1_000_000, slow_every=64)):
            yield {k:v for k,v in dict(case, config=dict(case["config"], **patch)).items() if k != "panel"}
    search = hill_climb(runner, starts, neighbors, seeds=seeds, heldout_seeds=(19,41),
                        objectives=("point_max_ns", "hot_max_ns", "wire_bytes", "drain_ns"),
                        validation_cases=validate, max_cases=32)
    print("search", len(runner.records), flush=True)
    if source != provenance():
        raise RuntimeError("campaign sources changed while running; retain records but reject mixed-source summary")
    records = list(runner.records.values())
    summary = dict(provenance=source, runs=len(records),
        errors=[r["id"] for r in records if r["status"] != "ok"],
        violations=[r["id"] for r in records if r["status"] == "ok" and r["observation"]["violations"]],
        comparisons=[r["id"] for r in shape_records], load=load, health=health, memory=memory,
        mixes=[r["id"] for r in mixed], controls=[r["id"] for r in controls], search=search)
    (Path(output) / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    return summary


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = run_study(args.output)
    print(json.dumps({k: result[k] for k in ("runs", "errors", "violations")}))
