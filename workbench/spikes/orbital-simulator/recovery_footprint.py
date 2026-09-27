"""Compare whole-journal and bounded-record reopening of the same history.

This is a recovery implementation experiment, not history reclamation. The
standalone traffic journal is contiguous, immutable and has one writer; recovery
starts only after a device reset cancels old submissions. A missing next record
is therefore its end. These assumptions do not establish authority after a
process-only restart, nor permit skipping a damaged journal record.
"""
from __future__ import annotations

import argparse
from dataclasses import replace
import hashlib
import json
from pathlib import Path

from kernel import digest
from sim import encoded_size
from traffic import Config, Shard, Strategy, audit, build, initial_state, workload


class StreamingShard(Shard):
    def on(self, ctx, kind, data):
        if kind in ("boot", "replay-next"):
            if "base" not in self.leases:
                lease = ctx.reserve(1024 + encoded_size(self.versions), "recovered-shard-base")
                if lease is None:
                    ctx.wait("recovery", "resident base capacity")
                    ctx.timer(100_000, "replay-next")
                    return
                self.leases["base"] = lease
            if not ctx.load(f"journal/{self.sequence + 1}", "replay-loaded"):
                ctx.wait("recovery", "record read capacity")
                ctx.timer(100_000, "replay-next")
        elif kind == "replay-loaded":
            if not data["ok"]:
                ctx.timer(100_000, "replay-next")
                return
            batch = data["value"]
            if batch is None:
                self.ready = True
                ctx.clear_wait("recovery")
                ctx.note("traffic_recovered", records=self.sequence)
                ctx.note("stream_recovered_state", logical_hash=digest(self.logical_state()))
                self.report_waits(ctx)
                return
            allowance = sum(encoded_size(request) + 96 for request in batch)
            lease = ctx.reserve(allowance, "recovered-journal-record")
            if lease is None:
                ctx.wait("recovery", "resident history capacity")
                ctx.timer(100_000, "replay-next")
                return
            # Retained request state and the current read buffer are both paid.
            # The callback retires the latter before asking for the next record.
            cost = max(1, self.metadata_ns_per_entry * len(batch) *
                       (len(self.versions) + len(self.tickets) + len(self.waiting) + 1))
            if ctx.compute(cost, "replay-apply", dict(batch=batch, lease=lease),
                           pool="control", leases=(lease,)):
                self.leases[f"journal/{self.sequence + 1}"] = lease
            else:
                ctx.release(lease)
                ctx.timer(100_000, "replay-next")
        elif kind == "replay-apply":
            for request in data["batch"]:
                self.apply(request)
            self.sequence += 1
            ctx.timer(1, "replay-next")
        else:
            super().on(ctx, kind, data)


def experiment(memory_bytes=12_000, mode="whole", seed=1, replay=None):
    config = Config(count=12, interval_ns=200_000, memory_bytes=memory_bytes,
                    start_ns=100_000, drain_ns=3_000_000)
    strategy = Strategy()
    world, plans, before = build(config, strategy, seed, replay=replay)
    # Both modes run the identical healthy implementation. Only the restarted
    # actor factory differs; the factory contains configuration, never history.
    if mode == "stream":
        initial = initial_state(config)
        world.actors["shard0"].factory = lambda: StreamingShard(
            0, initial, strategy, config.metadata_ns_per_entry)
    elif mode != "whole":
        raise ValueError(mode)
    # A new ordinary request is offered through the real client after recovery
    # begins. Completion of the old cohort alone cannot conceal failed service.
    extra = workload(replace(config, count=13), seed)[-1]
    extra.update(at=before + 500_000, origin=0, cohort="after-recovery",
                 reads=[], writes=["s0:k0"], value=9000, program="set")
    plans.append(extra)
    world.inject("client", "offer", extra, at=extra["at"])
    world.fault(before + 1, "power_loss", host="h0")
    world.fault(before + 2, "power_on", host="h0")
    world.run(until=before)
    pre_complete = sum(e["kind"] == "traffic_response" for e in world.trace)
    resident_before = world.hosts["h0"].used
    logical_before = world.actors["shard0"].actor.logical_state()
    until = before + 3_000_000
    world.run(until=until)
    responses = {e["op"]: e["time"] for e in world.trace if e["kind"] == "traffic_response"}
    refused = {e["op"] for e in world.trace if e["kind"] == "traffic_refused"}
    recovered = [e for e in world.trace if e["kind"] == "traffic_recovered"
                 and e["actor"] == "shard0" and e["incarnation"] == 2]
    violations = audit(world, plans, config)
    restored_hashes = [e["logical_hash"] for e in world.trace if e["kind"] == "stream_recovered_state"
                       and e["actor"] == "shard0" and e["incarnation"] == 2]
    state_matches = restored_hashes[0] == digest(logical_before) if restored_hashes else None
    if state_matches is False:
        violations.append("recovered ordering state differs from pre-reset state")
    # Observer-only comparison: the reconstructed old prefix must still exist.
    shard = world.actors["shard0"].actor
    if recovered:
        for key, versions in logical_before["versions"].items():
            if any(tuple(v) not in shard.versions[key] for v in versions):
                violations.append(f"reopening discarded old version {key}")
    row = dict(mode=mode, seed=seed, memory_bytes=memory_bytes,
               offered=len(plans), completed=len(responses), refused=len(refused),
               unfinished=len(plans) - len(responses) - len(refused), completed_before_reset=pre_complete,
               resident_before_reset=resident_before,
               shard_recovered=bool(recovered), recovery_ns=recovered[0]["time"]-before-2 if recovered else None,
               recovered_state_matches=state_matches,
               new_request_latency_ns=responses.get(extra["id"], None),
               violations=violations,
               trace_hash=world.report()["trace_hash"], choices_hash=world.report()["choices_hash"])
    if row["new_request_latency_ns"] is not None:
        row["new_request_latency_ns"] -= extra["at"]
    return row, world


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    names = ("recovery_footprint.py", "traffic.py", "kernel.py", "sim.py")
    sources = {name: hashlib.sha256((here / name).read_bytes()).hexdigest() for name in names}
    rows = [experiment(memory, mode, seed)[0] for memory in (12_000, 16_000, 20_000)
            for mode in ("whole", "stream") for seed in (1, 7, 19)]
    assert all(not row["violations"] for row in rows)
    assert sources == {name: hashlib.sha256((here / name).read_bytes()).hexdigest() for name in names}
    result = dict(sources=sources, rows=rows, boundary="Synthetic device-reset recovery; no reclamation or authority change")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(rows, indent=2))


if __name__ == "__main__":
    main()
