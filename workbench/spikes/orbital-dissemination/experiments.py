"""Authored write/fan-in/fan-out workloads over the finite-resource simulator."""
from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import asdict
import hashlib
import json
from pathlib import Path

from simulator import Edge, Network, Node, Outcomes, Sim, resource_report


def topology(names, config):
    nodes = [Node(name, zone, cpu_slots=config.get("cpu_slots", 2),
                  cpu_packet_us=config.get("cpu_packet_us", .35),
                  nic_bytes_us=config.get("nic_bytes_us", 1250),
                  persist_us=config.get("persist_us", 10),
                  persist_slots=config.get("persist_slots", 8),
                  persist_bytes_us=config.get("persist_bytes_us", 2000),
                  queue_bytes=config.get("queue_bytes", 1 << 20),
                  reserve_bytes=config.get("reserve_bytes", 0))
             for name, zone in names.items()]
    edges = {}
    for a in nodes:
        for b in nodes:
            if a.name == b.name:
                continue
            public = "public" in (a.zone, b.zone)
            latency = (config.get("public_us", 1000) if public else
                       config.get("same_az_us", 8) if a.zone == b.zone else
                       config.get("cross_az_us", 100))
            if a.zone != b.zone and (a.name.startswith("s") or b.name.startswith("s")):
                latency += config.get("slow_extra_us", 60)
            edges[a.name, b.name] = Edge(
                latency_us=latency, jitter_us=config.get("jitter_us", 0),
                loss=config.get("loss", 0), duplicate=config.get("duplicate", 0),
                price_per_gb=(.09 if public else .01 if a.zone != b.zone else 0),
                pool="public" if public else config.get("fabric_pool"))
    return nodes, edges


def network(sim, names, config):
    nodes, edges = topology(names, config)
    pools = {"public": dict(bytes_us=config.get("public_bytes_us", 125),
                             queue_bytes=config.get("queue_bytes", 1 << 20))}
    pools.update(config.get("pools", {}))
    net = Network(sim, nodes, edges, pools=pools,
                  batch_bytes=config.get("batch_bytes", 0),
                  batch_wait_us=config.get("batch_wait_us", 0),
                  batch_capacity=config.get("queue_bytes", 1 << 20),
                  mtu=config.get("mtu", 1400), retry_us=config.get("retry_us", 400),
                  max_retries=config.get("max_retries", 2))
    net.inject(config.get("faults", []))
    return net


def tree(origin, recipients, policy):
    """Explicit direct, chain, bounded-degree or two-tier comparison baselines."""
    result = {}
    for i, recipient in enumerate(recipients):
        if policy == "chain":
            parent = origin if i == 0 else recipients[i - 1]
        elif policy == "balanced":
            parent = origin if i < 4 else recipients[(i - 4) // 4]
        elif policy == "hierarchical":
            # Recipients are grouped in zone order by the caller.
            parent = origin if i % 4 == 0 else recipients[i - i % 4]
        else:
            parent = origin
        result.setdefault(parent, []).append(recipient)
    return result


def routing_tree(net, origin, recipients, policy, *, size, message_id=0,
                 config=None, cache=None):
    """Adapt static planner candidates to flood's parent->children mapping.

    Edmonds/family minimize synthetic wire-byte equivalents, charging an
    isolated data transfer and ACK at each edge's configured monetary price.
    Shortest paths use baseline propagation only. Neither score includes
    contention, batching, faults, retransmissions or a percentile objective.
    Cached plans deliberately do not inspect fault state while messages fly.
    """
    if policy not in ("edmonds", "shortest", "family"):
        if policy not in ("direct", "chain", "balanced", "hierarchical"):
            raise ValueError("unknown message routing policy " + policy)
        return tree(origin, recipients, policy)
    from math import ceil, isfinite
    from routing import (Edge as PlannerEdge, minimum_arborescence,
                         shortest_path_forest, tree_family, tree_metrics)

    config = config or {}
    cache = {} if cache is None else cache
    recipients = tuple(dict.fromkeys(r for r in recipients if r != origin))
    nodes = (origin,) + recipients
    reference_price = config.get("routing_reference_price_per_gb", .01)
    if not isfinite(reference_price) or reference_price <= 0:
        raise ValueError("routing reference price must be finite and positive")
    family_count = config.get("routing_family_count", 4)
    seed = config.get("seed", 1)
    jitter = config.get("routing_jitter", .1)
    reuse_penalty = config.get("routing_reuse_penalty", .25)
    key = (origin, recipients, size, policy, reference_price, family_count,
           seed, jitter, reuse_penalty)
    if key not in cache:
        # _batch adds a 16-byte logical transfer frame; every packet adds header.
        packet_payload = net.mtu - net.header
        wire = size + 16 + ceil((size + 16) / packet_payload) * net.header
        ack_wire = 24 + 16 + ceil(40 / packet_payload) * net.header
        edges = []
        for src in nodes:
            for dst in nodes:
                if src == dst:
                    continue
                edge, reverse = net.edge(src, dst), net.edge(dst, src)
                cost = (wire * (1 + edge.price_per_gb / reference_price) +
                        ack_wire * (1 + reverse.price_per_gb / reference_price))
                edges.append(PlannerEdge(src, dst, cost, edge.latency_us))
        if policy == "edmonds":
            plans = (minimum_arborescence(nodes, edges, origin),)
        elif policy == "shortest":
            plans = (shortest_path_forest(nodes, edges, {origin: 0}),)
        else:
            plans = tree_family(nodes, edges, origin, count=family_count,
                                seed=seed, jitter=jitter, reuse_penalty=reuse_penalty)
        routes = []
        metrics = []
        for plan in plans:
            route = {}
            for edge in plan:
                route.setdefault(edge.src, []).append(edge.dst)
            routes.append(route)
            measured = tree_metrics(nodes, plan, {origin: 0})
            metrics.append(dict(wire_byte_equivalent_cost=measured.total_cost,
                                propagation_max_us=measured.max_arrival_us,
                                depth=measured.max_depth, fanout=measured.max_fanout))
        cache[key] = dict(origin=origin, size=size, candidates=routes,
                          metrics=metrics, selections=[0] * len(routes))
    record = cache[key]
    identity = json.dumps([origin, message_id, seed], separators=(",", ":"))
    hashed = hashlib.blake2b(identity.encode(), digest_size=8,
                            person=b"orbital-tree").digest()
    selected = int.from_bytes(hashed, "big") % len(record["candidates"])
    record["selections"][selected] += 1
    return record["candidates"][selected]


def flood(net, routes, origin, size, callback, kind="data"):
    def forward(src):
        for dst in routes.get(src, []):
            def receive(dst=dst):
                callback(dst)
                forward(dst)
            net.send(src, dst, size, receive, kind)
    forward(origin)


def schedule(sim, count, rate, action, burst=1):
    if rate <= 0 or count <= 0 or burst <= 0:
        raise ValueError("count, rate and burst must be positive")
    for i in range(count):
        sim.at((i // burst) * burst * 1e6 / rate, lambda i=i: action(i))
    return count * 1e6 / rate


def run_writes(config, proposal_timing_factory=None):
    sim = Sim(config.get("seed", 1))
    count, rate = config.get("count", 2000), config.get("rate", 20000)
    shard_count = config.get("shards", 1)
    per_shard = config.get("consumers", 8)
    names, shards = {}, []
    shared = config.get("shared_witnesses", False)
    colocated = config.get("colocated", False)
    for s in range(shard_count):
        group = 0 if shared else s
        p, leader, fast, slow = f"p{s}", f"l{group}", f"f{group}", f"s{group}"
        names.update({p: "a", leader: "a", fast: "b", slow: "c"})
        if config.get("producers", 1) > 1:
            for i in range(config["producers"]):
                names[f"p{s}_p{i}"] = "a"
        consumers = []
        for j in range(per_shard):
            c = (fast if j == 0 else slow) if colocated else f"c{s}_{j}"
            zone = "b" if j < (per_shard + 1) // 2 else "c"
            if config.get("public_consumers", False) and j >= per_shard // 2:
                zone = "public"
            names.setdefault(c, zone)
            if c not in consumers:
                consumers.append(c)
        shards.append((p, leader, fast, slow, consumers))
    net = network(sim, names, config)
    timing = proposal_timing_factory(sim, net) if proposal_timing_factory else None
    effect, payload, response, all_consumers = (Outcomes(sim) for _ in range(4))
    size = config.get("size", 256)
    mode = config.get("admission", "strict")
    if mode not in ("strict", "pipelined_candidate"):
        raise ValueError("unknown admission path")
    enhancement = config.get("enhancement", "none")
    policy = config.get("policy", "direct")
    next_position = [0] * shard_count
    # Each witness advances a contiguous per-shard journal prefix. A conditional
    # proposal with missing payload cannot be skipped merely to improve latency.
    prefix = {}
    pending_prefix = {}
    leader_credits = {name: 0 for name in names}
    consumer_credits = {name: 0 for name in names}
    producer_for = {}

    def eligible(shard, witness, position, callback):
        lane = (shard, witness)
        pending_prefix.setdefault(lane, {})[position] = callback
        head = prefix.get(lane, 0)
        while head in pending_prefix[lane]:
            pending_prefix[lane].pop(head)()
            head += 1
        prefix[lane] = head

    def offer(key):
        p, leader, fast, slow, consumers = shards[key % shard_count]
        if config.get("producers", 1) > 1:
            p = f"p{key % shard_count}_p{(key // shard_count) % config['producers']}"
        producer_for[key] = p
        for out in (effect, payload, response, all_consumers):
            out.offer(key)
        durable, journal, branched, event_sent = set(), set(), set(), set()
        state = {c: set() for c in consumers}
        started, finished = set(), set()
        admitted = False
        proposed = False
        operation_mode = mode
        position = None
        queued = set()
        leader_released = False
        # Two independently durable origins get disjoint destinations by default.
        # Redundant mode pays both trees, sharing the same callback identities.
        recipients = {}
        for witness in (fast, slow):
            recipients[witness] = (consumers if policy == "redundant" else
                [c for c in consumers if (c == fast or names[c] == "b") == (witness == fast)])
        routes = {w: tree(w, recipients[w], policy) for w in (fast, slow)}

        def consume(c, component):
            if component in state[c]:
                return
            if component == "payload":
                if consumer_credits[c] + size > config.get("queue_bytes", 1 << 20):
                    sim.count["overflow:consumer_join:" + c] += 1
                    return
                consumer_credits[c] += size
            state[c].add(component)
            if c in started or not {"payload", "epoch"} <= state[c]:
                return
            started.add(c)
            effect.mark(key, "target_ready" if c == consumers[0] else "other_ready")
            analysis = (config.get("verify_us", 1) if "hint" in state[c] else
                        config.get("analysis_us", 8))

            def done():
                consumer_credits[c] -= size
                finished.add(c)
                if c == consumers[0]:
                    effect.finish(key)
                    net.send(c, p, config.get("response_bytes", 64),
                             lambda: response.finish(key), "control")
                if len(finished) == len(consumers):
                    all_consumers.finish(key)
            net.compute(c, analysis + config.get("fold_us", 2), size, done)

        def disseminate(w, component, nbytes):
            if w in recipients[w]:
                net.send(w, w, nbytes, lambda: consume(w, component),
                         "control" if component == "epoch" else "data")
            # A colocated consumer is the root, never its own child.
            route = tree(w, [c for c in recipients[w] if c != w], policy)
            flood(net, route, w, nbytes, lambda c: consume(c, component),
                  "control" if component == "epoch" else "data")

        def publish_payload(w):
            if w in event_sent:
                return
            event_sent.add(w)
            disseminate(w, "payload", size)

        def maybe_branch(w):
            nonlocal admitted
            if w not in durable or w not in journal or w in queued:
                return
            queued.add(w)
            effect.mark(key, "eligible:" + w)
            eligible(key % shard_count, w, position, lambda: branch(w))

        def branch(w):
            nonlocal admitted, leader_released
            branched.add(w)
            admitted = True
            effect.mark(key, "admitted:" + w)
            if not leader_released:
                leader_released = True
                def release():
                    leader_credits[leader] -= 128
                # Branch dissemination doesn't wait for this return to leader.
                net.send(w, leader, 40, release, "control")
            disseminate(w, "epoch", 64)
            if enhancement == "none":
                publish_payload(w)
            else:
                if enhancement == "optional":
                    publish_payload(w)

                def hint_done():
                    hint_bytes = config.get("hint_bytes", 512)
                    if enhancement == "blocking":
                        # Hint and raw input co-travel; both are available on receipt.
                        route = tree(w, [c for c in recipients[w] if c != w], policy)
                        def enriched(c):
                            consume(c, "hint")
                            consume(c, "payload")
                        if w in recipients[w]:
                            net.send(w, w, size + hint_bytes, lambda: enriched(w))
                        flood(net, route, w, size + hint_bytes, enriched)
                        event_sent.add(w)
                    else:
                        disseminate(w, "hint", hint_bytes)
                # Application analysis worker colocated with the witness, sharing
                # its CPU. This does not give the witness application semantics.
                net.compute(w, config.get("analysis_us", 8), size, hint_done)

        def propose():
            nonlocal proposed
            if proposed:
                return
            proposed = True
            def at_leader():
                nonlocal position
                if leader_credits[leader] + 128 > config.get("queue_bytes", 1 << 20):
                    sim.count["admission_refused"] += 1
                    return
                leader_credits[leader] += 128
                position = next_position[key % shard_count]
                next_position[key % shard_count] += 1
                effect.mark(key, "leader_request")
                def leader_durable():
                    effect.mark(key, "leader_journal")
                    for w in (fast, slow):
                        def proposal_received(w=w):
                            def witness_durable():
                                journal.add(w)
                                effect.mark(key, "journal:" + w)
                                maybe_branch(w)
                            net.persist(w, 64, witness_durable, "control")
                        net.send(leader, w, 96, proposal_received, "control")
                net.persist(leader, 64, leader_durable, "control")
            net.send(p, leader, 64, at_leader, "control")

        def producer_durable():
            nonlocal operation_mode
            effect.mark(key, "producer_durable")
            if timing:
                operation_mode = ("pipelined_candidate" if timing.begin(p, key) else "strict")
                effect.mark(key, "proposal_mode:" + operation_mode)
            if operation_mode == "pipelined_candidate":
                propose()
            for w in (fast, slow):
                def data_received(w=w):
                    def follower_durable():
                        durable.add(w)
                        effect.mark(key, "payload:" + w)
                        payload.finish(key)
                        if enhancement != "blocking":
                            publish_payload(w)
                        maybe_branch(w)
                        if operation_mode == "strict" or timing:
                            def receipt():
                                if timing:
                                    timing.receipt(p, key)
                                if operation_mode == "strict":
                                    propose()
                            net.send(w, p, 40, receipt, "control")
                    net.persist(w, size, follower_durable)
                net.send(p, w, size, data_received)
        net.persist(p, size, producer_durable)

    cutoff = schedule(sim, count, rate, offer, config.get("burst", 1))
    drain = cutoff + config.get("drain_us", 5000)
    sim.run(cutoff)
    snapshot = {n: r.used for n, r in sim.resources.items()}
    sim.run(drain)
    result = effect.summary(cutoff, drain)
    result.update(payload_ready=payload.summary(cutoff, drain),
                  response=response.summary(cutoff, drain),
                  all_consumers=all_consumers.summary(cutoff, drain))
    result["traces"] = {str(k): effect.trace[k] for k in list(effect.trace)[:3]}
    result["journal_ready_behind_gap"] = sum(len(q) for q in pending_prefix.values())
    result["pending_leader_journal_bytes"] = sum(leader_credits.values())
    result["pending_consumer_input_bytes"] = sum(consumer_credits.values())
    result["max_admitted_journal_prefix_wait_us"] = max(
        (trace["admitted:" + witness] - trace["eligible:" + witness]
         for trace in effect.trace.values() for witness in names
         if "admitted:" + witness in trace), default=0)
    result["max_pending_journal_prefix_wait_us"] = max(
        (drain - value for trace in effect.trace.values() for stage, value in trace.items()
         if stage.startswith("eligible:") and stage.replace("eligible:", "admitted:") not in trace),
        default=0)
    result["by_producer"] = {}
    first_fault_start, first_fault_end = min(
        ((f["at_us"], f["until_us"]) for f in config.get("faults", []) if "until_us" in f),
        default=(0, 0))
    for producer in sorted(set(producer_for.values())):
        subset = Outcomes(sim)
        subset.offered = {k: t for k, t in effect.offered.items() if producer_for[k] == producer}
        subset.completed = {k: t for k, t in effect.completed.items() if k in subset.offered}
        row = subset.summary(cutoff, drain)
        row["completed_by_first_fault_end"] = sum(t <= first_fault_end for t in subset.completed.values())
        row["born_in_first_fault_window_unfinished_1ms_after"] = sum(
            first_fault_start <= t <= first_fault_end and
            subset.completed.get(k, float("inf")) > first_fault_end + 1000
            for k, t in subset.offered.items())
        result["by_producer"][producer] = row
    if timing:
        result["proposal_observations"] = timing.report()
        result["proposal_mode_counts"] = dict(Counter(
            stage.split(":", 1)[1] for trace in effect.trace.values()
            for stage in trace if stage.startswith("proposal_mode:")))
    return finish(config, sim, net, result, snapshot)


def run_messages(config):
    sim = Sim(config.get("seed", 1))
    ns, nr = config.get("senders", 8), config.get("receivers", 8)
    names = {f"p{i}": "a" for i in range(ns)}
    names.update({f"r{i}": config.get("receiver_zone", "b" if i % 2 else "a")
                  for i in range(nr)})
    if config.get("relay", False):
        names["relay"] = "a"
    net = network(sim, names, config)
    out = Outcomes(sim)
    count, rate, size = (config.get("count", 2000), config.get("rate", 20000),
                         config.get("size", 64))
    recipients = [f"r{i}" for i in range(nr)]
    policy = config.get("policy", "direct")
    route_cache = {}
    frontier_number = 0

    def routes(origin, payload_size, message_id):
        return routing_tree(net, origin, recipients, policy, size=payload_size,
                            message_id=message_id, config=config, cache=route_cache)

    # Optional monotone per-stream coalescing at an in-arborescence frontier.
    pending = {}
    frontiers = {i: -1 for i in range(ns)}
    seen = {i: set() for i in range(ns)}
    scheduled = False

    def at_recipient(key, receiver):
        pending[key].add(receiver)
        if len(pending[key]) == nr:
            out.finish(key)

    def emit_frontiers():
        nonlocal scheduled, frontier_number
        scheduled = False
        snapshot = dict(frontiers)
        def arrive(receiver):
            for key in pending:
                stream, sequence = key % ns, key // ns
                if sequence <= snapshot[stream]:
                    at_recipient(key, receiver)
        frontier_size = 16 + 16 * ns
        flood(net, routes("relay", frontier_size, frontier_number),
              "relay", frontier_size, arrive, "control")
        frontier_number += 1

    def offer(key):
        nonlocal scheduled
        out.offer(key)
        pending[key] = set()
        source = f"p{key % ns}"
        def forward():
            nonlocal scheduled
            if config.get("aggregate", False):
                stream, seq = key % ns, key // ns
                seen[stream].add(seq)
                while frontiers[stream] + 1 in seen[stream]:
                    frontiers[stream] += 1
                    seen[stream].remove(frontiers[stream])
                if not scheduled:
                    scheduled = True
                    sim.later(config.get("aggregate_wait_us", 4), emit_frontiers)
            else:
                origin = "relay" if config.get("relay", False) else source
                flood(net, routes(origin, size, key),
                      origin, size, lambda c: at_recipient(key, c))
        if config.get("relay", False):
            net.send(source, "relay", size, forward)
        else:
            forward()
    cutoff = schedule(sim, count, rate, offer, config.get("burst", 1))
    drain = cutoff + config.get("drain_us", 5000)
    sim.run(cutoff)
    snapshot = {n: r.used for n, r in sim.resources.items()}
    sim.run(drain)
    result = out.summary(cutoff, drain)
    if route_cache:
        result["routing"] = dict(
            policy=policy,
            objective=("minimum baseline propagation paths" if policy == "shortest"
                       else "normalized unbatched data+ACK wire-byte equivalents"),
            cost_formula="sum(wire_bytes * (1 + edge_price_per_gb / reference_price_per_gb))",
            reference_price_per_gb=config.get("routing_reference_price_per_gb", .01),
            readiness_us=0,
            graph="origin and required recipients; complete directed physical edges",
            limitations="static, fault-unaware; no queue, batching, retry or tail objective",
            plans=[dict(origin=value["origin"], size=value["size"],
                        metrics=value["metrics"], selections=value["selections"])
                   for value in route_cache.values()])
    return finish(config, sim, net, result, snapshot)


def finish(config, sim, net, result, snapshot):
    result.update(config=config, counters=dict(sim.count), resources=resource_report(sim),
                  resource_bytes_at_cutoff=snapshot,
                  pending_batch_bytes=sum(net.pending_bytes.values()),
                  pending_reassembly_bytes=sum(net.reassembly_bytes.values()),
                  unacked_transfers=sum(t.reliable and not t.acked for t in net.transfers))
    return result


def run(config):
    return run_writes(config) if config.get("kind", "writes") == "writes" else run_messages(config)


def campaign(count):
    cases = []
    def add(name, **kw):
        cases.append(dict(name=name, count=count, **kw))
    for admission in ("strict", "pipelined_candidate"):
        for cross in (80, 100, 130):
            add(f"write-{admission}-{cross}", admission=admission, cross_az_us=cross)
    for policy in ("direct", "chain", "balanced", "hierarchical", "redundant"):
        for rate in (20000, 100000):
            add(f"large-{policy}-{rate}", admission="pipelined_candidate",
                consumers=24, policy=policy, rate=rate, queue_bytes=65536)
    for enhancement in ("none", "blocking", "optional"):
        add(f"enhance-local-{enhancement}", admission="pipelined_candidate", enhancement=enhancement)
        add(f"enhance-public-{enhancement}", admission="pipelined_candidate", enhancement=enhancement,
            public_consumers=True, hint_bytes=4096)
    for batch in (0, 1200):
        for rate in (100000, 500000):
            for aggregate in (False, True):
                add(f"incast-{batch}-{rate}-{aggregate}", kind="messages", relay=True,
                    aggregate=aggregate, senders=32, receivers=1, size=32, rate=rate,
                    cpu_slots=1, cpu_packet_us=1, batch_bytes=batch, batch_wait_us=4,
                    queue_bytes=32768, burst=16)
    for senders in (1, 3, 16):
        for receivers in (1, 3, 16):
            add(f"cardinality-{senders}-{receivers}", kind="messages", senders=senders,
                receivers=receivers, size=64)
    for size in (64, 4096, 65536):
        for rate in (10000, 50000):
            add(f"bytes-{size}-{rate}", size=size, rate=rate, admission="pipelined_candidate",
                queue_bytes=131072)
    for shared in (False, True):
        add(f"shards-shared-{shared}", shards=16, shared_witnesses=shared, rate=100000,
            admission="pipelined_candidate", queue_bytes=65536)
    add("collocated-shards", shards=16, shared_witnesses=True, colocated=True,
        rate=100000, admission="pipelined_candidate")
    for fault in (
        dict(kind="crash", node="f0"),
        dict(kind="cpu_slow", node="f0", factor=20),
        dict(kind="disk_slow", node="f0", factor=20),
        dict(kind="partition", src="f0", dst="c0_0"),
        dict(kind="edge_delay", src="p0", dst="f0", delay_us=300),
    ):
        for policy in ("direct", "redundant"):
            cutoff = count * 1e6 / 20000
            add(f"fault-{fault['kind']}-{policy}", admission="pipelined_candidate", policy=policy,
                faults=[dict(fault, at_us=cutoff * .25, until_us=cutoff * .5)])
    for seed in (1, 2, 3):
        add(f"jitter-{seed}", admission="pipelined_candidate", jitter_us=3, loss=.001, seed=seed)
    for admission in ("strict", "pipelined_candidate"):
        add(f"missing-payload-prefix-{admission}", admission=admission, producers=2,
            faults=[dict(kind="partition", src="p0_p0", dst=w, at_us=0, until_us=1500)
                    for w in ("f0", "s0")])
        add(f"repairable-prefix-{admission}", admission=admission, producers=2, rate=100000,
            faults=[dict(kind="partition", src="p0_p0", dst=w, at_us=0, until_us=300)
                    for w in ("f0", "s0")])
    for policy in ("edmonds", "shortest", "family"):
        add(f"solver-{policy}", kind="messages", senders=3, receivers=16, policy=policy,
            rate=100000, size=4096, queue_bytes=65536)
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, help="single JSON scenario")
    parser.add_argument("--campaign", action="store_true")
    parser.add_argument("--count", type=int, default=2000)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    configs = campaign(args.count) if args.campaign else [json.loads(args.config.read_text())]
    results = [run(c) for c in configs]
    sources = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
               for p in Path(__file__).parent.glob("*.py")}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(model="synthetic-discrete-event-v1", sources=sources,
                                          results=results), indent=2) + "\n")
    print(f"{len(results)} scenarios -> {args.output}")


if __name__ == "__main__":
    main()
