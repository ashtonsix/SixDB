#!/usr/bin/env python3
"""Mixed physical-resource experiment; the foreground is not a consensus model.

Foreground endpoint: local + remote payload persistence, notification, local
effect completion; separately, a different client receives its response.
Background endpoint: sink reduces every chunk of one supplied immutable cut.
All service times, rates and placements are authored simulation inputs.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path

from simulator import Edge, Network, Node, Outcomes, Resource, Sim, resource_report


class AttributedTx(Resource):
    """Attribute completed TX bytes in this fault-free, unbatched workload.

    Bulk output and every sink packet (its ACKs) belong to the read class.
    All other traffic belongs to the foreground. This is workload attribution,
    not an addition to the core simulator's transport protocol.
    """
    def __init__(self, sim, node, totals):
        super().__init__(sim, node.name + ":tx", capacity=node.queue_bytes,
                         reserve=node.reserve_bytes)
        self.node, self.totals = node.name, totals

    def submit(self, duration, charge, done, kind="data", reject=None, begin=None):
        category = "background" if kind == "bulk" or self.node == "sink" else "foreground"
        def sent():
            self.totals[category] += charge
            done()
        return super().submit(duration, charge, sent, kind, reject, begin)


def run_mixed(config):
    sim = Sim(config.get("seed", 1))
    count, rate = config.get("count", 600), config.get("rate", 20000)
    cutoff = count * 1e6 / rate
    drain = cutoff + config.get("drain_us", 20000)
    placement = config.get("placement", "shared")
    if placement not in {"shared", "cores", "host"}:
        raise ValueError("unknown background placement")
    queue_bytes = config.get("queue_bytes", 1 << 20)
    reserve = config.get("reserve_bytes", 0)
    names = {"writer": "a", "durable": "b", "effect": "a", "client": "a", "sink": "b"}
    if placement == "host":
        names["background"] = "a"
    background = "background" if placement == "host" else "effect"
    nodes = [Node(name, zone, cpu_slots=(2 if placement == "shared" else 1)
                  if name == "effect" else 1 if name == "background" else 2,
                  queue_bytes=queue_bytes, reserve_bytes=reserve,
                  nic_bytes_us=config.get("nic_bytes_us", 1250),
                  persist_us=10, persist_bytes_us=2000)
             for name, zone in names.items()]
    edges = {(a, b): Edge(8 if names[a] == names[b] else 30)
             for a in names for b in names if a != b}
    net = Network(sim, nodes, edges, retry_us=config.get("retry_us", 400), max_retries=2)
    wire_by_class = Counter()
    for node in nodes:
        net.tx[node.name] = AttributedTx(sim, node, wire_by_class)
    core_pool = (Resource(sim, "effect:background_core", slots=1,
                          capacity=queue_bytes, reserve=reserve)
                 if placement == "cores" else None)
    effect, response, reads = (Outcomes(sim) for _ in range(3))
    counts = Counter()
    held = {"foreground": 0, "background": 0}
    high = {"foreground": 0, "background": 0}
    foreground_credit = config.get("foreground_credit_bytes", 1 << 18)
    background_credit = config.get("background_credit_bytes", 1 << 25)
    input_bytes = config.get("read_bytes", 131072)
    chunk_bytes = config.get("chunk_bytes", 131072)
    output_ratio = config.get("output_ratio", 1 / 32)
    copies = config.get("compute_copies", 1)
    amplification = config.get("compute_us_per_byte", .006) * copies
    if min(count, rate, input_bytes, chunk_bytes, copies) <= 0:
        raise ValueError("positive workload sizes required")
    cancellation = []

    def acquire(kind, charge, cap):
        if held[kind] + charge > cap:
            counts[kind + "_refused"] += 1
            return False
        held[kind] += charge
        high[kind] = max(high[kind], held[kind])
        counts[kind + "_admitted"] += 1
        return True

    def foreground_offer(key):
        effect.offer(key)
        response.offer(key)
        size = config.get("write_bytes", 512)
        charge = size + 64
        if not acquire("foreground", charge, foreground_credit):
            return

        def apply_effect():
            def done():
                effect.finish(key)
                def answered():
                    response.finish(key)
                    held["foreground"] -= charge
                net.send("effect", "client", 64, answered, "control")
            duration = config.get("effect_us", 2)
            if net.compute("effect", duration, size, done, "control"):
                counts["foreground_submitted_application_cpu_us"] += duration

        def replicated():
            net.persist("durable", size,
                        lambda: net.send("durable", "effect", 64, apply_effect, "control"),
                        "control")

        net.persist("writer", size,
                    lambda: net.send("writer", "durable", size, replicated, "control"),
                    "control")

    def background_offer(key):
        reads.offer(key)
        pieces = math.ceil(input_bytes / chunk_bytes)
        # Conservative application reservation covers one input and its maximum
        # output. Transport buffers remain held until ACK or bounded give-up.
        total_output = sum(math.ceil(min(chunk_bytes, input_bytes - p * chunk_bytes) * output_ratio)
                           for p in range(pieces))
        charge = input_bytes + total_output
        if not acquire("background", charge, background_credit):
            return
        state = {"next": 0, "active": 0, "covered": set(), "cancelled": False,
                 "failed": False, "released": False, "transfers": [], "record": None}

        def release_if_idle():
            if (state["active"] == 0 and not state["released"] and
                    (state["next"] == pieces or state["cancelled"] or state["failed"])):
                held["background"] -= charge
                state["released"] = True
                if state["record"] is not None:
                    state["record"]["retired_us"] = sim.now

        def retire_chunk():
            state["active"] -= 1
            launch()
            release_if_idle()

        def fail_chunk():
            if not state["failed"]:
                counts["background_failed"] += 1
            state["failed"] = True
            retire_chunk()

        def observe_ack(transfer):
            age = sim.now - transfer.born
            give_up = net.retry_us * (2 ** (net.max_retries + 1) - 1)
            if transfer.acked:
                retire_chunk()
            elif age >= give_up:
                counts["background_transport_give_up"] += 1
                fail_chunk()
            else:
                sim.later(20, lambda: observe_ack(transfer))

        def compute_piece(piece, size, result_size):
            if state["cancelled"]:
                retire_chunk()
                return
            counts["background_scan_bytes"] += size
            duration = size * amplification + config.get("chunk_setup_us", .5)

            def computed():
                if state["cancelled"]:
                    counts["background_computed_after_cancel_bytes"] += size * copies
                    retire_chunk()
                    return
                counts["background_output_offered_bytes"] += result_size

                def received():
                    if state["cancelled"]:
                        return
                    def reduced():
                        if state["cancelled"]:
                            return
                        state["covered"].add(piece)
                        if len(state["covered"]) == pieces:
                            reads.finish(key)
                    if net.compute("sink", .25, result_size, reduced, "bulk"):
                        counts["background_submitted_reduce_us"] += .25

                transfer = net.send(background, "sink", result_size, received, "bulk")
                state["transfers"].append(transfer)
                sim.later(20, lambda: observe_ack(transfer))

            if core_pool is None:
                accepted = net.compute(background, duration, size, computed, "bulk", fail_chunk)
            else:
                accepted = core_pool.submit(duration, size, computed, "bulk", fail_chunk)
            if accepted:
                counts["background_compute_input_bytes"] += size * copies
                counts["background_submitted_compute_us"] += duration

        def launch():
            window = config.get("read_window", 2)
            while (not state["cancelled"] and not state["failed"] and
                   state["active"] < window and state["next"] < pieces):
                piece = state["next"]
                state["next"] += 1
                state["active"] += 1
                size = min(chunk_bytes, input_bytes - piece * chunk_bytes)
                result_size = math.ceil(size * output_ratio)
                duration = size / net.nodes[background].memory_bytes_us
                accepted = net.mem[background].submit(
                    duration, size,
                    lambda p=piece, b=size, r=result_size: compute_piece(p, b, r),
                    "bulk", fail_chunk)
                if accepted:
                    counts["background_memory_offered_bytes"] += size

        if "cancel_after_us" in config:
            def cancel():
                if key in reads.completed or state["released"]:
                    return
                state["cancelled"] = True
                counts["background_cancelled"] += 1
                record = {"query": key, "cancel_us": sim.now, "active_chunks": state["active"],
                          "reserved_query_bytes": charge, "retired_us": None}
                state["record"] = record
                if len(cancellation) < 8:
                    cancellation.append(record)
                release_if_idle()
            sim.later(config["cancel_after_us"], cancel)
        launch()

    for key in range(count):
        sim.at(key * 1e6 / rate, lambda key=key: foreground_offer(key))
    background_rate = config.get("background_rate", 1000)
    background_count = math.floor(cutoff * background_rate / 1e6)
    burst = config.get("background_burst", 2)
    for key in range(background_count):
        sim.at((key // burst) * burst * 1e6 / background_rate,
               lambda key=key: background_offer(key))
    sim.run(cutoff)
    cutoff_resources = {name: resource.used for name, resource in sim.resources.items()}
    cutoff_held = dict(held)
    sim.run(drain)

    def summary(outcomes, kind):
        result = outcomes.summary(cutoff, drain)
        # The durable-notification proxy is not the brief's write path. Do not
        # inherit that separate experiment's deadline labels into this study.
        result.pop("deadline_170_fraction")
        result.pop("deadline_250_fraction")
        result.update(admitted=counts[kind + "_admitted"], refused=counts[kind + "_refused"])
        deadline = config.get("foreground_deadline_us", 150) if kind == "foreground" else config.get("read_deadline_us", 2000)
        result["deadline_us"] = deadline
        result["deadline_fraction"] = sum(
            key in outcomes.completed and outcomes.completed[key] - born <= deadline
            for key, born in outcomes.offered.items()) / max(1, len(outcomes.offered))
        return result

    return dict(config=config, model="authored mixed resource proxy; no witness admission",
                foreground_effect=summary(effect, "foreground"),
                client_response=summary(response, "foreground"),
                background_read=summary(reads, "background"),
                work=dict(counts), counters=dict(sim.count), resources=resource_report(sim),
                application_bytes_at_cutoff=cutoff_held, application_bytes_at_drain=held,
                application_high_bytes=high, resource_bytes_at_cutoff=cutoff_resources,
                cancellation=cancellation, logical_background_bytes=background_count * input_bytes,
                completed_tx_wire_bytes_by_class=dict(wire_by_class),
                cpu_budget="two effect/background CPU slots total; other hosts fixed",
                placement=placement)


def campaign(count=600):
    base = dict(count=count)
    cases = [
        dict(name="foreground-only", background_rate=0),
        dict(name="shared-large"),
        dict(name="shared-small", chunk_bytes=8192),
        dict(name="split-cores-large", placement="cores"),
        dict(name="shared-large-reserved", reserve_bytes=65536),
        dict(name="shared-recompute-2x", compute_copies=2),
        dict(name="shared-shuffle-16x", chunk_bytes=8192, output_ratio=16),
        dict(name="shuffle-cores", placement="cores", chunk_bytes=8192, output_ratio=16),
        dict(name="shuffle-host", placement="host", chunk_bytes=8192, output_ratio=16),
        dict(name="bounded-admission", chunk_bytes=8192, compute_copies=2,
             background_credit_bytes=2 * (131072 + 4096)),
        dict(name="cancel-large", cancel_after_us=50),
        dict(name="cancel-small", chunk_bytes=8192, cancel_after_us=50),
    ]
    return [run_mixed(dict(base, **case)) for case in cases]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--count", type=int, default=600)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    here = Path(__file__).parent
    def hashes():
        return {name: hashlib.sha256((here / name).read_bytes()).hexdigest()
                for name in ("mixed_study.py", "simulator.py", "check_mixed.py")}
    sources = hashes()
    results = campaign(args.count)
    if hashes() != sources:
        raise RuntimeError("mixed study sources changed during execution; rerun from stable bytes")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(sources=sources, results=results), indent=2) + "\n")
    print(f"{len(results)} mixed cases -> {args.output}")


if __name__ == "__main__":
    main()
