"""Deterministic, failure-free contention slice. See MODEL.md for its boundary."""

from __future__ import annotations

import copy
import hashlib
import heapq
import json
import math
from collections import Counter, defaultdict
from dataclasses import dataclass, field


POLICIES = ("batch-independent", "batch-hold", "batch-reset")
POLICY_NAMES = {"batch-independent": "Keep each component independently",
                "batch-hold": "Pause all collection for pending verdicts",
                "batch-reset": "Replace reservations at every retry epoch"}


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def identity(value):
    return hashlib.sha256(canonical(value).encode()).hexdigest()


def integer(value, name, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f"{name} must be an integer in [{low}, {high}]")
    return value


def fields(value, allowed, name):
    if not isinstance(value, dict):
        raise ValueError(f"{name} must be an object")
    extra = set(value) - set(allowed.split())
    if extra:
        raise ValueError(f"unsupported {name} fields: {', '.join(sorted(extra))}")


def validate(raw):
    """Normalize a finite authored DAG; reject malformed scenarios at the edge."""
    s = copy.deepcopy(raw)
    fields(s, "version name description generator shards policy control_delay_us horizon_us shard_order transactions", "scenario")
    if not isinstance(s, dict) or s.get("version") != 1:
        raise ValueError("scenario version must be 1")
    if not isinstance(s.get("shards"), dict) or not 1 <= len(s["shards"]) <= 8:
        raise ValueError("supply 1–8 shards")
    for name, config in s["shards"].items():
        if not isinstance(name, str) or not name or not isinstance(config, dict):
            raise ValueError("invalid shard")
        fields(config, "period_us offset_us capacity", "shard")
        integer(config.setdefault("period_us", 100), "period_us", 1, 1000000)
        integer(config.setdefault("offset_us", 0), "offset_us", 0, 1000000)
        integer(config.setdefault("capacity", 8), "capacity", 1, 1000)
    p = s.setdefault("policy", {})
    fields(p, "yield batch_us arbitration_us solver fast_retries local_solver reservations", "policy")
    if p.setdefault("yield", "batch-independent") not in POLICIES:
        raise ValueError(f"yield must be one of {POLICIES}")
    integer(p.setdefault("batch_us", 500), "batch_us", 1, 1000000)
    integer(p.setdefault("arbitration_us", 1200), "arbitration_us", 0, 1000000)
    if p.setdefault("solver", "age") not in ("age", "work", "bounded"):
        raise ValueError("solver must be age, work or bounded")
    if type(p.setdefault("fast_retries", True)) is not bool:
        raise ValueError("fast_retries must be boolean")
    if type(p.setdefault("local_solver", False)) is not bool:
        raise ValueError("local_solver must be boolean")
    if p.setdefault("reservations", "compatible") not in ("compatible", "scope"):
        raise ValueError("reservations must be compatible or scope")
    integer(s.setdefault("control_delay_us", 100), "control_delay_us", 0, 1000000)
    integer(s.setdefault("horizon_us", 10000), "horizon_us", 1, 10000000)
    order = s.setdefault("shard_order", sorted(s["shards"]))
    if not isinstance(order, list) or sorted(order) != sorted(s["shards"]):
        raise ValueError("shard_order must list every shard exactly once")
    txs = s.get("transactions")
    if not isinstance(txs, list) or not 1 <= len(txs) <= 400:
        raise ValueError("supply 1–400 transactions")
    ids = set()
    total_parts = 0
    for t in txs:
        fields(t, "id kind coordinator arrival_us parts", "transaction")
        name = t.get("id")
        if not isinstance(name, str) or not name or name in ids:
            raise ValueError("transaction IDs must be unique nonempty strings")
        ids.add(name)
        if t.setdefault("kind", "C") not in ("L", "C"):
            raise ValueError("transaction kind must be L or C")
        if t.get("coordinator") not in s["shards"]:
            raise ValueError(f"unknown coordinator for {name}")
        integer(t.setdefault("arrival_us", 0), "arrival_us", 0, 10000000)
        parts = t.get("parts")
        if not isinstance(parts, list) or not 1 <= len(parts) <= 16:
            raise ValueError("supply 1–16 parts per transaction")
        total_parts += len(parts)
        if total_parts > 1600:
            raise ValueError("at most 1600 parts per scenario")
        for part in parts:
            fields(part, "id shard locks after work", "part")
        names = [x.get("id") for x in parts]
        if any(not isinstance(x, str) or not x for x in names) or len(set(names)) != len(names):
            raise ValueError("part IDs must be unique nonempty strings")
        for part in parts:
            if part.get("shard") not in s["shards"]:
                raise ValueError("unknown part shard")
            deps = part.setdefault("after", [])
            if not isinstance(deps, list) or len(set(deps)) != len(deps) or any(x not in names for x in deps):
                raise ValueError("invalid dependency")
            locks = part.get("locks")
            if not isinstance(locks, dict) or not locks or len(locks) > 32:
                raise ValueError("a part needs 1–32 key locks")
            if any(not isinstance(k, str) or not k or v not in ("R", "W") for k, v in locks.items()):
                raise ValueError("locks map nonempty key names to R or W")
            integer(part.setdefault("work", 1), "work", 1, 1000000)
        visited = set()
        while True:
            fresh = {x["id"] for x in parts if set(x["after"]) <= visited} - visited
            if not fresh:
                break
            visited |= fresh
        if visited != set(names):
            raise ValueError(f"dependency cycle in {name}")
        if t["kind"] == "L" and (len(parts) != 1 or parts[0]["after"]):
            raise ValueError("L must have exactly one root part")
    return s


@dataclass
class Part:
    spec: dict
    state: str = "hidden"
    generation: int = 0
    attempts: int = 0
    failures: int = 0
    retry_epoch: int = 0
    applied: int = 0
    inputs: dict = field(default_factory=dict)


@dataclass
class Transaction:
    spec: dict
    parts: dict
    state: str = "future"
    known: set = field(default_factory=set)
    reported: dict = field(default_factory=dict)
    c2_ready: set = field(default_factory=set)
    completed_us: int | None = None


class Model:
    def __init__(self, scenario):
        self.scenario = validate(scenario)
        self.time = 0
        self.steps = 0
        self.epochs = {s: 0 for s in self.scenario["shards"]}
        self.rotation = {s: 0 for s in self.epochs}
        self.transactions = {
            t["id"]: Transaction(t, {p["id"]: Part(p) for p in t["parts"]})
            for t in self.scenario["transactions"]
        }
        self.events = []
        self.serial = 0
        self.inbox = defaultdict(list)
        self.log = []
        self.counts = Counter()
        self.grants = set()
        self.last_epoch = None
        self.initial_cycle_us = None
        # Equal-time epochs consume the same pre-epoch arrivals, then follow this
        # authored order. It is part of the replay input, not thread scheduling.
        for rank, shard in enumerate(self.scenario["shard_order"]):
            self.schedule(self.scenario["shards"][shard]["offset_us"], "epoch", shard, rank)
        for tid, tx in self.transactions.items():
            self.schedule(tx.spec["arrival_us"], "arrival", tid)

    def schedule(self, time, kind, payload, rank=0):
        self.serial += 1
        heapq.heappush(self.events, (time, 1 if kind == "epoch" else 0, rank, self.serial, kind, payload))

    def note(self, event_type, **fields):
        self.log.append({"time_us": self.time, "type": event_type, **fields})
        self.counts[event_type] += 1

    def send(self, shard, kind, **fields):
        self.schedule(self.time + self.scenario["control_delay_us"], "message", (shard, {"kind": kind, **fields}))
        self.counts["control_messages"] += 1

    def part(self, ref):
        return self.transactions[ref[0]].parts[ref[1]]

    def priority(self, tid):
        return self.transactions[tid].spec["arrival_us"], tid

    def conflict(self, a, b, same_transaction=True):
        if same_transaction and a[0] == b[0]:
            return False
        pa, pb = self.part(a).spec, self.part(b).spec
        return pa["shard"] == pb["shard"] and any(
            key in pb["locks"] and (mode == "W" or pb["locks"][key] == "W")
            for key, mode in pa["locks"].items()
        )

    def blockers(self, ref, epoch_grants=()):
        return {b for b in self.grants | set(epoch_grants) if self.conflict(ref, b)}

    def fence_blockers(self, ref):
        return set()

    def descendants(self, tid, roots):
        result = set(roots)
        parts = self.transactions[tid].parts
        while True:
            expanded = result | {pid for pid, p in parts.items() if set(p.spec["after"]) & result}
            if expanded == result:
                return result
            result = expanded

    def work(self, tid, ids=None):
        parts = self.transactions[tid].parts
        return sum(p.spec["work"] for pid, p in parts.items()
                   if p.state == "prepared" and (ids is None or pid in ids))

    def invalidate(self, tid, root_ids, reason):
        tx = self.transactions[tid]
        if tx.state != "preparing":
            return False
        roots = set(root_ids)
        # A verdict can name several blocking parts. If one depends on another,
        # only the ancestor is a restart root; descendants must be rediscovered.
        roots = {r for r in roots if not any(r in self.descendants(tid, [a]) for a in roots if a != r)}
        affected = self.descendants(tid, roots)
        discarded = self.work(tid, affected)
        self.counts["discarded_work"] += discarded
        for pid in sorted(affected):
            p = tx.parts[pid]
            p.generation += 1
            p.state = "ready" if pid in roots else "hidden"
            p.inputs.clear()
            p.retry_epoch = self.epochs[p.spec["shard"]] + 1
            self.grants.discard((tid, pid))
            tx.reported.pop(pid, None)
            tx.known.discard(pid)
        tx.known |= roots
        self.note("invalidate", transaction=tid, parts=sorted(affected), discarded_work=discarded, reason=reason)
        return True

    def report(self, msg):
        tid, pid = msg["transaction"], msg["part"]
        tx, p = self.transactions[tid], self.part((tid, pid))
        if (tx.state != "preparing" or p.generation != msg["generation"]
                or p.state != "prepared" or pid not in tx.known):
            self.note("stale_report", transaction=tid, part=pid)
            return
        # Successor registration and completion observation are one atomic input.
        children = {cid for cid, child in tx.parts.items() if pid in child.spec["after"]}
        tx.known |= children
        tx.reported[pid] = p.generation
        for cid in sorted(children | tx.known):
            child = tx.parts[cid]
            if child.state == "hidden" and all(tx.reported.get(d) == tx.parts[d].generation for d in child.spec["after"]):
                child.state = "ready"
                self.note("discover", transaction=tid, part=cid, shard=child.spec["shard"])
        self.note("report", transaction=tid, part=pid, generation=p.generation)
        if all(tx.reported.get(k) == tx.parts[k].generation for k in tx.known):
            assert len(tx.known) == len(tx.parts), "discovery closure violated"
            tx.state = "authorized"
            self.note("authorize", transaction=tid)
            for shard in sorted({p.spec["shard"] for p in tx.parts.values()}):
                self.send(shard, "c2", transaction=tid)

    def retry(self, ref, blockers):
        p = self.part(ref)
        p.failures += 1
        p.retry_epoch = self.epochs[p.spec["shard"]] + 1
        self.note("retry", transaction=ref[0], part=ref[1],
                  blockers=[list(b) for b in sorted(blockers)],
                  reservations=[list(key) for key in sorted(self.fence_blockers(ref))], next_epoch=p.retry_epoch)

    def complete(self, tid):
        tx = self.transactions[tid]
        if all(p.state == "applied" for p in tx.parts.values()):
            tx.state = "complete"
            tx.completed_us = self.time
            self.note("complete", transaction=tid, latency_us=self.time - tx.spec["arrival_us"])

    def acquire(self, ref, epoch_grants):
        """One conservative key-protection realization, not a required L lifecycle.

        L attempts leave only epoch-local conflict accounting, never retained
        grants. folds.py separately probes a payload fold that combines L work.
        """
        tid, pid = ref
        tx, p = self.transactions[tid], self.part(ref)
        p.attempts += 1
        blocked = self.blockers(ref, epoch_grants)
        if blocked or self.fence_blockers(ref):
            self.retry(ref, blocked)
            return
        p.failures = 0
        p.inputs = {d: tx.parts[d].generation for d in p.spec["after"]}
        epoch_grants.add(ref)
        self.counts["executed_work"] += p.spec["work"]
        self.note("prepare" if tx.spec["kind"] == "C" else "local", transaction=tid, part=pid, generation=p.generation)
        if tx.spec["kind"] == "L":
            p.state = "applied"
            p.applied += 1
            self.complete(tid)
        else:
            p.state = "prepared"
            self.grants.add(ref)
            self.send(tx.spec["coordinator"], "report", transaction=tid, part=pid, generation=p.generation)

    def wait_edges(self):
        edges = []
        for tid, tx in sorted(self.transactions.items()):
            if tx.state != "preparing":
                continue
            for pid, p in sorted(tx.parts.items()):
                if p.state == "ready":
                    for blocker in sorted(self.wait_blockers((tid, pid))):
                        edges.append({"from": tid, "to": blocker[0], "part": pid,
                                      "blocked_by": blocker[1], "shard": p.spec["shard"]})
        return edges

    def wait_blockers(self, ref):
        return self.blockers(ref)

    def eligible(self, ref):
        return self.part(ref).retry_epoch <= self.epochs[self.part(ref).spec["shard"]]

    def handle_event(self, kind, payload):
        return False

    def cycles(self, edges=None):
        """Return nontrivial strongly connected components, deterministically."""
        graph = defaultdict(set)
        for edge in self.wait_edges() if edges is None else edges:
            graph[edge["from"]].add(edge["to"])
        index, low, stack, on_stack, result = {}, {}, [], set(), []

        def visit(v):
            index[v] = low[v] = len(index)
            stack.append(v)
            on_stack.add(v)
            for w in sorted(graph.get(v, ())):
                if w not in index:
                    visit(w)
                    low[v] = min(low[v], low[w])
                elif w in on_stack:
                    low[v] = min(low[v], index[w])
            if low[v] == index[v]:
                component = []
                while True:
                    w = stack.pop()
                    on_stack.remove(w)
                    component.append(w)
                    if w == v:
                        break
                if len(component) > 1:
                    result.append(sorted(component))

        for vertex in sorted(set(graph) | {v for vs in graph.values() for v in vs}):
            if vertex not in index:
                visit(vertex)
        return sorted(result)

    def epoch(self, shard):
        self.epochs[shard] += 1
        messages, self.inbox[shard] = self.inbox[shard], []
        for msg in messages:
            if msg["kind"] == "report":
                self.report(msg)
            elif msg["kind"] == "c2":
                tx = self.transactions[msg["transaction"]]
                if tx.state == "authorized":
                    tx.c2_ready.add(shard)
            else:
                raise AssertionError("unknown message kind")
            self.check()
        queues = {"L": [], "C1": [], "C2": []}
        for tid, tx in self.transactions.items():
            for pid, p in tx.parts.items():
                if p.spec["shard"] != shard:
                    continue
                if tx.state == "preparing" and p.state == "ready" and self.eligible((tid, pid)):
                    queues["L" if tx.spec["kind"] == "L" else "C1"].append((tid, pid))
                elif tx.state == "authorized" and shard in tx.c2_ready and p.state == "prepared":
                    queues["C2"].append((tid, pid))
        kinds = ("L", "C1", "C2")
        kind = "idle"
        for i in range(3):
            k = (self.rotation[shard] + i) % 3
            if queues[kinds[k]]:
                kind = kinds[k]
                self.rotation[shard] = (k + 1) % 3
                break
        attempted = []
        if kind != "idle":
            # This model charges one slot per C2 transaction on this shard;
            # it is a capacity assumption, not an Orbital execution mechanism.
            ordered = sorted(queues[kind], key=lambda ref: (self.priority(ref[0]), ref[1]))
            capacity = self.scenario["shards"][shard]["capacity"]
            epoch_grants = set()
            if kind == "C2":
                txids = list(dict.fromkeys(ref[0] for ref in ordered))[:capacity]
                for tid in txids:
                    for ref in (r for r in ordered if r[0] == tid):
                        p = self.part(ref)
                        p.state = "applied"
                        p.applied += 1
                        self.grants.remove(ref)
                        attempted.append(list(ref))
                        self.note("apply", transaction=tid, part=ref[1], shard=shard)
                    self.complete(tid)
            else:
                for ref in ordered[:capacity]:
                    self.acquire(ref, epoch_grants)
                    attempted.append(list(ref))
        cycles = self.cycles()
        if cycles and self.initial_cycle_us is None:
            self.initial_cycle_us = self.time
            self.note("cycle", components=cycles)
        self.last_epoch = {"shard": shard, "epoch": self.epochs[shard], "kind": kind, "attempted": attempted}
        self.note("epoch", **self.last_epoch)
        period = self.scenario["shards"][shard]["period_us"]
        self.schedule(self.time + period, "epoch", shard, self.scenario["shard_order"].index(shard))

    def step(self):
        """Consume through one shard epoch; virtual time and ties are authored."""
        if all(tx.state == "complete" for tx in self.transactions.values()):
            return False
        while self.events:
            if self.events[0][0] > self.scenario["horizon_us"]:
                self.time = self.scenario["horizon_us"]
                return False
            time, _, _, _, kind, payload = heapq.heappop(self.events)
            self.time = time
            if kind == "arrival":
                tx = self.transactions[payload]
                tx.state = "preparing"
                tx.known = {pid for pid, p in tx.parts.items() if not p.spec["after"]}
                for pid in tx.known:
                    tx.parts[pid].state = "ready"
                self.note("arrival", transaction=payload)
            elif kind == "message":
                shard, msg = payload
                self.inbox[shard].append(msg)
            elif kind == "epoch":
                self.epoch(payload)
                self.steps += 1
            elif not self.handle_event(kind, payload):
                raise AssertionError("unknown event kind")
            self.check()
            if kind == "epoch":
                return True
        return False

    def check(self):
        """Runtime assertions concern this model, never an unmodeled protocol."""
        def exclusive(refs, grants):
            by_key = defaultdict(list)
            for ref in sorted(refs):
                p = self.part(ref)
                for key, mode in p.spec["locks"].items():
                    for other, other_mode in by_key[p.spec["shard"], key]:
                        assert grants and (ref[0] == other[0] or mode == other_mode == "R"), (ref, other, key)
                    by_key[p.spec["shard"], key].append((ref, mode))
        exclusive(self.grants, True)
        for tid, tx in self.transactions.items():
            assert set(tx.reported) <= tx.known
            if tx.state in ("authorized", "complete") and tx.spec["kind"] == "C":
                assert len(tx.known) == len(tx.parts)
                assert all(tx.reported.get(pid) == p.generation for pid, p in tx.parts.items())
            for pid, p in tx.parts.items():
                assert ((tid, pid) in self.grants) == (p.state == "prepared")
                assert p.applied == (1 if p.state == "applied" else 0)
                if p.state in ("prepared", "applied"):
                    assert all(p.inputs.get(d) == tx.parts[d].generation for d in p.spec["after"])
                if p.state == "ready":
                    assert all(tx.reported.get(d) == tx.parts[d].generation for d in p.spec["after"])
                if tx.state == "complete":
                    assert p.state == "applied"

    def summary(self):
        states = Counter(t.state for t in self.transactions.values())
        latencies = sorted(t.completed_us - t.spec["arrival_us"] for t in self.transactions.values() if t.completed_us is not None)
        arrived = [t for t in self.transactions.values() if t.state != "future"]
        pending = [t for t in arrived if t.state != "complete"]
        return {
            "time_us": self.time, "epochs": self.steps,
            "completed": states["complete"], "pending": len(pending), "future": states["future"],
            "completion_fraction": states["complete"] / len(arrived) if arrived else None,
            "p50_completed_us": latencies[(len(latencies) - 1) // 2] if latencies else None,
            "p99_completed_us": latencies[math.ceil(.99 * len(latencies)) - 1] if latencies else None,
            "oldest_pending_us": max((self.time - t.spec["arrival_us"] for t in pending), default=0),
            "first_cycle_us": self.initial_cycle_us,
            "cycles": self.cycles(), "counts": dict(sorted(self.counts.items())),
        }

    def snapshot(self):
        edges = self.wait_edges()
        return {
            "time_us": self.time, "step": self.steps, "last_epoch": self.last_epoch,
            "epochs": dict(self.epochs), "log_position": len(self.log), "summary": self.summary(),
            "wait_edges": edges,
            "transactions": [{"id": tid, "kind": tx.spec["kind"], "state": tx.state,
                              "known": sorted(tx.known), "reported": dict(tx.reported),
                              "completed_us": tx.completed_us,
                              "parts": [{"id": pid, "shard": p.spec["shard"], "state": p.state,
                                         "generation": p.generation, "attempts": p.attempts,
                                         "retry_epoch": p.retry_epoch,
                                         "locks": p.spec["locks"]} for pid, p in sorted(tx.parts.items())]}
                             for tid, tx in sorted(self.transactions.items())],
        }


def run(scenario, max_steps=2000, frames=False):
    integer(max_steps, "max_steps", 1, 10000)
    from batch import BatchModel
    model = BatchModel(scenario)
    history = [model.snapshot()] if frames else []
    stride = 1
    while model.steps < max_steps and model.step():
        if frames and model.steps % stride == 0:
            history.append(model.snapshot())
            if len(history) > 300:
                stride *= 2
                history = [frame for frame in history if frame["step"] % stride == 0]
    final = model.snapshot()
    if frames and (history[-1]["step"], history[-1]["time_us"]) != (final["step"], final["time_us"]):
        history.append(final)
    reason = "complete" if final["summary"]["completed"] == len(model.transactions) else (
        "horizon" if model.time >= model.scenario["horizon_us"] else "step_limit")
    return {"version": 1, "scenario": model.scenario, "scenario_sha256": identity(model.scenario),
            "stop_reason": reason, "max_steps": max_steps, "summary": model.summary(),
            "trace_sha256": identity(model.log), "trace": model.log,
            "final": final, "frame_stride": stride, "frames": history}
