#!/usr/bin/env python3
"""Dynamic snapshot/tail joins sharing the dissemination simulator resources.

Authority, snapshot validity and survival of completed persistent writes are
assumed. Network callbacks model delivery; only returned application durable
ACKs advance source retention. This is not a witness-admission simulation.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import hashlib
import json
import math
from pathlib import Path

from delivery import RetainedLog
from experiments import network
from simulator import Outcomes, Sim, resource_report


@dataclass
class Receiver:
    barrier: int
    prefix: int
    installed: bool
    pending: set[int] = field(default_factory=set)
    chunks: set[int] = field(default_factory=set)
    installing_incarnation: int = -1
    installing_since: float = -math.inf

    def accept(self, sequence):
        if sequence > self.prefix:
            self.pending.add(sequence)
            while self.prefix + 1 in self.pending:
                self.prefix += 1
                self.pending.remove(self.prefix)


def run_membership(config):
    sim = Sim(config.get("seed", 1))
    count, rate = int(config.get("count", 200)), float(config.get("rate", 50000))
    size = int(config.get("size", 256))
    join_us = float(config.get("join_us", 300))
    snapshot_bytes = int(config.get("snapshot_bytes", 256 << 10))
    chunk_bytes = int(config.get("snapshot_chunk_bytes", 4096))
    poll_us = float(config.get("repair_poll_us", 100))
    app_retry_us = float(config.get("application_retry_us", 1200))
    max_attempts = int(config.get("application_max_attempts", 12))
    snapshot_window = int(config.get("snapshot_window", 4))
    repair_window = int(config.get("repair_window", 8))
    capacity = int(config.get("retention_events", 256))
    if (min(count, rate, size, chunk_bytes, poll_us, app_retry_us, max_attempts,
            snapshot_window, repair_window, capacity) <= 0
            or snapshot_bytes < 0 or join_us < 0):
        raise ValueError("positive workload/queue limits and nonnegative join/snapshot required")
    cutoff = max(count * 1e6 / rate, join_us)
    drain = cutoff + float(config.get("drain_us", 15000))
    if drain < cutoff:
        raise ValueError("negative drain")
    net = network(sim, {"p": "a", "c": "a", "j": config.get("join_zone", "b")}, config)
    log = RetainedLog(capacity)
    log.capture_join("c", 0)
    log.install_snapshot("c", 0)
    receivers = {"c": Receiver(0, 0, True)}
    outcome = Outcomes(sim)
    chunks = math.ceil(snapshot_bytes / chunk_bytes)
    chunk_acked = set()
    allocated = reserved = refused = 0
    ready_source = set()
    offer_by_sequence = {}
    attempts, last_sent = {}, {}
    highs = {"retained_events": 0, "reserved_events": 0, "source_credit_events": 0,
             "join_pending_events": 0}
    first_ready_us = None
    joined_barrier = None
    state_at_cutoff = None

    def sample():
        nonlocal first_ready_us
        highs["retained_events"] = max(highs["retained_events"], len(log.events))
        highs["reserved_events"] = max(highs["reserved_events"], reserved)
        highs["source_credit_events"] = max(highs["source_credit_events"],
                                             len(log.events) + reserved)
        if "j" in log.subscriptions:
            sub = log.subscriptions["j"]
            highs["join_pending_events"] = max(highs["join_pending_events"],
                                                log.latest - sub.accepted_through)
            if first_ready_us is None and log.ready("j", log.latest):
                first_ready_us = sim.now

    def application_ack(recipient, sequence):
        def returned():
            log.receive_tail(recipient, sequence)
            log.trim()
            sample()
        net.send(recipient, "p", 32, returned, "control")

    def tail_arrived(recipient, sequence):
        receiver = receivers[recipient]
        if sequence <= receiver.prefix or sequence in receiver.pending:
            application_ack(recipient, sequence)
            return

        def persisted():
            receiver.accept(sequence)
            if recipient == "c":
                # This measured boundary is durable recipient delivery, not admission.
                outcome.finish(offer_by_sequence[sequence])
            application_ack(recipient, sequence)
        net.persist(recipient, size, persisted, "data" if recipient == "c" else "repair")

    def transmit_tail(recipient, sequence, kind):
        key = ("tail", recipient, sequence)
        attempts[key] = attempts.get(key, 0) + 1
        last_sent[key] = sim.now
        sim.count["application_tail_attempts"] += 1
        sim.count["application_" + kind + "_payload_bytes"] += size
        if attempts[key] > 1:
            sim.count["application_retries"] += 1
        net.send("p", recipient, size,
                 lambda: tail_arrived(recipient, sequence), kind)

    def snapshot_installed_ack():
        def returned():
            log.install_snapshot("j", joined_barrier)
            log.trim()
            sample()
        net.send("j", "p", 48, returned, "control")

    def attempt_install():
        receiver = receivers["j"]
        if receiver.installed:
            snapshot_installed_ack()
            return
        if len(receiver.chunks) != chunks:
            return
        incarnation = net.nodes["j"].incarnation
        if (receiver.installing_incarnation == incarnation
                and sim.now - receiver.installing_since < app_retry_us):
            return
        receiver.installing_incarnation = incarnation
        receiver.installing_since = sim.now

        def computed():
            def persisted():
                receiver.installed = True
                snapshot_installed_ack()
            net.persist("j", 64, persisted, "repair")
        net.compute("j", float(config.get("snapshot_install_us", 20)),
                    min(max(1, snapshot_bytes), chunk_bytes), computed, "repair")

    def transmit_chunk(index):
        key = ("snapshot", index)
        attempts[key] = attempts.get(key, 0) + 1
        last_sent[key] = sim.now
        amount = min(chunk_bytes, snapshot_bytes - index * chunk_bytes)
        sim.count["application_snapshot_payload_bytes"] += amount
        if attempts[key] > 1:
            sim.count["application_retries"] += 1

        def arrived():
            def ack():
                net.send("j", "p", 40, lambda: chunk_acked.add(index), "control")
                attempt_install()
            if index in receivers["j"].chunks:
                ack()
                return
            def persisted():
                receivers["j"].chunks.add(index)
                ack()
            net.persist("j", amount, persisted, "bulk")
        net.send("p", "j", amount, arrived, "bulk")

    def available(key):
        return (attempts.get(key, 0) < max_attempts and
                sim.now - last_sent.get(key, -math.inf) >= app_retry_us)

    def outstanding(key):
        return sim.now - last_sent.get(key, -math.inf) < app_retry_us

    def repair_tick():
        if not net.nodes["p"].down:
            # Source uses only missing application ACKs and its timers; it never
            # consults receiver state, loss events, transfer.acked or queue contents.
            for recipient in tuple(log.subscriptions):
                missing = log.missing(recipient)
                active = sum(outstanding(("tail", recipient, n)) for n in missing)
                for sequence in missing:
                    key = ("tail", recipient, sequence)
                    if active >= repair_window:
                        break
                    if available(key):
                        transmit_tail(recipient, sequence, "repair")
                        active += 1
            if "j" in log.subscriptions and not log.subscriptions["j"].snapshot_installed:
                missing_chunks = [n for n in range(chunks) if n not in chunk_acked]
                active = sum(outstanding(("snapshot", n)) for n in missing_chunks)
                for index in missing_chunks:
                    if active >= snapshot_window:
                        break
                    if available(("snapshot", index)):
                        transmit_chunk(index)
                        active += 1
                # Chunk receipt and installed-snapshot confirmation are distinct.
                key = ("install-status",)
                if len(chunk_acked) == chunks and available(key):
                    attempts[key] = attempts.get(key, 0) + 1
                    last_sent[key] = sim.now
                    net.send("p", "j", 32, attempt_install, "control")
        sample()
        if sim.now + poll_us <= drain:
            sim.later(poll_us, repair_tick)

    def source_persisted(sequence):
        nonlocal reserved
        ready_source.add(sequence)
        # The source exposes its own contiguous durable stream, even if write
        # completions reorder. A failed reserved write remains visibly blocking.
        while log.latest + 1 in ready_source:
            sequence = log.latest + 1
            ready_source.remove(sequence)
            reserved -= 1
            assert log.append() == sequence
            transmit_tail("c", sequence, "data")  # Frozen old route never adds j.
        sample()

    def offer(index):
        nonlocal allocated, reserved, refused
        outcome.offer(index)
        if len(log.events) + reserved >= capacity:
            refused += 1
            sim.count["retention_refused_offers"] += 1
            return
        allocated += 1
        reserved += 1
        offer_by_sequence[allocated] = index
        sequence = allocated
        net.persist("p", size, lambda: source_persisted(sequence), "data")
        sample()

    def join():
        nonlocal joined_barrier
        # Authenticated membership + correct pinned snapshot are assumed facts.
        joined_barrier = log.latest
        log.capture_join("j", joined_barrier)
        receivers["j"] = Receiver(joined_barrier, joined_barrier, False)
        sample()

    def snapshot_report():
        sub = log.subscriptions.get("j")
        return dict(published_through=log.latest, retained_events=len(log.events),
                    source_reserved_events=reserved,
                    snapshot_installed_ack=bool(sub and sub.snapshot_installed),
                    join_acked_through=sub.accepted_through if sub else None,
                    join_ready=bool(sub and log.ready("j", log.latest)),
                    join_missing_events=len(log.missing("j")) if sub else None,
                    snapshot_chunks_acked=len(chunk_acked))

    def capture_cutoff():
        nonlocal state_at_cutoff
        state_at_cutoff = snapshot_report()

    for index in range(count):
        sim.at(index * 1e6 / rate, lambda index=index: offer(index))
    sim.at(join_us, join)
    sim.at(0, repair_tick)
    sim.at(cutoff, capture_cutoff)
    sim.run(drain)
    final = snapshot_report()
    unresolved_keys = [("tail", recipient, n) for recipient in log.subscriptions
                       for n in log.missing(recipient)]
    unresolved_keys += [("snapshot", n) for n in range(chunks) if n not in chunk_acked]
    if ("j" in log.subscriptions and not log.subscriptions["j"].snapshot_installed
            and len(chunk_acked) == chunks):
        unresolved_keys.append(("install-status",))
    return dict(kind="dynamic membership over finite network resources; synthetic timings",
                config=config, cutoff_us=cutoff, end_us=drain, old_route=["p", "c"],
                join_barrier=joined_barrier, first_ready_us=first_ready_us,
                accepted_offers=allocated, refused_offers=refused,
                foreground_durable_delivery=outcome.summary(cutoff, drain),
                at_cutoff=state_at_cutoff, final=final, highs=highs,
                retained_payload_high_bytes=highs["retained_events"] * size,
                application_exhausted_obligations=sum(attempts.get(k, 0) >= max_attempts
                                                       for k in unresolved_keys),
                counters=dict(sim.count), resources=resource_report(sim))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, help="JSON configuration including network faults")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    config = json.loads(args.config.read_text()) if args.config else {}
    result = run_membership(config)
    directory = Path(__file__).resolve().parent
    result["source_sha256"] = {name: hashlib.sha256((directory / name).read_bytes()).hexdigest()
                               for name in ("membership_scenario.py", "delivery.py",
                                            "simulator.py", "experiments.py")}
    encoded = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded)
        print(json.dumps({k: result[k] for k in ("join_barrier", "accepted_offers",
              "refused_offers", "at_cutoff", "final", "highs")}, indent=2))
    else:
        print(encoded, end="")


if __name__ == "__main__":
    main()
