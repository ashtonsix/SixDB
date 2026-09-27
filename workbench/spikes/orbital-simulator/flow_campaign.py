"""Executable campaigns over the existing dataflow actors and shared World."""
from __future__ import annotations

import argparse
from dataclasses import fields
import hashlib
import json
from pathlib import Path

from campaigns import Observation, Runner, cohort, combinations, hill_climb, ramp
from dataflow_scenario import FlowConfig, build, summarize


HERE = Path(__file__).resolve().parent


def provenance():
    return dict(kind="synthetic-model-campaign", sources={name: hashlib.sha256((HERE / name).read_bytes()).hexdigest()
        for name in ("kernel.py", "sim.py", "dataflow_scenario.py", "campaigns.py", "flow_campaign.py")})


def _diagnostics(world, output):
    if output is None:
        return
    output.mkdir(parents=True, exist_ok=False)
    (output / "choices.json").write_text(json.dumps(world.decisions, separators=(",", ":")) + "\n")
    with (output / "trace.jsonl").open("w") as stream:
        for row in world.trace:
            stream.write(json.dumps(row, separators=(",", ":")) + "\n")
    (output / "waits.json").write_text(json.dumps(dict(current=list(world.waits.values()),
                                                     retired=world.retired_waits), indent=2) + "\n")


def evaluate(case, seed, output=None):
    """One fixed offering window followed by a separately bounded drain.

    The dataflow batch has two sources and two required materializations. The
    open-loop foreground is the load axis; chunks/rows vary the competing batch.
    Pausing a worker is an authored physical incident, not an actor instruction.
    """
    params = dict(case)
    period = params.pop("period_ns", 4_000)
    window = params.pop("offer_window_ns", 100_000)
    drain = params.pop("drain_ns", 400_000)
    incident = params.pop("incident_ns", 0)
    network = params.pop("network", "symmetric")
    budget = params.pop("event_budget", 200_000)
    retain = params.pop("diagnostics", False)
    if min(period, window, budget) <= 0 or min(drain, incident) < 0:
        raise ValueError("positive offering parameters and nonnegative drain/incident required")
    if network not in {"symmetric", "asymmetric"}:
        raise ValueError("unknown network")
    allowed = {f.name for f in fields(FlowConfig)} - {"seed", "ordering", "foreground_count", "foreground_period_ns"}
    if not set(params) <= allowed:
        raise ValueError(f"unknown/controlled parameters: {set(params) - allowed}")
    count = (window + period - 1) // period
    config = FlowConfig(**params, seed=seed, ordering="shuffle", foreground_count=count,
                        foreground_period_ns=period)
    world = build(config)
    if network == "asymmetric":
        for (source, target), link in world.links.items():
            if source == "p1" and not target.startswith("p"):
                link.bandwidth /= 4
                link.latency *= 3
    if incident:
        world.fault(30_000, "pause", host="c0", resource="workers")
        world.fault(30_000 + incident, "pause", host="c0", resource="workers", paused=False)
    start, offered_until, until = 2_000, 2_000 + window, 2_000 + window + drain
    try:
        world.run(until=until, max_events=budget)
        summary = summarize(world, config)
        rows = world.trace

        def outcomes(kind, identity):
            result = {}
            for row in rows:
                if row["kind"] == kind:
                    key = row[identity]
                    result[key] = min(result.get(key, row["time"]), row["time"])
            return result

        arrivals = {f"foreground/{i}": start + i * period for i in range(count)}
        groups = dict(
            foreground=cohort(arrivals, outcomes("foreground_done", "op"), outcomes("foreground_refused", "op"),
                              offered_until=offered_until, until=until),
            sources=cohort({f"source{i}": start for i in range(2)}, outcomes("flow_source_finished", "actor"),
                           outcomes("flow_refused", "actor"), offered_until=offered_until, until=until),
            materializations=cohort({f"sink{i}": start for i in range(2)}, outcomes("flow_materialized", "actor"), {},
                                   offered_until=offered_until, until=until))
        fg = groups["foreground"]
        end = max(fg["completions_ns"].values(), default=None)
        final = max(summary["materialized_at"].values(), default=None)
        middle = start + window // 2
        def backlog(at):
            return (sum(t < at for t in arrivals.values())
                    - sum(t < at for t in fg["completions_ns"].values())
                    - sum(t < at for t in fg["refusals_ns"].values()))
        resume = 30_000 + incident
        pending_at_resume = [(g, key) for g in groups.values() for key, arrival in g["arrivals_ns"].items()
                             if arrival <= resume and g["completions_ns"].get(key, until + 1) > resume
                             and g["refusals_ns"].get(key, until + 1) > resume]
        recovered = [g["completions_ns"].get(key) for g, key in pending_at_resume]
        violations = [] if summary["correct"] else ["materialized reduction differs from whole-input oracle"]
        # Repeated telemetry from recovery is valid for durable results; foreground
        # operations have no replay/recovery path and must complete exactly once.
        if sum(r["kind"] == "foreground_done" for r in rows) != fg["completed"]:
            violations.append("duplicate foreground completion")
        metrics = dict(foreground_max_ns=max(fg["latency_ns"].values(), default=None),
            foreground_throughput_per_model_ms=fg["completed_in_window"] * 1_000_000 / window,
            foreground_late_throughput_per_model_ms=sum(middle <= t < offered_until for t in fg["completions_ns"].values())
                                                   * 1_000_000 / (offered_until - middle),
            foreground_backlog_mid=backlog(middle), foreground_backlog_end=backlog(offered_until),
            offered_per_model_ms=count * 1_000_000 / window,
            foreground_drain_ns=max(0, end - offered_until) if fg["completed"] == count else None,
            finalization_ns=final - start if groups["materializations"]["completed"] == 2 else None,
            wire_bytes=summary["wire_bytes"], wan_bytes=summary["wan_bytes"], worker_ns=summary["worker_ns"],
            unfinished=sum(g["unfinished"] for g in groups.values()),
            refused=sum(g["refused"] for g in groups.values()),
            pre_resume_work_drain_ns=max((t - resume for t in recovered), default=0)
                if incident and resume <= until and all(t is not None for t in recovered) else None,
            since_last_foreground_completion_ns=until - end if end is not None else None)
        observation = Observation(groups, metrics, violations, dict(
            offered_from_ns=start, offered_until_ns=offered_until, observed_until_ns=until,
            actor_reported_foreground=summary["foreground"],
            resource_refusal_attempts=summary["counts"].get("resource_refused", 0),
            materialized_at=summary["materialized_at"], trace_hash=summary["trace_hash"],
            waits=summary["waits"], pending_events=len(world.kernel.events),
            interpretation="Finite burst plus explicit drain; unfinished is not a deadlock proof."))
    except Exception:
        _diagnostics(world, output)
        raise
    if retain or violations or metrics["unfinished"]:
        _diagnostics(world, output)
    return observation


def run_study(output):
    source = provenance()
    runner = Runner(evaluate, output, source)
    seeds = (1, 7)
    # All placements and enhancement choices on identical arrivals and incidents.
    strategies = combinations(dict(placement=["source", "destination", "consumer"], enhance=[False, True]))
    load = [ramp(runner, strategy, "period_ns", [4_000, 800, 600, 400, 100], seeds,
                 limits={"foreground_max_ns": 20_000}) for strategy in strategies]
    # Health severity is varied separately from offered load, and then in mixes.
    health = [ramp(runner, dict(strategy, period_ns=800), "incident_ns", [0, 20_000, 80_000, 320_000, 1_000_000],
                   seeds, limits={"foreground_max_ns": 20_000}) for strategy in strategies]
    mixed_cases = combinations(dict(placement=["source", "destination", "consumer"], enhance=[False, True],
        chunks=[2, 8], window=[1, 4], retry_ns=[40_000, 160_000],
        incident_ns=[0, 80_000], network=["symmetric", "asymmetric"]), mode="pairwise")
    mixed = runner.matched([dict(c, period_ns=800) for c in mixed_cases], seeds)
    # A finite batch can inflate a short-window capacity impression. Repeat the
    # offering interval tenfold and retain late-window service/backlog separately.
    horizon = runner.matched([dict(s, period_ns=period, offer_window_ns=1_000_000)
                              for s in strategies for period in (800, 600, 400)], seeds)

    def neighbors(case):
        for name, options in dict(placement=["source", "destination", "consumer"], enhance=[False, True],
                                  window=[1, 2, 4], retry_ns=[40_000, 80_000, 160_000]).items():
            for value in options:
                if case[name] != value:
                    yield dict(case, **{name: value})

    def heldout(case):
        # Change pressure/topology as well as event/loss samples. Never feed these
        # results back into the search; the receipt retains failures and baselines.
        for mix in (dict(chunks=8, rows=13, network="asymmetric", duplicate=.15),
                    dict(chunks=3, rows=64, incident_ns=80_000, period_ns=600)):
            yield dict(case, **mix)

    search = hill_climb(runner,
        [dict(c, window=2, retry_ns=80_000, period_ns=800) for c in strategies], neighbors,
        seeds=seeds, heldout_seeds=(19, 41), objectives=("foreground_max_ns", "wan_bytes", "finalization_ns"),
        validation_cases=heldout, max_cases=36)
    if provenance() != source:
        raise RuntimeError("campaign sources changed during execution; retain run as mixed-source, rerun from capture")
    receipt = dict(kind="synthetic-model-campaign", provenance=source,
        load=[dict(strategy=s, **r) for s, r in zip(strategies, load)],
        health=[dict(strategy=s, **r) for s, r in zip(strategies, health)],
        pairwise_cases=mixed_cases, pairwise_records=[r["id"] for r in mixed], search=search,
        horizon_records=[r["id"] for r in horizon],
        unique_runs=len(runner.records), errors=[r["id"] for r in runner.records.values() if r["status"] != "ok"])
    (output / "summary.json").write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    receipt = run_study(args.output)
    print(json.dumps(dict(runs=receipt["unique_runs"], errors=len(receipt["errors"]),
        frontier=[e["case"] for e in receipt["search"]["frontier"]],
        validation_failures=sum(not v["feasible"] for v in receipt["search"]["validation"])), indent=2))
    return bool(receipt["errors"])


if __name__ == "__main__":
    raise SystemExit(main())
