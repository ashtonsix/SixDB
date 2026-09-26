#!/usr/bin/env python3
"""Packet-size/loss stress plus separate exact ideal-code arithmetic.

The network experiment has finite CPU/NIC/reassembly queues, whole-message
retransmission, actual ACK packets and explicit loss/blackout schedules. The
FEC section is an exact probability calculation for an ideal erasure code;
it does not add FEC to Network or emulate a production transport.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from math import ceil, comb
from pathlib import Path

from simulator import Edge, Network, Node, Outcomes, Sim, resource_report


def framing(size, mtu, header=80):
    """Unbatched Network framing: per-transfer identity plus per-packet header."""
    if size < 0 or mtu <= header:
        raise ValueError("nonnegative size and MTU greater than header required")
    payload = size + 16
    packets = ceil(payload / (mtu - header))
    return dict(packets=packets, wire_bytes=payload + packets * header)


def run(config):
    count, size, rate = config["count"], config["size"], config["rate"]
    if count < 1 or size < 0 or rate <= 0:
        raise ValueError("invalid workload")
    sim = Sim(config.get("seed", 11))
    nodes = [Node(name, zone, cpu_slots=config.get("cpu_slots", 2),
                  cpu_packet_us=config.get("cpu_packet_us", .35),
                  nic_bytes_us=config.get("nic_bytes_us", 1250),
                  queue_bytes=config.get("queue_bytes", 262144))
             for name, zone in (("s", "a"), ("r", "b"))]
    edges = {("s", "r"): Edge(latency_us=80, loss=config.get("loss", 0), price_per_gb=.02),
             ("r", "s"): Edge(latency_us=80, loss=config.get("ack_loss", 0), price_per_gb=.02)}
    net = Network(sim, nodes, edges, mtu=config["mtu"], header=80,
                  retry_us=config.get("retry_us", 1000), max_retries=2)
    cutoff = count * 1e6 / rate
    if config.get("periodic_blackout"):
        # Exogenous correlated packet loss on one directed physical edge. No
        # controller sees this schedule; source still learns via ACK/timeout.
        faults = [dict(kind="partition", src="s", dst="r", at_us=t,
                       until_us=t + 400) for t in range(2000, ceil(cutoff), 5000)]
        net.inject(faults)
    out = Outcomes(sim)

    def offer(key):
        out.offer(key)
        net.send("s", "r", size, lambda key=key: out.finish(key))

    for key in range(count):
        sim.at(key * 1e6 / rate, lambda key=key: offer(key))
    sim.run(cutoff)
    cutoff_bytes = sum(r.used for r in sim.resources.values())
    cutoff_reassembly = sum(net.reassembly_bytes.values())
    drain = cutoff + config.get("drain_us", 100000)
    sim.run(drain)
    result = out.summary(cutoff, drain)
    deadline = config.get("deadline_us", 250)
    on_time = sum(k in out.completed and out.completed[k] - t <= deadline
                  for k, t in out.offered.items())
    resources = resource_report(sim)
    result.update(config=config, synthetic=True, deadline_us=deadline,
                  on_time=on_time, late_completed=result["completed"] - on_time,
                  late_or_unfinished=result["offered"] - on_time,
                  objective="first complete usable message at the one receiver",
                  framing=framing(size, config["mtu"]),
                  counters=dict(sim.count), resource_bytes_at_cutoff=cutoff_bytes,
                  reassembly_bytes_at_cutoff=cutoff_reassembly,
                  overflow_events=sum(v for k, v in sim.count.items() if k.startswith("overflow:")),
                  unacked_transfers=sum(not t.acked for t in net.transfers if t.reliable),
                  pending_reassembly=sum(net.reassembly_bytes.values()),
                  cpu_service_us=sum(v["service_us"] for k, v in resources.items() if k.endswith(":cpu")),
                  tx_service_us=sum(v["service_us"] for k, v in resources.items() if k.endswith(":tx")),
                  resource_peak_bytes=max(v["high_bytes"] for v in resources.values()),
                  resource_outstanding_bytes=sum(v["outstanding_bytes"] for v in resources.values()),
                  resources=resources)
    return result


def loss_distribution(n, p, *, stay_bad=None, common_cut=False):
    """Exact number-of-erased-packets PMF, not packet simulation.

    IID Bernoulli(p), stationary two-state Markov loss, or one whole-block cut.
    In Markov mode, stay_bad=P(next lost | current lost), and good->bad is
    chosen so the stationary packet loss is p. The first packet is stationary.
    """
    if n < 1 or not 0 <= p <= 1:
        raise ValueError("invalid block/loss parameters")
    if common_cut:
        return [1 - p] + [0.0] * (n - 1) + [p]
    if stay_bad is None:
        return [comb(n, k) * p ** k * (1 - p) ** (n - k) for k in range(n + 1)]
    if not 0 < p < 1 or not 0 <= stay_bad <= 1:
        raise ValueError("Markov mode requires 0<p<1 and valid stay_bad")
    enter_bad = p * (1 - stay_bad) / (1 - p)
    if enter_bad > 1:
        raise ValueError("infeasible stationary chain")
    states = {(0, 0): 1 - p, (1, 1): p}
    for _ in range(n - 1):
        next_states = {}
        for (lost, bad), probability in states.items():
            q = stay_bad if bad else enter_bad
            for new_bad, transition in ((0, 1 - q), (1, q)):
                key = lost + new_bad, new_bad
                next_states[key] = next_states.get(key, 0) + probability * transition
        states = next_states
    result = [0.0] * (n + 1)
    for (lost, _), probability in states.items():
        result[lost] += probability
    return result


def ideal_block(k=8, parity=0, p=.05, mode="iid", attempts=3):
    """Ideal (k+parity,k) MDS code; no FEC algorithm or repair feedback claim.

    At most parity erasures allow perfect decoding. Trials between attempts are
    independent, even for Markov-within-block and common-cut modes. Actual
    persistent outage violates that reset; its failure probability can remain p.
    Every attempt sends its whole block; there is no packet-selective repair.
    """
    if k < 1 or parity < 0 or attempts < 1 or mode not in ("iid", "bursty", "common_cut"):
        raise ValueError("invalid code/policy")
    n = k + parity
    pmf = loss_distribution(n, p, stay_bad=.8 if mode == "bursty" else None,
                            common_cut=mode == "common_cut")
    q = sum(pmf[:parity + 1])
    expected_attempts = sum((1 - q) ** i for i in range(attempts))
    # Separate illustrative timing/cost calculation. Assumes a complete block
    # is needed, no queueing, 1400-byte coded packets and 10 Gbit/s serialization.
    # Codes pay 5us encode and 8us decode even with zero erasures: deliberately
    # visible costs, not a benchmark. Progress feedback is perfect before the
    # next 1000us retry timer; this does NOT model the actual ACK-loss experiment.
    wire = n * 1400
    encode, decode = (5, 8) if parity else (0, 0)
    first_arrival = 80 + wire / 1250 + encode + decode
    return dict(mode=mode, k=k, parity=parity, packet_loss=p, attempts=attempts,
                success_first_attempt=q, success_by_last_attempt=1 - (1 - q) ** attempts,
                expected_attempts=expected_attempts, expected_wire_bytes=wire * expected_attempts,
                expected_cpu_extra_us=encode * expected_attempts + decode * (1 - (1 - q) ** attempts),
                no_queue_first_arrival_us=first_arrival,
                deadline_110_fraction=q if first_arrival <= 110 else 0,
                packet_loss_pmf=pmf)


def analytic():
    coding = [ideal_block(parity=r, p=p, mode=mode)
              for p in (0, .01, .05, .2)
              for mode in ("iid", "bursty", "common_cut")
              for r in (0, 1, 2, 4)
              if not (mode == "bursty" and p == 0)]
    # Exact load counterexample: under no loss, parity alone drives service
    # demand beyond capacity. This is not an infinite-buffer latency simulation.
    capacity, utilization, k, parity, queue = 1250, .85, 8, 2, 262144
    offered = capacity * utilization
    coded = offered * (k + parity) / k
    overload = dict(capacity_bytes_us=capacity, uncoded_offered_bytes_us=offered,
                    coded_offered_bytes_us=coded, k=k, parity=parity,
                    uncoded_utilization=utilization, coded_utilization=coded / capacity,
                    extra_queue_bytes=queue, fill_time_us=queue / (coded - capacity),
                    assumption="all packets charged equally; no loss, feedback, pacing, or new capacity")
    boundaries = [dict(size=size, mtu=mtu, **framing(size, mtu))
                  for mtu in (512, 1400, 9000) for size in (64, 1304, 1305, 4096, 65536)]
    return dict(ideal_codes=coding, parity_congestion_counterexample=overload,
                framing_boundaries=boundaries)


def campaign(count=400):
    profiles = (
        dict(name="tiny_quiet", size=64, rate=2000),
        dict(name="tiny_packet_cpu", size=64, rate=400000, cpu_packet_us=4, cpu_slots=1),
        dict(name="medium_iid_loss", size=4096, rate=2000, loss=.02),
        dict(name="large_quiet", size=65536, rate=500),
        dict(name="large_packet_cpu", size=65536, rate=8000, cpu_packet_us=4, deadline_us=500),
        dict(name="large_iid_loss", size=65536, rate=2000, loss=.02),
        dict(name="large_periodic_blackout", size=65536, rate=2000, periodic_blackout=True),
        dict(name="large_ack_loss", size=65536, rate=2000, ack_loss=.5),
    )
    return [run(dict(profile, count=count, mtu=mtu, seed=11))
            for profile in profiles for mtu in (512, 1400, 9000)]


def sources():
    root = Path(__file__).resolve().parent
    return {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
            for name in ("transport_study.py", "simulator.py")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--count", type=int, default=400)
    args = parser.parse_args()
    before = sources()
    results = campaign(args.count)
    maths = analytic()
    after = sources()
    if before != after:
        raise RuntimeError("source changed during study")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    payload = dict(synthetic=True, sources=before, source_hashes_stable=True,
                   comparisons=results, analytic=maths)
    args.output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    print(json.dumps(dict(output=str(args.output), comparisons=len(results),
                          analytic_codes=len(maths["ideal_codes"]), sources=before)))


if __name__ == "__main__":
    main()
