"""Adversarial histories over LEAD-owned actors; no replacement protocol model.

The observer may inspect the environment. No observation is fed back into actors.
The four-entry probes exercise failure boundaries, not sustainable throughput.
"""
from __future__ import annotations

from dataclasses import asdict, dataclass
import json
from pathlib import Path

from campaigns import Observation, cohort
import composed
import protocol


@dataclass
class Trial:
    world: object
    case: dict
    until: int
    arrivals: dict
    commands: dict
    flow_config: object = None


def cases():
    """Small concrete panel. Campaigns can vary named severity/cost/time fields."""
    return ([dict(family="payload", severity=s) for s in range(7)]
            + [dict(family="response", severity=s) for s in (1, 2, 3, 4)]
            + [dict(family="finalization", resource=r, severity=1)
               for r in ("durable", "workers", "control", "disk_bytes")]
            + [dict(family="composed", severity=s) for s in range(4)])


def _integer(case, key, default, *, minimum=0):
    value = case.get(key, default)
    if type(value) is not int or value < minimum:
        raise ValueError(f"{key} must be an integer >= {minimum}")
    return value


def build_case(case, seed=1, replay=None):
    family = case.get("family", "payload")
    specific = {
        "payload": {"heal_ns", "negative"},
        "response": {"start_ns", "heal_ns", "negative"},
        "finalization": {"start_ns", "heal_ns", "resource", "budget_bytes", "negative"},
        "composed": {"start_ns", "heal_ns", "factor"},
    }
    if family not in specific:
        raise ValueError("unknown adversarial family")
    allowed = {"family", "severity", "until_ns", "ordering"} | specific[family]
    if set(case) - allowed:
        raise ValueError(f"unsupported {family} fields: {sorted(set(case) - allowed)}")
    severity = _integer(case, "severity", 0)
    if severity > {"composed": 3, "payload": 6}.get(family, 4):
        raise ValueError("unsupported severity")
    until = _integer(case, "until_ns", 3_000_000, minimum=300_001)
    start = _integer(case, "start_ns", 100_000 if family == "finalization" else 500_000)
    heal = _integer(case, "heal_ns", 1_200_000)
    if start >= heal:
        raise ValueError("start_ns must precede heal_ns")
    ordering = case.get("ordering", "shuffle")
    if family == "composed":
        if case.get("negative") is not None:
            raise ValueError("composed negative controls belong to the existing bindings")
        world, flow = composed.build(seed=seed, ordering=ordering, replay=replay,
                                     hold_artifact_until=heal if severity else None)
        expected = composed.oracle(flow)
        commands = {1: dict(kind="set", key="unrelated", value=7),
                    2: dict(kind="set", key="aggregate", value=expected["sum"], artifact=expected)}
        arrivals = {"publication": {str(i): 2_000 for i in commands},
                    "version_read": {str(i): 2_000 for i in commands},
                    "private_result": {f"sink{i}": 2_000 for i in range(2)},
                    "foreground": {f"foreground/{i}": 2_000 + i * flow.foreground_period_ns
                                   for i in range(flow.foreground_count)}}
        if severity >= 2:
            factor = _integer(case, "factor", 4, minimum=1)
            world.slowdown("c0", "workers", factor)
        if severity >= 3:
            # This is deliberately an exogenous timeline, not a guessed restart
            # after a callback that may never occur at a higher load.
            world.fault(start, "power_loss", host="c0")
            world.fault(start + 100_000, "power_on", host="c0")
            world.fault(start + 200_000, "crash", actor="application")
            world.fault(start + 300_000, "restart", actor="application")
        return Trial(world, dict(case), until, arrivals, commands, flow)

    negative = case.get("negative")
    if negative not in (None, "publish-too-early", "chosen-before-payload"):
        raise ValueError("unknown protocol negative control")
    commands = {i: dict(command) for i, command in enumerate(protocol.DEFAULT_COMMANDS, 1)}
    world = protocol.build_scenario(seed=seed, ordering=ordering, replay=replay,
                                    negative=negative, offer=False)
    arrivals = {"publication": {str(i): 100_000 + i * 40_000 for i in commands},
                "producer_knowledge": {str(i): 100_000 + i * 40_000 for i in commands},
                "ordinary": {"ordinary": 150_000}}
    for lsn, command in commands.items():
        world.inject("producer", "submit", dict(lsn=lsn, command=command),
                     at=arrivals["publication"][str(lsn)])
    world.inject("ordinary", "work", {}, at=150_000)

    if family == "payload" and 0 < severity <= 4:
        holders = ("source",) if severity == 1 else ("source", "copy_b", "copy_c")
        for host in holders:
            world.partition(host, "reader")
            if severity <= 2:
                world.fault(heal, "partition", source_host=host, target_host="reader", blocked=False)
        if severity == 4:
            for host in holders:
                world.when("prefix_chosen", lambda row: len(row["entries"]) == 4,
                           "destroy", host=host)
    elif family == "payload" and severity >= 5:
        # Past publication remains lawful. These incidents test what still
        # survives after the promised outcome, not retroactive invalidation.
        hosts = ["source", "copy_b", "copy_c"]
        if severity == 6:
            hosts.append("reader")
        for host in hosts:
            world.when("published", lambda row: row["lsn"] == 4, "destroy", host=host)
    elif family == "response" and severity:
        world.partition("reader", "source")
        if severity < 4:
            world.fault(heal, "partition", source_host="reader", target_host="source", blocked=False)
        if severity >= 2:
            world.fault(start, "power_loss", host="reader")
            world.fault(start + 100_000, "power_on", host="reader")
        if severity >= 3:
            world.partition("reader", "relay_b")
            for actor, at in (("producer", start + 150_000), ("consumer", start + 200_000)):
                world.inject(actor, "route_update", dict(version=2, via="relay_b"), at=at)
                world.inject(actor, "route_update", dict(version=1, via="relay_a"), at=at + 80_000)
            if severity < 4:
                world.fault(heal, "partition", source_host="reader", target_host="relay_b", blocked=False)
    elif family == "finalization" and severity:
        resource = case.get("resource", "durable")
        if resource == "durable":
            world.hosts["reader"].config.durable_bytes = _integer(case, "budget_bytes", 256)
        elif resource in {"workers", "control", "disk_bytes"}:
            # Pause before ordinary work and application finalization arrive.
            world.fault(start, "pause", host="reader", resource=resource)
            if severity < 4:
                world.fault(heal, "pause", host="reader", resource=resource, paused=False)
        else:
            raise ValueError("unknown finalization resource")
    return Trial(world, dict(case), until, arrivals, commands)


def _first(rows, identity):
    result = {}
    for row in rows:
        key = identity(row)
        result[key] = min(row["time"], result.get(key, row["time"]))
    return result


def _public_values(commands):
    values, snapshots = {}, {}
    for lsn, command in sorted(commands.items()):
        key, value = command["key"], command["value"]
        values[key] = value if command["kind"] == "set" else values.get(key, 0) + value
        snapshots[lsn] = dict(values)
    return snapshots


def observe(trial):
    world, commands = trial.world, trial.commands
    rows, until = world.trace, world.kernel.now
    baseline = composed.audit(world, trial.flow_config) if trial.flow_config else protocol.audit(world)
    violations = list(baseline["violations"])
    snapshots = _public_values(commands)
    publications = [row for row in rows if row["kind"] == "published"]
    for row in publications:
        if row["lsn"] not in snapshots or row["values"] != snapshots[row["lsn"]]:
            violations.append("publication differs from the harness-authored input history")
    completed = {"publication": _first(publications, lambda row: str(row["lsn"]))}
    if trial.flow_config:
        completed.update(
            version_read=_first([r for r in rows if r["kind"] == "job_version_read"], lambda r: str(r["lsn"])),
            private_result=_first([r for r in rows if r["kind"] == "flow_materialized"], lambda r: r["actor"]),
            foreground=_first([r for r in rows if r["kind"] == "foreground_done"], lambda r: r["op"]))
    else:
        completed.update(
            producer_knowledge=_first([r for r in rows if r["kind"] == "wait_cleared"
                                       and r.get("actor") == "producer"
                                       and str(r.get("op", "")).startswith(protocol.STREAM + ":")],
                                      lambda r: str(r["op"]).split(":")[1]),
            ordinary=_first([r for r in rows if r["kind"] == "ordinary_done"], lambda r: "ordinary"))
    refusals = {name: {} for name in trial.arrivals}
    if trial.flow_config:
        refusals["foreground"] = _first([r for r in rows if r["kind"] == "foreground_refused"],
                                         lambda r: r["op"])
    cohorts = {}
    for name, arrivals in trial.arrivals.items():
        unexpected = set(completed[name]) - set(arrivals)
        if unexpected:
            violations.append(f"unexpected {name} identities: {sorted(unexpected)}")
        done = {key: time for key, time in completed[name].items() if key in arrivals}
        cohorts[name] = cohort(arrivals, done, refusals[name], offered_until=max(arrivals.values(), default=0) + 1,
                               until=until)

    # Current state is separate from protocol.audit's preceding durable facts.
    # Only canonical payload storage is covered; cached state, pending network
    # bytes, code/KMS and arbitrary reconstruction recipes are not certified here.
    current, historic = {}, {}
    for lsn, command in commands.items():
        body = dict(stream=protocol.STREAM, lsn=lsn, command=command)
        found = []
        for holder in protocol.HOLDERS:
            host = world.actors[holder].host
            if world.durable(host, holder).get(f"payload/{lsn}") == body:
                found.append(dict(holder=holder, host=host, domain=world.hosts[host].config.domain,
                                  online=world.hosts[host].up and world.actors[holder].live))
        current[str(lsn)] = found
        historic[str(lsn)] = sorted({world.hosts[r["host"]].config.domain for r in rows
                                    if r["kind"] == "durable_write" and r.get("actor") in protocol.HOLDERS
                                    and r["key"] == f"payload/{lsn}" and r["value"] == body})
    chosen = max((len(r["entries"]) for r in rows if r["kind"] == "prefix_chosen"), default=0)
    durable_public = world.durable(world.actors["consumer"].host, "consumer")
    retained_public = sorted(int(k.split("/")[1]) for k, v in durable_public.items()
                             if k.startswith("applied/") and v["values"] == snapshots.get(v["lsn"]))
    current_body_prefix = 0
    for lsn in sorted(commands):
        if lsn != current_body_prefix + 1 or not current[str(lsn)]:
            break
        current_body_prefix = lsn
    report = world.report()
    lags = [age for c in cohorts.values() for age in c["unfinished_age_ns"].values()]
    latency = cohorts["publication"]["latency_ns"]
    foreground_name = "foreground" if trial.flow_config else "ordinary"
    foreground = cohorts[foreground_name]["latency_ns"]
    alive_witnesses = sum(world.hosts[world.actors[a].host].up and world.actors[a].live
                          for a in protocol.WITNESSES)
    metrics = dict(publication_max_ns=max(latency.values(), default=None),
                   foreground_max_ns=max(foreground.values(), default=None),
                   oldest_unfinished_ns=max(lags, default=None), chosen_prefix=chosen,
                   historically_two_domain_bodies=sum(len(v) >= 2 for v in historic.values()),
                   currently_retained_bodies=sum(bool(v) for v in current.values()),
                   current_canonical_body_prefix=current_body_prefix,
                   current_materialized_state_lsn=max(retained_public, default=0),
                   currently_online_bodies=sum(any(h["online"] for h in v) for v in current.values()),
                   chosen_bodies_without_durable_copy=sum(not current[str(i)] for i in range(1, chosen + 1)),
                   retained_public_versions=len(retained_public), live_witnesses=alive_witnesses,
                   worker_ns=sum(v["busy_ns"] for k, v in report["resources"].items() if k.endswith("/workers")),
                   control_ns=sum(v["busy_ns"] for k, v in report["resources"].items() if k.endswith("/control")),
                   completed_wire_bytes=sum(r["size"] for r in rows if r["kind"] == "wire_transmitted"),
                   resident_bytes=sum(h["memory_used"] for h in report["hosts"].values()),
                   durable_bytes=sum(h["durable_used"] for h in report["hosts"].values()),
                   active_waits=len(report["waits"]), retired_waits=len(report["retired_waits"]))
    if trial.flow_config:
        source_records = world.durable(world.actors["application"].host, "application")
        metrics["retained_outbox_commands"] = sum(source_records.get(f"outbox/{i}") == command
                                                 for i, command in commands.items())
    details = dict(case=trial.case, until_ns=until, bounded_protocol_entries=len(commands),
                   safety=baseline, current_payload_copies=current, historical_domains=historic,
                   retained_public_versions=retained_public, model=report,
                   outcome_definition="historical terminal observations; journal-backed publication and private artifacts are separate; surviving storage is measured independently",
                   pending_semantic_triggers=len(world.triggers),
                   incident_events=[{k: v for k, v in r.items() if k not in {"cause", "parents"}}
                                    for r in rows if r["kind"] in {"partition", "process_crash", "power_loss",
                                                                  "power_on", "storage_destroyed", "route_changed",
                                                                  "service_pause", "slowdown"}])
    return Observation(cohorts, metrics, sorted(set(violations)), details)


def evaluate(case, seed=1, output_path=None):
    trial = build_case(case, seed)
    world = trial.world
    try:
        world.run(until=trial.until)
        return observe(trial)
    finally:
        # Preserve partial diagnostics even when the runtime or observer raises.
        # campaigns.Runner owns exception classification and the campaign record.
        if output_path is not None:
            path = Path(output_path)
            path.mkdir(parents=True, exist_ok=False)
            (path / "case.json").write_text(json.dumps(dict(case=case, seed=seed), indent=2) + "\n")
            (path / "choices.json").write_text(json.dumps(world.decisions, separators=(",", ":")) + "\n")
            with (path / "trace.jsonl").open("w") as stream:
                for row in world.trace:
                    stream.write(json.dumps(row, separators=(",", ":")) + "\n")
            (path / "model.json").write_text(json.dumps(world.report(), indent=2) + "\n")


if __name__ == "__main__":
    print(json.dumps([dict(case=case, **asdict(evaluate(case))) for case in cases()], indent=2))
