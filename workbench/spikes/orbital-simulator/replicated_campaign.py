"""Bounded matched composition experiments for prepared full-body admission.

These finite histories examine safety, independent progress and recovery. They
are not throughput estimates or production quorum-admission latency estimates.
The decision remains coordinator-local; producer-frontier admission, elections,
authority handoff, extension checking and object reconstruction are not composed.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict
import hashlib
import difflib
import json
from pathlib import Path

from campaigns import Observation, Runner, all_work, cohort
from kernel import digest
from replicated_contention import Consumer, audit, build, consumer_names, witness_names
from traffic import Config, Strategy, diagnostics

HERE = Path(__file__).resolve().parent
SOURCES = ("kernel.py", "sim.py", "traffic.py", "campaigns.py", "replicated_contention.py", "replicated_campaign.py")
LIMITS = ("Prepared static leader, full input bodies PLP-written at witnesses; "
          "not producer-copy/frontier admission. Coordinator decisions remain local. "
          "Device-reset recovery only; no elections, handoff, extension/object composition or reclamation. "
          "Authored resource costs, finite histories, no native latency or throughput claim.")


def provenance():
    return dict(kind="prepared-quorum-contention-composition", limits=LIMITS,
                sources={name: hashlib.sha256((HERE / name).read_bytes()).hexdigest() for name in SOURCES})


def cases():
    base = dict(count=6, width=4, shards=2, topology="lan", drain_ns=3_000_000,
                slow_every=3, slow_ns=300_000)
    result = [dict(name=f"healthy-{shape}-lan", config=dict(base, workload=shape))
              for shape in ("points", "hot", "scan", "transfer", "max-update", "conditional")]
    result += [dict(name=f"healthy-{shape}-man", config=dict(base, workload=shape, topology="man"))
               for shape in ("points", "transfer", "scan")]
    result += [dict(name=f"points-{fault}", config=dict(base, workload="points"), fault=fault)
               for fault in ("one-follower-disconnected", "both-followers-until-600us",
                             "primary-consumer-destroyed", "coordinator-outcome-power-cut",
                             "primary-consumer-workers-paused")]
    result += [dict(name=f"completion-room-{memory}", config=dict(base, workload="points", count=1,
                                                                width=2, memory_bytes=memory))
               for memory in (8192, 12288, 16384)]
    result.append(dict(name="unfinished-wide-predecessor", config=dict(base, workload="bulk-rmw",
        count=3, width=2, slow_every=20, slow_ns=5_000_000, point_rmw=True,
        interval_ns=200_000, drain_ns=1_000_000)))
    return result


def inject_fault(world, kind):
    if kind == "none":
        return
    if kind in ("one-follower-disconnected", "both-followers-until-600us"):
        targets = ("w0_1",) if kind == "one-follower-disconnected" else ("w0_1", "w0_2")
        for target in targets:
            world.fault(0, "partition", source_host="h0", target_host=target)
            if len(targets) == 2:
                world.fault(600_000, "partition", source_host="h0", target_host=target, blocked=False)
    elif kind == "primary-consumer-destroyed":
        world.when("replicated_fixpoint", lambda e: e["actor"] == "consumer0_0" and e["epoch"] == 1,
                   "destroy", host="c0_0")
        world.fault(600_000, "power_on", host="c0_0")
    elif kind == "coordinator-outcome-power-cut":
        # Coordinator0 and prepared shard0 leader share this physical machine.
        world.when("durable_write", lambda e: e["actor"] == "coordinator0" and e["key"] == "outcome/1",
                   "power_loss", host="h0")
        world.fault(1_200_000, "power_on", host="h0")
    elif kind == "primary-consumer-workers-paused":
        world.fault(100_000, "pause", host="c0_0", resource="workers")
        world.fault(800_000, "pause", host="c0_0", resource="workers", paused=False)
    else:
        raise ValueError(f"unknown fault {kind}")


def evaluate(case, seed, output=None):
    config, strategy = Config(**case["config"]), Strategy(**case.get("strategy", {}))
    fault = case.get("fault", "none")
    if config.incident != "none" or config.topology not in ("lan", "man"):
        raise ValueError("campaign uses explicit faults and LAN/MAN only")
    world, plans, until = build(config, strategy, seed, replica_delays=(0, 7_000, 23_000))
    inject_fault(world, fault)
    try:
        world.run(until=until, max_events=1_000_000)
        violations = audit(world)
        done, refused, accepted = {}, {}, set()
        for event in world.trace:
            if event["kind"] == "traffic_response":
                done.setdefault(str(event["op"]), event["time"])
            elif event["kind"] == "traffic_refused":
                refused.setdefault(str(event["op"]), event["time"])
            elif event["kind"] == "traffic_accepted":
                accepted.add(str(event["op"]))
        offered_until = max(p["at"] for p in plans) + config.interval_ns
        cohorts = {}
        for name in sorted({p["cohort"] for p in plans}):
            arrivals = {str(p["id"]): p["at"] for p in plans if p["cohort"] == name}
            cohorts[name] = cohort(arrivals, {k:v for k,v in done.items() if k in arrivals},
                                   {k:v for k,v in refused.items() if k in arrivals},
                                   offered_until=offered_until, until=until)
        unfinished = {str(p["id"]) for p in plans} - set(done) - set(refused)
        replica_details, max_lag, disagreements = {}, 0, 0
        for shard in range(config.shards):
            consumers = [world.actors[name].actor for name in consumer_names(shard)]
            prefix = min(c.applied if isinstance(c, Consumer) else 0 for c in consumers)
            highest = max(c.applied if isinstance(c, Consumer) else 0 for c in consumers)
            common = [c.state_hashes.get(prefix) if c is not None else None for c in consumers]
            output_hashes = [c.output_hashes.get(prefix) if c is not None else None for c in consumers]
            equal = len(set(common)) == 1 and len(set(output_hashes)) == 1
            disagreements += not equal
            max_lag = max(max_lag, highest - prefix)
            replica_details[str(shard)] = dict(applied=[c.applied if c is not None else None for c in consumers],
                shared_prefix=prefix, common_state_hash=common[0] if equal else None,
                common_output_hash=output_hashes[0] if equal else None, shared_prefix_equal=equal,
                witness_prefixes={name:len(world.actors[name].actor.batches)
                    if world.actors[name].actor is not None else None for name in witness_names(shard)})
        if disagreements:
            violations.append("replica common-prefix state/output differs at observation end")
        fired_kinds = {"partition", "power_loss", "power_on", "storage_destroyed", "service_pause"}
        fired = [{k:v for k,v in e.items() if k not in ("cause",)} for e in world.trace if e["kind"] in fired_kinds]
        # An intended fault that never fired is a rejected experiment, not healthy evidence.
        if fault != "none" and not fired:
            violations.append("intended physical fault did not fire")
        if world.triggers:
            violations.append("intended semantic fault trigger did not fire")
        origins = {str(origin): dict(offered=sum(p["origin"] == origin for p in plans),
            completed=sum(p["origin"] == origin and str(p["id"]) in done for p in plans),
            unfinished=sum(p["origin"] == origin and str(p["id"]) in unfinished for p in plans))
            for origin in sorted({p["origin"] for p in plans})}
        restored_at = min((e["time"] for e in fired if e["kind"] == "power_on"
                           or e["kind"] == "partition" and not e["blocked"]
                           or e["kind"] == "service_pause" and not e["paused"]), default=None)
        for origin, row in origins.items():
            row["completed_before_restoration"] = sum(
                p["origin"] == int(origin) and str(p["id"]) in done
                and restored_at is not None and done[str(p["id"])] < restored_at for p in plans)
        latencies = [value for c in cohorts.values() for value in c["latency_ns"].values()]
        metrics = dict(offered=len(plans), completed=len(done), refused=len(refused), unfinished=len(unfinished),
            accepted=len(accepted), accepted_unfinished=len(unfinished & accepted),
            completion_max_ns=max(latencies, default=None), wire_bytes=sum(e["size"] for e in world.trace
                                                                                  if e["kind"] == "wire_transmitted"),
            last_completion_ns=max(done.values(), default=None), fired_fault_events=len(fired),
            max_replica_epoch_lag=max_lag, replica_prefix_disagreements=disagreements,
            retained_bytes=sum(h.used for h in world.hosts.values()),
            durable_bytes=sum(h.storage_used for h in world.hosts.values()),
            event_choices=len(world.decisions), pending_events=len(world.kernel.events))
        for name, group in cohorts.items():
            metrics[f"{name}_max_ns"] = max(group["latency_ns"].values(), default=None)
        details = dict(name=case["name"], config=asdict(config), strategy=asdict(strategy),
            limits=LIMITS, offered_from_ns=min(p["at"] for p in plans), offered_until_ns=offered_until,
            observed_until_ns=until, replica_fold_ns=[0, 7_000, 23_000], fault=fault, actual_faults=fired,
            replicas=replica_details, origins=origins, first_restoration_ns=restored_at, waits=list(world.waits.values()),
            trace_hash=digest(world.trace), choices_hash=digest(world.decisions),
            unfinished_ids=sorted(unfinished), accepted_unfinished_ids=sorted(unfinished & accepted),
            interpretation="Completion latency is conditional on completion; unfinished is not a deadlock proof.")
        observation = Observation(cohorts, metrics, sorted(set(violations)), details)
    except Exception:
        diagnostics(world, output)
        raise
    if violations or unfinished:
        diagnostics(world, output)
    return observation


def run_study(output, evidence, compare=None):
    source = provenance()
    def checked(case, seed, diagnostic_path):
        if provenance() != source:
            raise RuntimeError("sources changed before case; reject mixed-source run")
        observed = evaluate(case, seed, diagnostic_path)
        if provenance() != source:
            raise RuntimeError("sources changed during case; reject mixed-source run")
        return observed
    runner = Runner(checked, output, source)
    for case in cases():
        rows = runner.matched([case], (1, 7, 19))
        print(case["name"], [(r["seed"], r["status"],
              r["observation"]["metrics"]["completed"] if r["observation"] else None) for r in rows], flush=True)
    if provenance() != source:
        raise RuntimeError("sources changed during campaign; reject mixed-source summary")
    records = list(runner.records.values())
    summary = dict(provenance=source, seeds=[1, 7, 19], cases=len(cases()), runs=len(records),
        errors=[r["id"] for r in records if r["status"] != "ok"],
        violations=[r["id"] for r in records if r["status"] == "ok" and r["observation"]["violations"]],
        unfinished=[r["id"] for r in records if r["status"] == "ok" and r["observation"]["metrics"]["unfinished"]],
        all_offered_completed=[r["id"] for r in records if all_work(r)],
        diagnostics_directory=str(output),
        interpretation="Every case/seed retained, including rejected and unfinished histories. "
                       "status=ok means the evaluator ran, not that service goals passed.")
    (output / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    # Keep programmatic cohorts/timing, actual incidents, source identities and
    # boundaries, but avoid duplicating verbose wait telemetry in the checked-in receipt.
    compact = []
    for record in records:
        row = dict(record)
        if row["observation"]:
            row["observation"] = dict(row["observation"])
            detail = dict(row["observation"]["details"])
            waits = detail.pop("waits")
            detail["wait_reasons"] = sorted({w["reason"] for w in waits})
            detail["wait_count"] = len(waits)
            row["observation"]["details"] = detail
        compact.append(row)
    retained = dict(summary=summary, records=compact)
    if compare is not None:
        prior = json.loads((compare / "summary.json").read_text())
        old_core = (compare / "replicated_contention.py").read_bytes()
        if hashlib.sha256(old_core).hexdigest() != prior["provenance"]["sources"]["replicated_contention.py"]:
            raise ValueError("prior core copy does not match prior campaign source identity")
        fields = ("offered", "completed", "refused", "unfinished", "completion_max_ns", "wire_bytes", "event_choices")
        matched = []
        for record in records:
            previous = json.loads((compare / (record["id"] + ".json")).read_text())
            matched.append(dict(id=record["id"], name=record["case"]["name"], seed=record["seed"],
                before={key:previous["observation"]["metrics"][key] for key in fields},
                after={key:record["observation"]["metrics"][key] for key in fields}))
        retained["retry_comparison"] = dict(prior_directory=str(compare), prior_provenance=prior["provenance"],
            rows=matched, interpretation="Matched histories before/after bounding duplicate replication. "
                "Prior implementation is retained as the rejected strategy; physical costs remain authored.",
            core_diff=list(difflib.unified_diff(old_core.decode().splitlines(),
                (HERE / "replicated_contention.py").read_text().splitlines(),
                fromfile="before/replicated_contention.py", tofile="after/replicated_contention.py", lineterm="")))
    evidence.parent.mkdir(parents=True, exist_ok=True)
    evidence.write_text(json.dumps(retained, indent=2, sort_keys=True) + "\n")
    return summary


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, default=HERE / "evidence/replicated-contention.json")
    parser.add_argument("--compare", type=Path, help="Prior completed campaign with its exact saved core")
    args = parser.parse_args()
    result = run_study(args.output, args.evidence, args.compare)
    print(json.dumps({k:result[k] for k in ("runs", "errors", "violations", "unfinished")}))
