"""Synthetic delivery model. Time: us; resources: bytes/us; prices: USD/decimal GB.

No sockets, cloud API, storage durability, consensus, or transport implementation.
Network transfers share all of their capacity pools by weighted max-min fairness.
CPU work is non-preemptive per chunk, with control jobs ahead of queued data jobs.
"""
from __future__ import annotations

from collections import defaultdict, deque
from dataclasses import dataclass
import hashlib
import heapq
import math
import random
from typing import Callable

import networkx as nx


def draw(*key):
    """Counter-based random draw: unrelated event ordering cannot consume this draw."""
    raw = hashlib.blake2b(repr(key).encode(), digest_size=8).digest()
    return int.from_bytes(raw, "big") / 2**64


def wire(size, mtu=1500):
    """IPv6/UDP + WireGuard + inner IPv6/UDP + 32-byte application framing.

    Outer 80 bytes; inner 80 bytes; encrypted inner packet padded to 16 bytes.
    This is an illustrative framing contract, not measured WireGuard throughput.
    """
    capacity = ((mtu - 80) // 16) * 16 - 80
    if capacity <= 0 or size <= 0:
        raise ValueError("positive payload and MTU above framing overhead required")
    packets = math.ceil(size / capacity)
    full, tail = divmod(size, capacity)
    total = full * (80 + math.ceil((80 + capacity) / 16) * 16)
    if tail:
        total += 80 + math.ceil((80 + tail) / 16) * 16
    return total, packets


def chunks(size, chunk):
    return [min(chunk, size - i) for i in range(0, size, chunk)]


class Topology:
    def __init__(self, data):
        self.data = data
        self.nodes = {n["id"]: n for n in data["nodes"]}
        self.edges = {e["id"]: e for e in data["edges"]}
        self.capacity = {r["id"]: r["gbps"] * 125 for r in data["resources"]}
        self.resource_info = {r["id"]: r for r in data["resources"]}
        self.pairs = defaultdict(list)
        for e in self.edges.values():
            assert e["src"] in self.nodes and e["dst"] in self.nodes
            assert e["src"] != e["dst"] and e["delay_us"] >= 0
            assert e["resources"] and all(r in self.capacity for r in e["resources"])
            assert len(e["resources"]) == len(set(e["resources"]))
            assert all(v >= 0 for v in e.get("charges", {}).values())
            self.pairs[e["src"], e["dst"]].append(e)
        assert all(c > 0 for c in self.capacity.values())

    def legal(self, edge, spec):
        allowed = spec.get("allowed_locations")
        return not allowed or all(x in allowed for x in edge["locations"])

    def cost(self, edge, size, chunk, mtu):
        total = sum(wire(s, mtu)[0] for s in chunks(size, chunk))
        return total / 1e9 * sum(edge.get("charges", {}).values()) + edge.get("request_usd", 0)

    def delay(self, edge, size, spec):
        # Unloaded, whole-object store/forward planning surrogate; NOT simulation.
        b = sum(wire(s, spec["mtu"])[0] for s in chunks(size, spec["chunk_bytes"]))
        capacity = min(self.capacity[r] * spec.get("capacity_scale", 1) for r in edge["resources"])
        return edge["delay_us"] + edge.get("setup_us", 0) + b / capacity

    def direct(self, src, dst, spec, objective="delay"):
        es = [e for e in self.pairs[src, dst] if self.legal(e, spec)]
        if not es:
            raise ValueError(f"no legal direct route {src} -> {dst}")
        score = (lambda e: self.delay(e, spec["size_bytes"], spec)) if objective == "delay" else (
            lambda e: self.cost(e, spec["size_bytes"], spec["chunk_bytes"], spec["mtu"]))
        return min(es, key=lambda e: (score(e), e["id"]))["id"]


def validate_plan(topo, root, targets, plan):
    graph = nx.DiGraph()
    graph.add_node(root)
    for dst, eid in plan.items():
        e = topo.edges[eid]
        assert dst == e["dst"] and dst != root
        graph.add_edge(e["src"], dst)
    if not nx.is_directed_acyclic_graph(graph):
        raise ValueError("routing plan contains a cycle")
    reachable = nx.descendants(graph, root) | {root}
    if not set(targets) <= reachable or set(graph) != reachable:
        raise ValueError("routing plan does not reach every required recipient")


def plan(topo, spec, policy):
    root, targets = spec["source"], spec["targets"]
    if policy == "robust":
        from planner import optimize
        from resilience import select
        candidates = [x["route"] for x in optimize(topo,spec)["candidates"]]
        return select(topo,spec,candidates)["route"]
    if policy in ("balanced", "latency", "shallow"):
        from planner import optimize, shallow
        return shallow(topo, spec) if policy == "shallow" else dict(optimize(topo, spec)[policy])
    if policy == "branch-b1":
        route = plan(topo, spec, "cost")
        route["b1"] = topo.direct("b2", "b1", spec)
        validate_plan(topo, root, targets, route)
        return route
    if policy == "direct":
        return {t: topo.direct(root, t, spec) for t in targets}
    graph = nx.DiGraph()
    included = {root, *targets, *spec.get("relays", [])}
    graph.add_nodes_from(sorted(included))
    for e in topo.edges.values():
        if e["src"] not in included or e["dst"] not in included or e["dst"] == root or not topo.legal(e, spec):
            continue
        cost = topo.cost(e, spec["size_bytes"], spec["chunk_bytes"], spec["mtu"])
        delay = topo.delay(e, spec["size_bytes"], spec)
        score = delay if policy in ("shortest", "bounded") else cost
        old = graph.get_edge_data(e["src"], e["dst"])
        if old is None or (score, delay, e["id"]) < (old["weight"], old["delay"], old["eid"]):
            graph.add_edge(e["src"], e["dst"], weight=score, delay=delay, eid=e["id"], cost=cost)
    if policy in ("shortest", "bounded"):
        paths = nx.single_source_dijkstra_path(graph, root, weight="weight")
        result = {}
        for target in targets:
            if target not in paths:
                raise ValueError(f"unreachable required recipient {target}")
            for a, b in zip(paths[target], paths[target][1:]):
                result[b] = graph[a][b]["eid"]
    elif policy == "cost":
        # Rooted directed minimum spanning arborescence; root has no incoming edge.
        # If optional relay nodes exist, prune unused leaves afterwards. That is a
        # Steiner heuristic, NOT a minimum-cost directed Steiner tree guarantee.
        tree = nx.minimum_spanning_arborescence(graph, attr="weight", preserve_attrs=True)
        result = {b: d["eid"] for a, b, d in tree.edges(data=True)}
        changed = True
        while changed:
            changed = False
            parents = {topo.edges[e]["src"] for e in result.values()}
            for n in list(result):
                if n not in targets and n not in parents:
                    del result[n]
                    changed = True
    else:
        raise ValueError(f"unknown routing policy: {policy}")
    validate_plan(topo, root, targets, result)
    if policy == "bounded":
        # Greedy cost-reducing reparenting within a per-recipient static stretch.
        # Shared-capacity simulation evaluates it later; no deadline guarantee.
        def distances(p):
            g = nx.DiGraph()
            g.add_node(root)
            for dst, eid in p.items():
                e = topo.edges[eid]
                g.add_edge(e["src"], dst, weight=topo.delay(e, spec["size_bytes"], spec))
            return nx.single_source_dijkstra_path_length(g, root)
        budget = {n: d * spec.get("stretch", 1.5) for n, d in distances(result).items()}
        while True:
            candidates = []
            for dst, old in result.items():
                old_cost = topo.cost(topo.edges[old], spec["size_bytes"], spec["chunk_bytes"], spec["mtu"])
                for e in topo.edges.values():
                    if e["dst"] != dst or e["src"] not in ({root} | set(result)) or not topo.legal(e, spec):
                        continue
                    saving = old_cost - topo.cost(e, spec["size_bytes"], spec["chunk_bytes"], spec["mtu"])
                    if saving <= 1e-15:
                        continue
                    alternative = dict(result, **{dst: e["id"]})
                    try:
                        validate_plan(topo, root, targets, alternative)
                        ds = distances(alternative)
                    except ValueError:
                        continue
                    if all(ds[t] <= budget[t] + 1e-8 for t in targets):
                        candidates.append((-saving, e["id"], dst, alternative))
            if not candidates:
                break
            result = min(candidates, key=lambda c: c[:3])[3]
    validate_plan(topo, root, targets, result)
    return result


def fair_rates(flows, capacities):
    """Weighted max-min allocation over intersecting capacity pools."""
    rates = {i: 0.0 for i in flows}
    remaining = dict(capacities)
    free = set(flows)
    while free:
        demand = defaultdict(float)
        for i in free:
            for r in flows[i].resources:
                demand[r] += flows[i].weight
        level = min(remaining[r] / w for r, w in demand.items())
        saturated = {r for r, w in demand.items() if remaining[r] / w <= level + 1e-10}
        for i in free:
            rates[i] += level * flows[i].weight
        for r, w in demand.items():
            remaining[r] = max(0, remaining[r] - level * w)
        fixed = {i for i in free if saturated.intersection(flows[i].resources)}
        assert fixed, "water filling made no progress"
        free -= fixed
    return rates


@dataclass
class Flow:
    remaining: float
    resources: tuple
    weight: float
    complete: Callable
    start: float
    edge: str
    message: int
    kind: str
    chunk: int
    size: int


class Simulator:
    def __init__(self, topo, spec, route, seed=1, trace=False):
        validate_plan(topo, spec["source"], spec["targets"], route)
        self.topo, self.spec, self.route, self.seed = topo, spec, route, seed
        self.now = 0.0
        self.serial = 0
        self.events = []
        self.flows = {}
        self.rates = {}
        self.cpu = defaultdict(list)
        self.cpu_busy = set()
        self.stream_queues = defaultdict(deque)
        self.stream_busy = set()
        self.stream_initialized = set()
        self.capacity = {r: c * spec.get("capacity_scale", 1) for r, c in topo.capacity.items()}
        for r, fraction in spec.get("background", {}).items():
            self.capacity[r] *= 1 - fraction
        assert all(c > 0 for c in self.capacity.values())
        self.used = defaultdict(float)
        self.peak = defaultdict(float)
        self.cpu_us = defaultdict(float)
        self.costs = defaultdict(float)
        self.bytes = defaultdict(int)
        self.packets = 0
        self.requests = set()
        self.retries = 0
        self.drops = 0
        self.duplicates = 0
        self.repairs = 0
        self.rejected = 0
        self.active_objects = set()
        self.pending_transfers = defaultdict(int)
        self.peak_active = 0
        self.repair_rounds = defaultdict(int)
        self.last_progress = {}
        self.route_versions = {}
        self.blocked_until = {}
        self.blocked_resources = {}
        self.repair_path = {}
        self.controller_events = []
        self.last_rewrite = -math.inf
        self.controller_busy = False
        self.messages = {}
        self.trace = []
        self.trace_enabled = trace
        self.edge_bytes = defaultdict(int)
        self.received = defaultdict(set)
        self.control_seen = set()
        self.deadline = spec["deadline_us"]
        self.parts = chunks(spec["size_bytes"], spec["chunk_bytes"])
        self.children = defaultdict(list)
        for dst, eid in route.items():
            self.children[topo.edges[eid]["src"]].append(eid)
        # Scheduling must not depend on a planner's dictionary insertion order.
        for edges in self.children.values():
            edges.sort()

    def children_for(self, mid, node):
        if not self.spec.get("adaptive"):
            return self.children[node]
        return sorted(eid for eid in self.route_versions[mid].values() if self.topo.edges[eid]["src"] == node)

    def observe_stall(self, mid, node):
        """Root learns a suspicion only after an actual missing-chunk request."""
        if not self.spec.get("adaptive"):
            return
        eid = self.repair_path.get((mid,node),self.route_versions[mid].get(node))
        if not eid:
            return
        if (mid,node) not in self.repair_path:
            # A downstream timeout is not evidence against a relay that has
            # never acknowledged possession. Trace back to the first unknown
            # holder after the last acknowledged ancestor.
            known={self.spec["source"],*self.messages[mid]["confirmed"]}
            parent=self.topo.edges[eid]["src"]
            while parent not in known:
                eid=self.route_versions[mid][parent]
                parent=self.topo.edges[eid]["src"]
        self.blocked_until[eid] = self.now + self.spec.get("quarantine_us", 20000)
        self.controller_events.append(dict(time_us=self.now, kind="suspect", edge=eid, message=mid, node=node))
        # Two distinct reported paths through one pool provide evidence of a
        # shared problem. This is suspicion with a TTL, not failure truth.
        evidence=defaultdict(set)
        for suspect,expiry in self.blocked_until.items():
            if expiry>self.now:
                for resource in self.topo.edges[suspect]["resources"]:
                    # Do not ban a receiver's every path because two upstream
                    # attempts stalled, or infer local-NIC death from timeouts.
                    if not resource.startswith(("tx:","rx:","link:")):
                        evidence[resource].add(suspect)
        for resource,paths in evidence.items():
            if len(paths)>=2:
                if self.blocked_resources.get(resource,0)<=self.now:
                    self.controller_events.append(dict(time_us=self.now,kind="suspect-pool",resource=resource))
                self.blocked_resources[resource]=self.now+self.spec.get("quarantine_us",20000)
        if self.controller_busy:
            return
        self.controller_busy = True
        ready=max(self.now,self.last_rewrite+self.spec.get("rewrite_hold_us",5000))
        self.event(ready,lambda:self.cpu_task(self.spec["source"],"controller",
                   self.spec.get("controller_decision_us",50),lambda:self.rewrite(mid,node)))

    def rewrite(self, mid, node):
        self.controller_busy = False
        from planner import alternatives, estimate, identity
        candidates = [dict(self.route), *alternatives(self.topo, self.spec, self.route)]
        def bad(p):
            return sum(self.edge_suspected(self.topo.edges[e]) for e in p.values())
        least = min(map(bad, candidates))
        if least >= bad(self.route):
            return
        available = [p for p in candidates if bad(p) == least]
        scores = [(p, estimate(self.topo, self.spec, p)) for p in available]
        best = min(s[1] for p,s in scores)
        eligible = [(p,s) for p,s in scores if s[1] <= best*(1+self.spec.get("planner_slack", .1))]
        chosen = min(eligible, key=lambda ps: (ps[1][0],ps[1][1],ps[1][2:],identity(ps[0])))[0]
        old = dict(self.route)
        self.route = chosen
        self.last_rewrite = self.now
        self.controller_events.append(dict(time_us=self.now, kind="rewrite", old=old, route=dict(chosen),
                                           trigger_message=mid, trigger_node=node))

    def edge_suspected(self, edge):
        return (self.blocked_until.get(edge["id"],0)>self.now or
                any(self.blocked_resources.get(r,0)>self.now for r in edge["resources"]))

    def receipt(self, mid, node):
        m = self.messages[mid]
        m["confirmed"].setdefault(node, self.now)

    def finish_transfer(self, mid):
        self.pending_transfers[mid] -= 1
        assert self.pending_transfers[mid] >= 0
        m = self.messages[mid]
        if not self.pending_transfers[mid] and all(t in m["confirmed"] for t in self.spec["targets"]):
            self.active_objects.discard(mid)

    def event(self, time, fn):
        assert time >= self.now - 1e-7
        self.serial += 1
        heapq.heappush(self.events, (max(time, self.now), self.serial, fn))

    def cpu_job(self, node, kind, size, packets, fn):
        n = self.topo.nodes[node]
        duration = n.get("chunk_cpu_us", 1) + size / (n.get("crypto_gbps", 32) * 125) + packets * n.get("packet_cpu_us", .08)
        self.cpu_task(node,kind,duration,fn)

    def cpu_task(self, node, kind, duration, fn):
        self.serial += 1
        heapq.heappush(self.cpu[node], (1 if kind == "data" else 0, self.serial, duration, fn))
        self.cpu_start(node)

    def cpu_start(self, node):
        if node in self.cpu_busy or not self.cpu[node]:
            return
        _, _, duration, fn = heapq.heappop(self.cpu[node])
        self.cpu_busy.add(node)
        self.cpu_us[node] += duration
        def done():
            self.cpu_busy.remove(node)
            fn()
            self.cpu_start(node)
        self.event(self.now + duration, done)

    def failed(self, edge, kind, start, end):
        f = self.spec.get("failure")
        if not f or kind not in f.get("kinds", ["data"]):
            return False
        affected = (f.get("source") == edge["src"] or f.get("edge") == edge["id"] or f.get("resource") in edge["resources"])
        return affected and start < f.get("end_us", math.inf) and end >= f.get("start_us", 0)

    def send(self, mid, eid, size, kind, chunk, callback, attempt=0):
        self.pending_transfers[mid] += 1
        e = self.topo.edges[eid]
        b, packets = wire(size, self.spec["mtu"])
        stream = (mid, eid, kind)
        def start():
            self.serial += 1
            identity = self.serial
            sent_at = self.now
            self.bytes[kind] += b
            self.packets += packets
            self.edge_bytes[eid] += b
            for charge, rate in e.get("charges", {}).items():
                self.costs[charge] += b / 1e9 * rate
            req = (mid, eid, kind)
            if req not in self.requests:
                self.requests.add(req)
                if kind == "data":
                    self.costs["requests"] += e.get("request_usd", 0)
            def crossed():
                end = self.now
                self.stream_busy.remove(stream)
                self.start_stream(stream)
                # One common domain draw per object, plus independent per-edge chunk delay.
                domain = e.get("jitter_domain", eid)
                common = draw(self.seed, mid, domain, "delay")
                extra = self.spec.get("jitter_scale", 1) * e.get("jitter_us", 0) * (-math.log(max(1e-12, 1-common)))
                extra += self.spec.get("jitter_scale", 1) * e.get("jitter_us", 0) * .1 * draw(self.seed, mid, eid, chunk, attempt)
                bad = self.failed(e, kind, sent_at, end)
                probability = 1 - (1-self.spec.get("packet_loss", 0))**packets
                bad |= draw(self.seed, mid, eid, kind, chunk, attempt, "loss") < probability
                if self.trace_enabled and mid == 0:
                    self.trace.append(dict(edge=eid, src=e["src"], dst=e["dst"], kind=kind, chunk=chunk,
                                           start_us=sent_at, wire_end_us=end, arrival_us=end+e["delay_us"]+extra,
                                           wire_bytes=b, dropped=bool(bad)))
                if bad:
                    self.drops += 1
                    if attempt < self.spec.get("max_retries", 1):
                        self.retries += 1
                        # Abstract chunk retransmission timer. It includes a return-path allowance.
                        timer = max(self.spec.get("retry_floor_us", 300), 2*e["delay_us"] + 200)
                        def retry():
                            self.send(mid, eid, size, kind, chunk, callback, attempt+1)
                            self.finish_transfer(mid)
                        self.event(self.now + timer, retry)
                    else:
                        self.finish_transfer(mid)
                else:
                    def arrived():
                        callback()
                        self.finish_transfer(mid)
                    self.event(self.now + e["delay_us"] + extra,
                               lambda: self.cpu_job(e["dst"], kind, b, packets, arrived))
            self.flows[identity] = Flow(b, tuple(e["resources"]), self.spec.get("control_weight", 1) if kind != "data" else 1,
                                        crossed, self.now, eid, mid, kind, chunk, size)
        def enqueue():
            self.stream_queues[stream].append(start)
            self.start_stream(stream)
        self.cpu_job(e["src"], kind, b, packets, enqueue)

    def start_stream(self, stream):
        if stream in self.stream_busy or not self.stream_queues[stream]:
            return
        if stream not in self.stream_initialized:
            self.stream_initialized.add(stream)
            setup = self.topo.edges[stream[1]].get("setup_us", 0)
            if setup:
                self.stream_busy.add(stream)
                def ready():
                    self.stream_busy.remove(stream)
                    self.start_stream(stream)
                self.event(self.now+setup, ready)
                return
        self.stream_busy.add(stream)
        self.stream_queues[stream].popleft()()

    def control(self, mid, node):
        if (mid, node) in self.control_seen:
            return
        self.control_seen.add((mid, node))
        if node in self.spec["targets"]:
            self.messages[mid]["control"][node] = self.now
            if self.spec.get("fallback_us") is not None:
                self.event(self.now+self.spec["fallback_us"], lambda: self.request_missing(mid, node))
        if self.spec.get("control_mode", "direct") == "tree":
            for eid in self.children_for(mid, node):
                dst = self.topo.edges[eid]["dst"]
                self.send(mid, eid, 256, "metadata", 0, lambda dst=dst: self.control(mid, dst))

    def request_missing(self, mid, node):
        missing = sorted(set(range(len(self.parts))) - self.received[mid, node])
        if not missing:
            return
        adaptive = self.spec.get("adaptive")
        if adaptive:
            if self.repair_rounds[mid,node] >= self.spec.get("repair_round_limit", 3):
                return
            last = self.last_progress.get((mid,node), self.messages[mid]["control"].get(node, self.now))
            stall = self.spec.get("repair_stall_us", self.spec["fallback_us"])
            if self.spec.get("progress_sensitive", True) and self.now-last < stall-1e-8:
                self.event(last+stall, lambda: self.request_missing(mid,node))
                return
            self.repair_rounds[mid,node] += 1
        root = self.spec["source"]
        request = self.topo.direct(node, root, self.spec)
        response = self.topo.direct(root, node, self.spec)
        self.repairs += 1
        def repair():
            # The root sees the requested snapshot, not omniscient live receiver state.
            self.observe_stall(mid, node)
            holder, chosen = root, response
            if adaptive:
                # Complete-copy receipts are the root's possession evidence.
                candidates=[]
                for h in sorted({root, *self.messages[mid]["confirmed"]} - {node}):
                    for e in self.topo.pairs[h,node]:
                        if not self.topo.legal(e,self.spec) or self.edge_suspected(e):
                            continue
                        needed=sum(self.parts[c] for c in missing)
                        latency=self.topo.delay(e,needed,self.spec)
                        cost=self.topo.cost(e,needed,self.spec["chunk_bytes"],self.spec["mtu"])
                        if h!=root:
                            command=self.topo.edges[self.topo.direct(root,h,self.spec)]
                            latency+=self.topo.delay(command,64+4*len(missing),self.spec)
                            cost+=self.topo.cost(command,64+4*len(missing),self.spec["chunk_bytes"],self.spec["mtu"])
                        candidates.append((h,e["id"],cost,latency))
                if candidates:
                    fastest=min(c[3] for c in candidates)
                    holder,chosen,_,_=min((c for c in candidates if c[3]<=fastest*(1+self.spec.get("planner_slack",.1))),
                                          key=lambda c:(c[2],c[3],c[0],c[1]))
            def transmit():
                # Holder possession is checked locally after the command arrives.
                for c in missing:
                    if c in self.received[mid,holder]:
                        self.send(mid, chosen, self.parts[c], "data", c, lambda c=c: self.arrive(mid, node, c))
            if holder==root:
                transmit()
            else:
                command=self.topo.direct(root,holder,self.spec)
                self.send(mid,command,64+4*len(missing),"repair_command",0,transmit)
            if adaptive:
                self.repair_path[mid,node]=chosen
                self.controller_events.append(dict(time_us=self.now,kind="repair",message=mid,node=node,
                                                   holder=holder,edge=chosen,chunks=len(missing)))
        self.send(mid, request, 64 + 4*len(missing), "repair_request", 0, repair)
        if adaptive:
            retry = self.spec.get("repair_retry_us", 2*self.spec["fallback_us"])
            self.event(self.now+retry,lambda: self.request_missing(mid,node))

    def arrive(self, mid, node, chunk):
        if chunk in self.received[mid, node]:
            self.duplicates += 1
            return
        self.received[mid, node].add(chunk)
        self.last_progress[mid,node] = self.now
        m = self.messages[mid]
        m["first"].setdefault(node, self.now)
        if len(self.received[mid, node]) == len(self.parts):
            m["delivered"][node] = self.now
            if node in self.spec["targets"]:
                eid = self.topo.direct(node, self.spec["source"], self.spec)
                self.send(mid, eid, 128, "receipt", 0, lambda: self.receipt(mid,node))
        for eid in self.children_for(mid,node):
            dst = self.topo.edges[eid]["dst"]
            self.send(mid, eid, self.parts[chunk], "data", chunk, lambda dst=dst: self.arrive(mid, dst, chunk))

    def release(self, mid, time):
        root = self.spec["source"]
        self.messages[mid] = dict(release_us=time, first={}, delivered={}, confirmed={}, control={})
        if len(self.active_objects) >= self.spec.get("max_inflight_objects", math.inf):
            self.messages[mid]["rejected"] = True
            self.rejected += 1
            return
        self.active_objects.add(mid)
        self.peak_active = max(self.peak_active,len(self.active_objects))
        self.route_versions[mid] = dict(self.route)
        if self.spec.get("mode") == "fetch":
            client = self.spec["targets"][0]
            def serve():
                def ready():
                    for c in range(len(self.parts)):
                        self.arrive(mid, root, c)
                self.event(self.now+self.topo.nodes[root].get("serve_us", 0), ready)
            self.send(mid, self.topo.direct(client, root, self.spec), 128, "fetch_request", 0, serve)
            return
        if self.spec.get("control_mode", "direct") == "direct":
            for node in self.spec["targets"]:
                self.send(mid, self.topo.direct(root, node, self.spec), 256, "metadata", 0,
                          lambda node=node: self.control(mid, node))
        else:
            self.control(mid, root)
        for c in range(len(self.parts)):
            self.arrive(mid, root, c)

    def run(self):
        rng = random.Random(self.seed)
        count = self.spec["messages"]
        times = [0.0]
        for i in range(1, count):
            gap = rng.expovariate(self.spec["rate_per_s"]) * 1e6
            if self.spec.get("burst"):
                # Exogenous periodic rate modulation; arrival sequence fixed per seed/policy.
                gap /= 3 if (i // 16) % 2 else .6
            times.append(times[-1] + gap)
        for mid, t in enumerate(times):
            self.event(t, lambda mid=mid, t=t: self.release(mid, t))
        # External capacity changes are applied by the environment. The routing
        # controller sees only resulting observations, not this future schedule.
        for change in self.spec.get("capacity_events", []):
            def update(change=change):
                r=change["resource"]
                self.capacity[r] *= change["factor"]
                assert self.capacity[r] > 0
            self.event(change["time_us"],update)
        while self.events or self.flows:
            self.rates = fair_rates(self.flows, self.capacity) if self.flows else {}
            net_time = min((self.now + f.remaining / self.rates[i] for i, f in self.flows.items()), default=math.inf)
            event_time = self.events[0][0] if self.events else math.inf
            next_time = min(net_time, event_time)
            assert math.isfinite(next_time) and next_time >= self.now - 1e-7
            dt = max(0, next_time-self.now)
            resource_rates = defaultdict(float)
            for i, f in self.flows.items():
                sent = min(f.remaining, self.rates[i]*dt)
                f.remaining -= sent
                for r in f.resources:
                    self.used[r] += sent
                    resource_rates[r] += self.rates[i]
            for r, rate in resource_rates.items():
                assert rate <= self.capacity[r]*(1+1e-8), (r, rate, self.capacity[r])
                self.peak[r] = max(self.peak[r], rate/self.capacity[r])
            self.now = next_time
            completed = [i for i, f in self.flows.items() if f.remaining <= 1e-6]
            callbacks = [self.flows.pop(i).complete for i in completed]
            for fn in callbacks:
                fn()
            while self.events and self.events[0][0] <= self.now + 1e-8:
                _, _, fn = heapq.heappop(self.events)
                fn()
        rows = []
        targets = self.spec["targets"]
        for mid, m in self.messages.items():
            completed = all(n in m["delivered"] for n in targets)
            confirmed = all(n in m["confirmed"] for n in targets)
            latency = max(m["delivered"][n] for n in targets)-m["release_us"] if completed else None
            remote = [m["delivered"][n] for n in targets if n in m["delivered"] and
                      self.topo.nodes[n]["zone"] != self.topo.nodes[self.spec["source"]]["zone"]]
            rows.append(dict(message=mid, release_us=m["release_us"], last_us=latency,
                             rejected=bool(m.get("rejected")),
                             first_us=min((m["first"][n] for n in targets if n in m["first"]), default=math.inf)-m["release_us"],
                             first_remote_copy_us=min(remote)-m["release_us"] if remote else None,
                             confirmed_us=max(m["confirmed"][n] for n in targets)-m["release_us"] if confirmed else None,
                             deadline_met=completed and latency <= self.deadline,
                             delivered_targets=sum(n in m["delivered"] for n in targets),
                             control_us=max(m["control"].values())-m["release_us"] if len(m["control"]) == len(targets) else None))
        return dict(rows=rows, costs=dict(self.costs), bytes=dict(self.bytes), wire_bytes=sum(self.bytes.values()),
                    rejected=self.rejected, peak_active=self.peak_active,
                    reserved_payload_bytes=self.peak_active*self.spec["size_bytes"]*(1+len(self.spec["targets"])),
                    controller_events=self.controller_events, final_route=dict(self.route),
                    packets=self.packets, requests=len(self.requests), retries=self.retries, drops=self.drops,
                    duplicates=self.duplicates, repairs=self.repairs, edge_bytes=dict(self.edge_bytes),
                    resource_bytes=dict(self.used), resource_peak=dict(self.peak), cpu_us=dict(self.cpu_us),
                    drain_us=self.now, offered_us=times[-1], trace=self.trace,
                    node_times=self.messages.get(0, {}))
