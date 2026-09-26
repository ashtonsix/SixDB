#!/usr/bin/env python3
"""Authored directed-tariff broadcasts and analytic economic boundaries.

No rates describe a named provider. Network tariffs are marginal byte charges;
fixed/setup/request/compute charges are separate economic comparisons below.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from simulator import Edge, Network, Node, Outcomes, Sim, resource_report


REGIONS = ("a0", "a1", "b0", "b1")
RECIPIENTS = tuple(region + suffix for region in REGIONS for suffix in ("x", "y"))


def tariff(src, dst):
    """Synthetic directed price units/decimal GB, including any receiver levy.

    Zero here means zero marginal metered transfer, not free infrastructure.
    """
    if src == dst or src[:2] == dst[:2]:
        return 0.0
    if src == "edge":
        return .004
    if src == "cheap":
        return .01
    if src[0] == dst[0]:
        return .02
    return .09


def route(origin, policy):
    if policy == "direct":
        return {origin: [r for r in RECIPIENTS if r != origin]}
    hub = {"regional": origin, "edge_relay": "edge", "cheap_relay": "cheap"}[policy]
    plan = {}
    if origin != hub:
        plan[origin] = [hub]
    # The origin already holds its local region's content; avoid a round trip.
    for region in REGIONS:
        first, second = region + "x", region + "y"
        if origin == first:
            plan.setdefault(origin, []).append(second)
        else:
            plan.setdefault(hub, []).append(first)
            plan.setdefault(first, []).append(second)
    return plan


def run(config):
    origin, policy = config["origin"], config["policy"]
    size, count, rate = config.get("size", 16384), config.get("count", 100), config.get("rate", 200)
    if origin not in ("edge", "a0x") or count < 1 or size < 0 or rate <= 0:
        raise ValueError("invalid origin or workload")
    sim = Sim(config.get("seed", 1))
    names = ("edge", "cheap") + RECIPIENTS
    nodes = [Node(n, n[:2], nic_bytes_us=(config.get("edge_bytes_us", 1250) if n == "edge" else 1250),
                  queue_bytes=config.get("queue_bytes", 1048576)) for n in names]
    edges = {(a, b): Edge(latency_us=(8 if a[:2] == b[:2] else
                                     4000 if a[0] == b[0] else 10000),
                          price_per_gb=tariff(a, b)) for a in names for b in names if a != b}
    net = Network(sim, nodes, edges, retry_us=50000, max_retries=2)
    net.inject(config.get("faults", []))
    out = Outcomes(sim)
    plan = route(origin, policy)
    delivered, hashes, responses = {}, {}, {}
    verify = config.get("direct_hashes", False)
    response_size = config.get("response_size", 0)

    def offer(key):
        out.offer(key)
        delivered[key] = {origin} if origin in RECIPIENTS else set()
        hashes[key] = {origin} if origin in RECIPIENTS else set()
        responses[key] = set()
        seen = {origin}
        hash_arrived, verifying = set(), set()
        effect_recorded = False

        def finish():
            nonlocal effect_recorded
            if len(delivered[key]) != len(RECIPIENTS):
                return
            if verify and len(hashes[key]) != len(RECIPIENTS):
                return
            if not effect_recorded:
                effect_recorded = True
                out.mark(key, "all_payload_and_required_hashes")
            if not response_size or len(responses[key]) == len(RECIPIENTS):
                out.finish(key)

        def response_from(receiver):
            # Response to the edge may be sent by a different host from ingress.
            def receive():
                responses[key].add(receiver)
                finish()
            net.send(receiver, "edge", response_size, receive)

        def verify_when_ready(receiver):
            if receiver not in delivered[key] or receiver not in hash_arrived or receiver in verifying:
                return
            verifying.add(receiver)
            def verified():
                hashes[key].add(receiver)
                finish()
            net.compute(receiver, size * .0001, max(1, size), verified, "control")

        def forward(src):
            for dst in plan.get(src, ()):
                def receive(dst=dst):
                    if dst in seen:
                        return
                    seen.add(dst)
                    if dst in RECIPIENTS:
                        delivered[key].add(dst)
                        if verify:
                            verify_when_ready(dst)
                        if response_size:
                            response_from(dst)
                    forward(dst)
                    finish()
                net.send(src, dst, size, receive)

        if response_size and origin in RECIPIENTS:
            response_from(origin)
        def send_hashes():
            for receiver in RECIPIENTS:
                if receiver == origin:
                    continue
                def receive_hash(receiver=receiver):
                    hash_arrived.add(receiver)
                    verify_when_ready(receiver)
                # Hash production and comparison are charged CPU, with semantic
                # correctness supplied by the fixture. No authorization proof.
                net.send(origin, receiver, 32, receive_hash, "control")
        if verify:
            net.compute(origin, size * .0001, max(1, size), send_hashes, "control")
        forward(origin)
        finish()

    for key in range(count):
        sim.at(key * 1e6 / rate, lambda key=key: offer(key))
    cutoff = count * 1e6 / rate
    drain = cutoff + config.get("drain_us", 500000)
    sim.run(cutoff)
    pending_bytes = sum(r.used for r in sim.resources.values())
    sim.run(drain)
    result = out.summary(cutoff, drain)
    result.update(config=config, synthetic=True, counters=dict(sim.count),
                  completed_recipient_pairs=sum(map(len, delivered.values())),
                  required_recipient_pairs=count * len(RECIPIENTS),
                  queue_bytes_at_cutoff=pending_bytes, resources=resource_report(sim),
                  plan=plan, edge_tariffs={a+"->"+b: tariff(a, b) for a in names for b in names if a != b},
                  objective="all eight usable payloads; optional required direct hashes and responses to edge")
    return result


def interconnect_break_even(fixed, public_per_gb, private_per_gb):
    """Volume over the same billing horizon; infinity when no variable saving."""
    return fixed / (public_per_gb - private_per_gb) if public_per_gb > private_per_gb else None


def retrieval(size, hit, blob_request, blob_gb, peer_request, peer_gb):
    """Peer miss falls back to blob; lookup paid on both hits and misses."""
    blob = blob_request + size / 1e9 * blob_gb
    peer = peer_request + hit * size / 1e9 * peer_gb + (1 - hit) * blob
    return dict(blob=blob, peer_then_blob=peer, saved=blob-peer)


def economics():
    return dict(
        unit="illustrative cost units; no named provider rates",
        interconnect=[dict(volume_gb=v, public=v*.09, private=200+v*.02)
                      for v in (100, 1000, 3000, 10000)],
        interconnect_break_even_gb=interconnect_break_even(200, .09, .02),
        peer_retrieval=[dict(size=s, hit=h, **retrieval(s, h, .0000004, 0, .00000002, .01))
                        for s in (128, 4096, 1048576) for h in (0, .1, .9, 1)],
        nat_bypass=[dict(gb=v, baseline=.04*v, direct=.005*v+10,
                        assumption="same horizon; 10 fixed broker/connection cost, extra capacity/auth cost not measured")
                    for v in (10, 100, 1000)],
        caveat="These are cost-only equal-obligation comparisons, excluding latency, residency retention, CPU, cold setup and failure capacity unless included explicitly.")


def campaign(count=100):
    configs = []
    for origin in ("edge", "a0x"):
        for size in (64, 16384):
            for policy in ("direct", "regional", "edge_relay", "cheap_relay"):
                configs.append(dict(name=f"{origin}-{size}-{policy}", origin=origin, size=size,
                                    policy=policy, count=count))
        for variant in ("hashes", "small_responses", "large_responses", "tight_buffers", "edge_slow", "edge_crash"):
            for policy in ("direct", "edge_relay"):
                c = dict(name=f"{origin}-{variant}-{policy}", origin=origin,
                         size=16384, policy=policy, count=count)
                if variant == "hashes":
                    c["direct_hashes"] = True
                elif variant in ("large_responses", "tight_buffers"):
                    c["response_size"] = 65536
                    if variant == "tight_buffers":
                        c["queue_bytes"] = 262144
                elif variant == "small_responses":
                    c["response_size"] = 32
                elif variant == "edge_slow":
                    c.update(edge_bytes_us=10, rate=2000, queue_bytes=262144)
                else:
                    c["faults"] = [dict(kind="crash", node="edge", at_us=50000, until_us=150000)]
                configs.append(c)
    return configs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--count", type=int, default=100)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).parent
    def hashes():
        return {n: hashlib.sha256((root/n).read_bytes()).hexdigest()
                for n in ("edge_study.py", "simulator.py")}
    sources = hashes()
    results = [run(c) for c in campaign(args.count)]
    if sources != hashes():
        raise RuntimeError("source changed during study")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(sources=sources, economics=economics(), results=results), indent=2)+"\n")
    print(f"{len(results)} edge comparisons and economic boundaries -> {args.output}")


if __name__ == "__main__":
    main()
