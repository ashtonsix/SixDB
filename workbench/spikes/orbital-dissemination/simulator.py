"""Deterministic finite-resource network experiment, not a protocol implementation.

Time is microseconds; bandwidth is bytes/us (1250 = 10 Gbit/s). A node is
a physical host, so roles/shards naming the same node share every resource.
The simulator uses application callbacks to express actual completion DAGs.
"""
from __future__ import annotations

from collections import Counter, defaultdict, deque
from dataclasses import dataclass, field
import heapq
import math
import random
from typing import Callable


class Sim:
    def __init__(self, seed=1):
        self.now = 0.0
        self.events = []
        self.serial = 0
        self.rng = random.Random(seed)
        self.count = Counter()
        self.resources = {}

    def at(self, when, action):
        if when < self.now - 1e-9:
            raise ValueError("event scheduled in the past")
        self.serial += 1
        heapq.heappush(self.events, (when, self.serial, action))

    def later(self, delay, action):
        self.at(self.now + delay, action)

    def run(self, until):
        while self.events and self.events[0][0] <= until:
            self.now, _, action = heapq.heappop(self.events)
            action()
        self.now = until


class Resource:
    """Nonpreemptive work-conserving queue; capacity includes active work.

    A reserved amount excludes bulk/repair from the last credits, while ordinary
    traffic can use them. Control has priority at dequeue; one in eight service
    selections serves noncontrol when present to avoid unconditional starvation.
    """
    def __init__(self, sim, name, slots=1, capacity=1 << 20, reserve=0):
        self.sim, self.name = sim, name
        self.slots, self.capacity, self.reserve = slots, capacity, reserve
        self.active = 0
        self.used = 0
        self.queues = [deque(), deque()]
        self.high = 0
        self.busy_us = 0.0
        self.wait_us = 0.0
        self.served = 0
        self.turn = 0
        self.active_ends = {}
        sim.resources[name] = self

    def submit(self, duration, charge, done, kind="data", reject=None, begin=None):
        charge = max(1, charge)
        cap = self.capacity - (self.reserve if kind in ("bulk", "repair") else 0)
        if charge > cap or self.used + charge > cap:
            self.sim.count["overflow:" + self.name] += 1
            if reject:
                reject()
            return False
        self.used += charge
        self.high = max(self.high, self.used)
        self.queues[0 if kind == "control" else 1].append(
            (self.sim.now, max(0.0, duration), charge, done, begin))
        self._pump()
        return True

    def _pump(self):
        while self.active < self.slots and any(self.queues):
            self.turn += 1
            q = (self.queues[1] if self.queues[1] and
                 (not self.queues[0] or self.turn % 8 == 0) else self.queues[0])
            arrived, duration, charge, done, begin = q.popleft()
            self.active += 1
            self.wait_us += self.sim.now - arrived
            self.busy_us += duration
            self.served += 1
            job = self.served
            self.active_ends[job] = self.sim.now + duration

            def finish(charge=charge, done=done, job=job):
                self.active -= 1
                self.used -= charge
                del self.active_ends[job]
                done()
                self._pump()

            self.sim.later(duration, finish)
            if begin:
                begin()


@dataclass
class Node:
    name: str
    zone: str = "a"
    cpu_slots: int = 2
    cpu_packet_us: float = 0.35
    cpu_byte_us: float = 0.00002
    nic_bytes_us: float = 1250.0
    memory_bytes_us: float = 20000.0
    persist_us: float = 10.0
    persist_bytes_us: float = 2000.0
    persist_slots: int = 8
    queue_bytes: int = 1 << 20
    reserve_bytes: int = 0
    down: bool = False
    incarnation: int = 0
    cpu_slow: float = 1.0
    persist_slow: float = 1.0


@dataclass
class Edge:
    latency_us: float = 8.0
    jitter_us: float = 0.0
    loss: float = 0.0
    duplicate: float = 0.0
    price_per_gb: float = 0.0
    pool: str | None = None
    # Optional *additional* store-and-forward bottleneck after the host NIC.
    # Do not give every edge its own phantom copy of a shared uplink.
    blocked: bool = False
    delay_extra_us: float = 0.0


@dataclass
class Transfer:
    number: int
    src: str
    dst: str
    size: int
    done: Callable
    kind: str
    reliable: bool
    born: float
    retries: int
    delivered: bool = False
    acked: bool = False
    attempts: int = 0
    src_incarnation: int = 0
    delivered_incarnation: int = -1


class Network:
    def __init__(self, sim, nodes, edges, *, mtu=1400, header=80,
                 batch_bytes=0, batch_wait_us=0.0, batch_capacity=1 << 20,
                 retry_us=400.0, max_retries=2, pools=None):
        self.sim = sim
        self.nodes = {n.name: n for n in nodes}
        self.edges = edges
        self.mtu, self.header = mtu, header
        if mtu <= header or retry_us <= 0:
            raise ValueError("invalid MTU or retry interval")
        self.batch_bytes, self.batch_wait_us = batch_bytes, batch_wait_us
        self.batch_capacity = batch_capacity
        self.retry_us, self.max_retries = retry_us, max_retries
        self.pending = defaultdict(list)
        self.pending_bytes = Counter()
        self.reassembly_bytes = Counter()
        self.transfer_serial = 0
        self.transfers = []
        self.pool = {}
        self.pool_rates = {}
        self.crash_times = {}
        for name, spec in (pools or {}).items():
            self.pool_rates[name] = spec["bytes_us"]
            self.pool[name] = Resource(sim, "fabric:" + name,
                                       capacity=spec.get("queue_bytes", 1 << 20))
        self.cpu, self.tx, self.rx, self.mem, self.disk, self.disk_bytes = {}, {}, {}, {}, {}, {}
        for n in nodes:
            opts = dict(capacity=n.queue_bytes, reserve=n.reserve_bytes)
            self.cpu[n.name] = Resource(sim, n.name + ":cpu", n.cpu_slots, **opts)
            self.tx[n.name] = Resource(sim, n.name + ":tx", **opts)
            self.rx[n.name] = Resource(sim, n.name + ":rx", **opts)
            self.mem[n.name] = Resource(sim, n.name + ":memory", **opts)
            self.disk[n.name] = Resource(sim, n.name + ":disk", n.persist_slots, **opts)
            self.disk_bytes[n.name] = Resource(sim, n.name + ":disk_bytes", **opts)

    def edge(self, src, dst):
        if src == dst:
            return Edge(latency_us=0.1)
        return self.edges[(src, dst)]

    def alive(self, name, incarnation=None):
        n = self.nodes[name]
        return not n.down and (incarnation is None or incarnation == n.incarnation)

    def crash(self, name):
        n = self.nodes[name]
        self.crash_times[name, n.incarnation] = self.sim.now
        n.down = True
        n.incarnation += 1
        self.sim.count["crashes"] += 1

    def recover(self, name):
        self.nodes[name].down = False

    def compute(self, node, duration, size, done, kind="data", reject=None):
        incarnation = self.nodes[node].incarnation
        if not self.alive(node):
            self.sim.count["compute_on_dead"] += 1
            if reject:
                reject()
            return False

        def finish():
            if self.alive(node, incarnation):
                done()
            else:
                self.sim.count["compute_lost_on_crash"] += 1
                if reject:
                    reject()

        return self.cpu[node].submit(duration * self.nodes[node].cpu_slow,
                                      size, finish, kind, reject)

    def persist(self, node, size, done, kind="data"):
        n = self.nodes[node]
        incarnation = n.incarnation
        if n.down:
            self.sim.count["persist_on_dead"] += 1
            return
        byte_duration = size / n.persist_bytes_us * n.persist_slow
        completion_duration = n.persist_us * n.persist_slow

        def finish():
            if self.alive(node, incarnation):
                self.sim.count["persisted_bytes"] += size
                done()
            else:
                self.sim.count["persist_lost_on_crash"] += 1

        def bytes_done():
            if self.alive(node, incarnation):
                self.disk[node].submit(completion_duration, size, finish, kind)
            else:
                self.sim.count["persist_lost_on_crash"] += 1

        # Completion concurrency cannot multiply the device's byte bandwidth.
        self.disk_bytes[node].submit(byte_duration, size, bytes_done, kind)

    def send(self, src, dst, size, done, kind="data", reliable=True):
        if size < 0:
            raise ValueError("negative size")
        self.transfer_serial += 1
        t = Transfer(self.transfer_serial, src, dst, size, done, kind, reliable,
                     self.sim.now, self.max_retries,
                     src_incarnation=self.nodes[src].incarnation)
        self.transfers.append(t)
        self.sim.count["logical_messages"] += 1
        self._attempt(t)
        return t

    def _attempt(self, t):
        if t.acked:
            return
        if not self.alive(t.src, t.src_incarnation):
            self.sim.count["sender_unavailable"] += 1
            return  # No magical successor possessing this sender's bytes.
        t.attempts += 1
        if t.attempts > 1:
            self.sim.count["retry_messages"] += 1
        self._enqueue(t)
        if t.reliable:
            # Sender learns only through ACK/timeout. Loss is never an oracle.
            def timeout():
                if t.acked:
                    return
                if t.attempts <= t.retries:
                    self._attempt(t)
                else:
                    self.sim.count["retry_exhausted"] += 1
            self.sim.later(self.retry_us * (2 ** (t.attempts - 1)), timeout)

    def _enqueue(self, t):
        if not self.batch_bytes or t.size >= self.batch_bytes:
            self._batch([t])
            return
        key = (t.src, t.dst, t.kind)
        if self.pending_bytes[t.src] + t.size + 16 > self.batch_capacity:
            self.sim.count["overflow:batch:" + t.src] += 1
            return
        q = self.pending[key]
        if q and sum(x.size + 16 for x in q) + t.size + 16 > self.batch_bytes:
            self._flush(key)
            q = self.pending[key]
        if not q:
            # Capture this list so an old timer cannot flush a later batch.
            self.sim.later(self.batch_wait_us,
                           lambda key=key, q=q: self._flush(key, q))
        q.append(t)
        self.pending_bytes[t.src] += t.size + 16
        if sum(x.size + 16 for x in q) >= self.batch_bytes:
            self._flush(key)

    def _flush(self, key, expected=None):
        q = self.pending[key]
        if expected is not None and q is not expected:
            return
        if not q:
            return
        self.pending[key] = []
        self.pending_bytes[key[0]] -= sum(t.size + 16 for t in q)
        self._batch(q)

    def _batch(self, transfers):
        valid = [t for t in transfers if not t.acked and self.alive(t.src, t.src_incarnation)]
        self.sim.count["discarded_stale_batch_messages"] += sum(
            not self.alive(t.src, t.src_incarnation) for t in transfers)
        transfers = valid
        if not transfers:
            return
        src, dst = transfers[0].src, transfers[0].dst
        kind = transfers[0].kind
        # Framing retains transfer identity even when several streams coalesce.
        payload = sum(t.size + 16 for t in transfers)
        packet_payload = self.mtu - self.header
        lengths = [min(packet_payload, payload - off)
                   for off in range(0, max(1, payload), packet_payload)]
        remaining = set(range(len(lengths)))
        src_inc, dst_inc = transfers[0].src_incarnation, self.nodes[dst].incarnation
        assembly = {"started": False, "expired": False}
        edge = self.edge(src, dst)
        rates = ([self.nodes[src].memory_bytes_us] if src == dst else
                 [self.nodes[src].nic_bytes_us, self.nodes[dst].nic_bytes_us])
        if edge.pool:
            rates.append(self.pool_rates[edge.pool])
        assembly_timeout = self.retry_us + (payload + len(lengths) * self.header) / min(rates)

        def expire():
            if assembly["started"] and remaining and not assembly["expired"]:
                assembly["expired"] = True
                self.reassembly_bytes[dst] -= payload
                self.sim.count["reassembly_expired"] += 1

        def fragment(index):
            if assembly["expired"]:
                return
            if index not in remaining:
                self.sim.count["duplicate_packets"] += 1
                return
            if not assembly["started"]:
                if self.reassembly_bytes[dst] + payload > self.nodes[dst].queue_bytes:
                    self.sim.count["overflow:reassembly:" + dst] += 1
                    assembly["expired"] = True
                    return
                assembly["started"] = True
                self.reassembly_bytes[dst] += payload
                # Permit at least the unloaded serialization of this whole batch,
                # while retaining a finite timeout for missing fragments.
                self.sim.later(assembly_timeout, expire)
            remaining.remove(index)
            if remaining:
                return
            self.reassembly_bytes[dst] -= payload
            self.sim.count["received_batches"] += 1
            for t in transfers:
                if not t.delivered or t.delivered_incarnation != dst_inc:
                    t.delivered = True
                    t.delivered_incarnation = dst_inc
                    self.sim.count["delivered_messages"] += 1
                    t.done()
                else:
                    self.sim.count["duplicate_messages"] += 1
                if t.reliable:
                    def acknowledge(t=t):
                        if self.alive(t.src, t.src_incarnation):
                            t.acked = True
                    self.send(dst, src, 24, acknowledge,
                              "control", reliable=False)

        for index, length in enumerate(lengths):
            size = length if src == dst else length + self.header
            self._packet(src, dst, size, kind,
                         lambda index=index: fragment(index), src_inc, dst_inc)

    def _packet(self, src, dst, size, kind, received, src_inc, dst_inc):
        n, peer = self.nodes[src], self.nodes[dst]
        edge = self.edge(src, dst)
        packet = {"departed": False, "start": None}

        def receive_complete():
            if not packet["departed"] or not self.alive(dst, dst_inc):
                self.sim.count["lost_packets"] += 1
                return
            self.compute(dst, peer.cpu_packet_us + peer.cpu_byte_us * size,
                         size, received, kind)

        def arrive(upstream_rate):
            if (not self.alive(dst, dst_inc) or edge.blocked or
                    self.sim.rng.random() < edge.loss):
                self.sim.count["lost_packets"] += 1
                return
            # A packet's first byte arrives after propagation. Its remaining
            # serialization overlaps TX, while every receiver shares a finite RX.
            duration = max(size / upstream_rate, size / peer.nic_bytes_us)
            self.rx[dst].submit(duration, size, receive_complete, kind)
            if self.sim.rng.random() < edge.duplicate:
                self.sim.count["injected_duplicate_packets"] += 1
                self.rx[dst].submit(duration, size, receive_complete, kind)

        def wave(upstream_rate):
            delay = edge.latency_us + edge.delay_extra_us
            if edge.jitter_us:
                delay += self.sim.rng.expovariate(1.0 / edge.jitter_us)
            self.sim.later(delay, lambda: arrive(upstream_rate))

        def tx_begin():
            if self.alive(src, src_inc):
                packet["start"] = self.sim.now
                self.sim.count["packets"] += 1
                if not edge.pool:
                    wave(n.nic_bytes_us)

        def tx_done():
            start = packet["start"]
            if start is None:
                self.sim.count["lost_packets"] += 1
                return
            stop = min(self.sim.now, self.crash_times.get((src, src_inc), self.sim.now))
            sent_bytes = min(size, max(0, stop - start) * n.nic_bytes_us)
            self.sim.count["wire_bytes"] += sent_bytes
            self.sim.count["bytes:" + kind] += sent_bytes
            self.sim.count["price"] += sent_bytes * edge.price_per_gb / 1e9
            if not self.alive(src, src_inc):
                self.sim.count["lost_packets"] += 1
                return
            packet["departed"] = True
            if edge.pool:
                # An explicitly modeled store-and-forward hop follows host TX.
                # Its queue rejection cannot erase the bytes already transmitted.
                rate = self.pool_rates[edge.pool]
                self.pool[edge.pool].submit(size / rate, size, lambda: None, kind,
                                            begin=lambda: wave(rate))

        def memory_done():
            if not self.alive(src, src_inc):
                self.sim.count["lost_packets"] += 1
                return
            self.sim.count["memory_bytes"] += size
            self.sim.count["bytes:" + kind] += size
            packet["departed"] = True
            self.sim.later(edge.latency_us, receive_complete)

        def transmit():
            if src == dst:
                self.mem[src].submit(size / n.memory_bytes_us, size, memory_done, kind)
            else:
                self.tx[src].submit(size / n.nic_bytes_us, size, tx_done, kind,
                                    begin=tx_begin)

        self.compute(src, n.cpu_packet_us + n.cpu_byte_us * size,
                     size, transmit, kind)

    def inject(self, faults):
        """Schedule declarative faults; only timers/ACKs reveal losses to senders."""
        for f in faults:
            def start(f=f):
                kind = f["kind"]
                if kind == "crash":
                    self.crash(f["node"])
                elif kind == "cpu_slow":
                    self.nodes[f["node"]].cpu_slow = f["factor"]
                elif kind == "disk_slow":
                    self.nodes[f["node"]].persist_slow = f["factor"]
                elif kind == "edge_delay":
                    self.edges[(f["src"], f["dst"])].delay_extra_us = f["delay_us"]
                elif kind == "partition":
                    self.edges[(f["src"], f["dst"])].blocked = True
                elif kind == "loss":
                    self.edges[(f["src"], f["dst"])].loss = f["probability"]
                else:
                    raise ValueError("unknown fault " + kind)
            def end(f=f):
                kind = f["kind"]
                if kind == "crash":
                    self.recover(f["node"])
                elif kind == "cpu_slow":
                    self.nodes[f["node"]].cpu_slow = 1.0
                elif kind == "disk_slow":
                    self.nodes[f["node"]].persist_slow = 1.0
                elif kind == "edge_delay":
                    self.edges[(f["src"], f["dst"])].delay_extra_us = 0.0
                elif kind == "partition":
                    self.edges[(f["src"], f["dst"])].blocked = False
                elif kind == "loss":
                    self.edges[(f["src"], f["dst"])].loss = 0.0
            self.sim.at(f["at_us"], start)
            if "until_us" in f:
                self.sim.at(f["until_us"], end)


class Outcomes:
    def __init__(self, sim):
        self.sim = sim
        self.offered = {}
        self.completed = {}
        self.trace = defaultdict(dict)

    def offer(self, key):
        self.offered[key] = self.sim.now
        self.mark(key, "offered")

    def mark(self, key, stage):
        self.trace[key].setdefault(stage, self.sim.now)

    def finish(self, key):
        if key not in self.completed:
            self.completed[key] = self.sim.now
            self.mark(key, "effect")

    def summary(self, cutoff, drain):
        values = sorted(self.completed[k] - born for k, born in self.offered.items()
                        if k in self.completed)
        total = len(self.offered)
        def quantile(q):
            return values[max(0, math.ceil(q * len(values)) - 1)] if values else None
        def fraction(deadline):
            return sum(k in self.completed and self.completed[k] - born <= deadline
                       for k, born in self.offered.items()) / max(1, total)
        unfinished = [drain - born for k, born in self.offered.items()
                      if k not in self.completed]
        return dict(offered=total, completed=len(values),
                    pending_at_cutoff=sum(self.completed.get(k, math.inf) > cutoff
                                          for k in self.offered),
                    unfinished=len(unfinished), oldest_unfinished_us=max(unfinished, default=0),
                    p50_us=quantile(.5), p99_us=quantile(.99), p999_us=quantile(.999),
                    deadline_170_fraction=fraction(170), deadline_250_fraction=fraction(250),
                    goodput_per_s=sum(t <= cutoff for t in self.completed.values()) /
                    max(cutoff, 1) * 1e6)


def resource_report(sim):
    return {name: dict(high_bytes=r.high, outstanding_bytes=r.used,
                       service_us=r.busy_us - sum(max(0, end - sim.now)
                                                  for end in r.active_ends.values()),
                       scheduled_service_us=r.busy_us,
                       mean_wait_us=r.wait_us / max(1, r.served),
                       jobs=r.served)
            for name, r in sim.resources.items()}
