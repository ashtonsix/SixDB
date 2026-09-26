#!/usr/bin/env python3
"""Bounded synthetic comparisons of constrained routes and equivalent senders.

Uses the finite CPU/NIC/RX/buffer simulator. No production controller, transport
implementation, dynamic membership, consensus or calibrated latency model.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from math import ceil
from pathlib import Path

from routing import (Edge as RouteEdge, minimum_arborescence, sender_order,
                     tree_family, tree_metrics)
from simulator import Edge, Network, Node, Outcomes, Sim, resource_report


def network(sim, zones, config):
    nodes = [Node(name, zone, cpu_slots=config.get("cpu_slots", 2),
                  cpu_packet_us=config.get("cpu_packet_us", .35),
                  nic_bytes_us=config.get("nic_bytes_us", 1250),
                  queue_bytes=config.get("queue_bytes", 262144))
             for name, zone in zones.items()]
    edges = {(a.name, b.name): Edge(
        latency_us=(config.get("local_us", 8) if a.zone == b.zone else
                    config.get("remote_us", 80)),
        price_per_gb=(0 if a.zone == b.zone else .01),
        jitter_us=config.get("jitter_us", 0))
        for a in nodes for b in nodes if a != b}
    net = Network(sim, nodes, edges, retry_us=config.get("retry_us", 400),
                  max_retries=config.get("max_retries", 2))
    net.inject(config.get("faults", []))
    return net


def route_edges(net, nodes, size):
    payload = size + 16
    wire = payload + ceil(payload / (net.mtu - net.header)) * net.header
    ack_wire = 40 + ceil(40 / (net.mtu - net.header)) * net.header
    return tuple(RouteEdge(a, b,
        wire * (1 + net.edge(a, b).price_per_gb / .01) +
        ack_wire * (1 + net.edge(b, a).price_per_gb / .01),
        net.edge(a, b).latency_us)
        for a in nodes for b in nodes if a != b)


def plan_family(net, recipients, size, policy, *, family_size=4, fanout=4, seed=1):
    """Explicit feasible shallow construction, compared with oblivious noise.

    Shallow plans each cross the AZ boundary once, then delegate to a local BFS
    tree. Depth is at most three for the authored 16-recipient, fanout=4 case.
    No claim of optimality under either constraint is made.
    """
    if not recipients or fanout < 1 or family_size < 1:
        raise ValueError("recipients, fanout and family size must be positive")
    nodes = ("source",) + tuple(recipients)
    edges = route_edges(net, nodes, size)
    by_pair = {(e.src, e.dst): e for e in edges}
    if policy == "direct":
        return (tuple(by_pair["source", r] for r in recipients),)
    if policy == "fixed_cheap":
        return (minimum_arborescence(nodes, edges, "source"),)
    if policy == "noise":
        return tree_family(nodes, edges, "source", count=family_size, seed=seed)
    if policy not in ("shallow_hash", "shallow_stripes", "shallow_block_stripes"):
        raise ValueError("unknown route policy " + policy)
    family = []
    for index in range(min(family_size, len(recipients))):
        # Spaced gateways also rotate the hosts that forward at the next level.
        shift = index * len(recipients) // min(family_size, len(recipients))
        local = tuple(recipients[shift:]) + tuple(recipients[:shift])
        plan = [by_pair["source", local[0]]]
        plan.extend(by_pair[local[(i - 1) // fanout], local[i]]
                    for i in range(1, len(local)))
        family.append(tuple(plan))
    return tuple(family)


def choice(message_id, count, policy, seed=1, stripe_length=32):
    if policy == "shallow_stripes":
        return message_id % count
    if policy == "shallow_block_stripes":
        return (message_id // stripe_length) % count
    digest = hashlib.blake2b(f"{seed}:{message_id}".encode(),
                             digest_size=8, person=b"adapt-routes").digest()
    return int.from_bytes(digest, "big") % count


def resources(sim):
    report = resource_report(sim)
    return dict(
        cpu_service_us=sum(r["service_us"] for n, r in report.items() if n.endswith(":cpu")),
        tx_service_us=sum(r["service_us"] for n, r in report.items() if n.endswith(":tx")),
        rx_service_us=sum(r["service_us"] for n, r in report.items() if n.endswith(":rx")),
        resource_peak_bytes=max((r["high_bytes"] for r in report.values()), default=0),
        resource_outstanding_bytes=sum(r["outstanding_bytes"] for r in report.values()),
        busiest_cpu=max((dict(node=n, service_us=r["service_us"]) for n, r in report.items()
                        if n.endswith(":cpu")), key=lambda r: r["service_us"], default=None),
        most_queued=max((dict(node=n, high_bytes=r["high_bytes"], mean_wait_us=r["mean_wait_us"])
                        for n, r in report.items()), key=lambda r: r["mean_wait_us"], default=None))


def summary(sim, net, out, config, cutoff, drain, cutoff_bytes, deadline):
    result = out.summary(cutoff, drain)
    on_time = sum(key in out.completed and out.completed[key] - born <= deadline
                  for key, born in out.offered.items())
    result.update(config=config, synthetic=True, deadline_us=deadline,
                  on_time=on_time, late_completed=result["completed"] - on_time,
                  late_or_unfinished=result["offered"] - on_time,
                  resource_bytes_at_cutoff=cutoff_bytes,
                  counters=dict(sim.count), **resources(sim),
                  pending_batches=sum(net.pending_bytes.values()),
                  pending_reassembly=sum(net.reassembly_bytes.values()),
                  unacked_transfers=sum(t.reliable and not t.acked for t in net.transfers))
    result["overflow_events"] = sum(value for key, value in sim.count.items()
                                    if key.startswith("overflow:"))
    return result


def run_routes(config):
    count, rate = config.get("count", 600), config.get("rate", 20000)
    size, nr = config.get("size", 256), config.get("receivers", 16)
    if count < 1 or rate <= 0 or size < 0 or nr < 1:
        raise ValueError("positive count/rate/receivers and nonnegative size required")
    policy = config.get("policy", "fixed_cheap")
    recipients = tuple(f"r{i}" for i in range(nr))
    sim = Sim(config.get("seed", 1))
    net = network(sim, dict(source="a", **dict.fromkeys(recipients, "b")), config)
    out = Outcomes(sim)
    family = plan_family(net, recipients, size, policy,
                         family_size=config.get("family_size", 4),
                         fanout=config.get("fanout", 4), seed=config.get("seed", 1))
    routes, metrics = [], []
    for plan in family:
        mapping = {}
        for edge in plan:
            mapping.setdefault(edge.src, []).append(edge.dst)
        routes.append(mapping)
        metric = tree_metrics(("source",) + recipients, plan, {"source": 0})
        metrics.append(dict(depth=metric.max_depth, fanout=metric.max_fanout,
                            propagation_max_us=metric.max_arrival_us,
                            wire_byte_equivalent_cost=metric.total_cost))

    # Install the explicit child lists once. Warm-up has its own accounted work;
    # it is outside per-message latency. Whole-family distribution is not free.
    installed = set()
    route_state_bytes = 0
    for receiver in recipients:
        descriptor = sum(16 + 8 * len(route.get(receiver, ())) for route in routes)
        route_state_bytes += descriptor
        net.send("source", receiver, descriptor,
                 lambda receiver=receiver: installed.add(receiver), "control")
    start = config.get("start_us", 1000)
    sim.run(start)
    if len(installed) != nr:
        raise ValueError("route setup incomplete before warm workload")
    setup_wire = sim.count["wire_bytes"]
    setup_cpu = resources(sim)["cpu_service_us"]
    uses = Counter()
    delivered = {}

    def offer(key):
        out.offer(key)
        delivered[key] = set()
        selected = choice(key, len(routes), policy, config.get("seed", 1),
                          config.get("stripe_length", 32))
        uses[selected] += 1
        route = routes[selected]

        def forward(src):
            for dst in route.get(src, ()):
                def received(dst=dst):
                    if dst in delivered[key]:
                        sim.count["duplicate_recipient_deliveries"] += 1
                        return
                    delivered[key].add(dst)
                    if len(delivered[key]) == nr:
                        out.finish(key)
                    forward(dst)
                net.send(src, dst, size, received)
        forward("source")

    for key in range(count):
        sim.at(start + key * 1e6 / rate, lambda key=key: offer(key))
    cutoff = start + count * 1e6 / rate
    drain = cutoff + config.get("drain_us", 20000)
    sim.run(cutoff)
    cutoff_bytes = sum(r.used for r in sim.resources.values())
    sim.run(drain)
    result = summary(sim, net, out, config, cutoff, drain, cutoff_bytes,
                     config.get("deadline_us", 250))
    # Outcomes' generic goodput denominator includes setup; expose workload rate.
    result["workload_goodput_per_s"] = sum(t <= cutoff for t in out.completed.values()) / (
        cutoff - start) * 1e6
    result.update(case="route_family", route_metrics=metrics, route_selections=dict(uses),
                  installed_route_state_bytes=route_state_bytes,
                  setup_wire_bytes=setup_wire, setup_cpu_service_us=setup_cpu,
                  accepted_recipient_pairs=sum(map(len, delivered.values())),
                  required_recipient_pairs=count * nr,
                  objective="static normalized unbatched data+ACK byte equivalents; no tail objective")
    return result


def run_senders(config):
    """Equivalent released copies, local timers, receiver ledger and real notices.

    The stable live recipient ledger is an explicit fixture assumption. Its
    survival across recipient failure and external effect atomicity are absent.
    """
    count, rate = config.get("count", 600), config.get("rate", 20000)
    if count < 1 or rate <= 0 or config.get("receivers", 1) < 1:
        raise ValueError("positive count, rate and receivers required")
    policy = config.get("policy", "timed_ranked")
    if policy not in ("single_ranked", "timed_ranked", "readiness_ranked", "eager"):
        raise ValueError("unknown sender policy")
    origins = ("s0", "s1", "s2")
    recipients = tuple(f"d{i}" for i in range(config.get("receivers", 1)))
    ready = dict(zip(origins, config.get("release_us", (0, 30, 80))))
    travel = dict(zip(origins, config.get("origin_latency_us", (80, 10, 30))))
    sim = Sim(config.get("seed", 1))
    net = network(sim, dict.fromkeys(origins + recipients, "a"), config)
    for origin in origins:
        for recipient in recipients:
            net.edge(origin, recipient).latency_us = travel[origin]
            net.edge(recipient, origin).latency_us = travel[origin]
            net.edge(recipient, origin).loss = config.get("receipt_loss", 0)
    out = Outcomes(sim)
    accepted, sent = {}, {}
    size = config.get("size", 512)
    start = config.get("start_us", 1000)
    fallback = config.get("fallback_us", 60)
    attempts_by_origin, suppressed_by_origin = Counter(), Counter()

    def offer(key):
        out.offer(key)
        accepted[key], sent[key] = set(), set()
        known = {origin: set() for origin in origins}
        if policy == "readiness_ranked":
            order = tuple(sorted(origins, key=lambda origin: ready[origin] + travel[origin]))
        else:
            order = sender_order(str(key), dict.fromkeys(origins, 1), salt="equivalent-v1")

        def attempt(origin):
            # A sender sees only its own received notices. Global acceptance
            # cannot cancel this action before the actual notice arrives.
            targets = [r for r in recipients if r not in known[origin]]
            if not targets:
                sim.count["suppressed_sender_attempts"] += 1
                suppressed_by_origin[origin] += 1
                return
            sent[key].add(origin)
            attempts_by_origin[origin] += 1
            for target in targets:
                def receive(target=target, origin=origin):
                    first = target not in accepted[key]
                    if first:
                        accepted[key].add(target)
                        sim.count["logical_recipient_effects"] += 1
                        if len(accepted[key]) == len(recipients):
                            out.finish(key)
                    else:
                        sim.count["duplicate_equivalent_deliveries"] += 1
                    if not first and key in out.completed:
                        sim.count["deliveries_after_logical_completion"] += 1
                    # First acceptance broadcasts a suppression notice. A
                    # duplicate refreshes only its sender's receipt evidence.
                    for peer in origins if first else (origin,):
                        def notify(peer=peer, target=target):
                            known[peer].add(target)
                        net.send(target, peer, 24, notify, "control")
                net.send(origin, target, size, receive)

        for rank, origin in enumerate(order):
            if policy == "single_ranked" and rank:
                continue
            when = ready[origin]
            if policy in ("timed_ranked", "readiness_ranked"):
                when = max(when, rank * fallback)
            sim.later(when, lambda origin=origin: attempt(origin))

    for key in range(count):
        sim.at(start + key * 1e6 / rate, lambda key=key: offer(key))
    cutoff = start + count * 1e6 / rate
    drain = cutoff + config.get("drain_us", 5000)
    sim.run(cutoff)
    cutoff_bytes = sum(r.used for r in sim.resources.values())
    sim.run(drain)
    result = summary(sim, net, out, config, cutoff, drain, cutoff_bytes,
                     config.get("deadline_us", 120))
    result["workload_goodput_per_s"] = sum(t <= cutoff for t in out.completed.values()) / (
        cutoff - start) * 1e6
    result.update(case="equivalent_senders", release_us=ready,
                  origin_propagation_us=travel, attempts_by_origin=dict(attempts_by_origin),
                  suppressed_by_origin=dict(suppressed_by_origin),
                  accepted_recipient_pairs=sum(map(len, accepted.values())),
                  required_recipient_pairs=count * len(recipients),
                  messages_with_multiple_senders=sum(len(s) > 1 for s in sent.values()),
                  objective="delivery after authored equivalent-copy releases; no authority change")
    return result


def campaign(count=600):
    configs = []
    for regime, size, rate in (("small", 256, 20000), ("gateway_load", 4096, 100000),
                              ("packet_cpu_load", 32, 50000)):
        for policy in ("direct", "fixed_cheap", "noise", "shallow_hash",
                       "shallow_stripes", "shallow_block_stripes"):
            config = dict(case="route_family", name=f"{regime}-{policy}",
                          count=count, size=size, rate=rate, policy=policy)
            if regime == "packet_cpu_load":
                config.update(cpu_slots=1, cpu_packet_us=4)
            configs.append(config)
    for incident in ("healthy", "primary_crash", "lost_receipts"):
        for policy in ("single_ranked", "timed_ranked", "readiness_ranked", "eager"):
            config = dict(case="equivalent_senders", name=f"{incident}-{policy}",
                          count=count, policy=policy)
            if incident == "primary_crash":
                config["faults"] = [dict(kind="crash", node="s1",
                                         at_us=1000 + count / 20000 * 1e6 * .25,
                                         until_us=1000 + count / 20000 * 1e6 * .6)]
            if incident == "lost_receipts":
                config["receipt_loss"] = .5
            configs.append(config)
            if incident in ("healthy", "primary_crash") and policy in ("timed_ranked", "eager"):
                configs.append(dict(config, receivers=3, name=f"{incident}-few-targets-{policy}"))
    return configs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--count", type=int, default=600)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).parent
    dependencies = ("adaptation_study.py", "routing.py", "simulator.py")
    def hashes():
        return {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
                for name in dependencies}
    sources = hashes()
    results = []
    for config in campaign(args.count):
        results.append((run_routes if config["case"] == "route_family" else run_senders)(config))
    if sources != hashes():
        raise RuntimeError("source changed during study; refusing mixed-source evidence")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(
        kind="authored_synthetic_not_measured_performance", sources=sources, results=results),
        indent=2) + "\n")
    print(f"{len(results)} comparisons -> {args.output}")


if __name__ == "__main__":
    main()
