"""Execution under offered load, through the same physical World as other probes.

Prepared, single shard authorities are an explicit boundary: journals here test
execution recovery after power loss, NOT quorum admission or authority transfer.
Scopes and integer programs belong to this application binding, not sim.py.
All protocol requests, receipts, reads and outcome installs use actor ports.
"""
from __future__ import annotations

from collections import defaultdict, deque
from dataclasses import asdict, dataclass, replace
import math

from kernel import clone, digest
from sim import Actor, Host, Link, World, encoded_size


@dataclass(frozen=True)
class Strategy:
    ordering: str = "mv"           # mv, protection, shard
    window: int = 16                # independent per client lane
    lanes: str = "origin"           # origin, class
    batch: int = 8
    batch_ns: int = 2_000
    quantum_ns: int = 20_000
    retry_ns: int = 200_000
    backoff: bool = True
    jitter: bool = True
    wake_waiters: bool = True
    read_floor_first: bool = True
    supersede: bool = False        # application-proved complete cell replacement
    negative: str | None = None


@dataclass(frozen=True)
class Config:
    workload: str = "points"
    topology: str = "man"
    count: int = 80
    interval_ns: int = 40_000
    start_ns: int = 100_000
    drain_ns: int = 12_000_000
    width: int = 16
    shards: int = 3
    slow_every: int = 20
    slow_ns: int = 500_000
    burst: int = 1
    memory_bytes: int = 2 << 20
    durable_bytes: int = 32 << 20
    workers: int = 2
    handler_ns: int = 100
    metadata_ns_per_entry: int = 5
    disk_latency: int = 8_000
    bandwidth: float = 2
    loss: float = 0
    duplicate: float = 0
    point_rmw: bool = False
    incident: str = "none"
    severity: float = 1
    fault_at_ns: int = 1_000_000
    fault_duration_ns: int = 1_000_000


def send(ctx, target, kind, data):
    return ctx.send(target, kind, data, size=encoded_size(data) + 48)


def shard_of(key):
    return int(key.split(":")[0][1:])


def group(keys):
    result = defaultdict(list)
    for key in keys:
        result[shard_of(key)].append(key)
    return dict(sorted(result.items()))


def initial_state(config):
    return {f"s{s}:k{k}": 100 + k for s in range(config.shards)
            for k in range(config.width)} | {
                f"s{s}:answer": 0 for s in range(config.shards)}


def workload(config, seed=1):
    """Offered identities and contents are independent of physical scheduling."""
    plans = []
    for i in range(config.count):
        local = i % min(config.shards, 2)
        key = f"s{local}:k{int(digest([seed, 'key', i])[:8], 16) % config.width}"
        broad = i % config.slow_every == 0
        p = dict(id=i + 1, at=config.start_ns + (i // config.burst) * config.interval_ns,
                 origin=local, cohort="point", program="set", reads=[], writes=[key],
                 value=i + 1000, compute=2_000, source_version=1)
        name = config.workload
        if config.point_rmw:
            p.update(program="increment", reads=[key])
        if name == "hot":
            key = f"s{local}:k0"
            p.update(program="increment", reads=[key], writes=[key], cohort="hot")
        elif name in ("scan", "global-scan", "max-update", "bulk-rmw", "bulk-blind", "conditional", "bridge", "overwrite") and broad:
            keys = [f"s{local}:k{k}" for k in range(config.width)]
            if name == "global-scan":
                keys = [f"s{s}:k{k}" for s in range(config.shards) for k in range(config.width)]
            p.update(reads=keys, compute=config.slow_ns, cohort="wide")
            if name in ("scan", "global-scan"):
                p.update(program="sum", writes=[f"s{local}:answer"])
            elif name == "max-update":
                p.update(program="max", writes=keys)
            elif name in ("bulk-rmw", "overwrite"):
                p.update(program="increment", writes=keys)
            elif name == "bulk-blind":
                p.update(program="set", reads=[], writes=keys)
            elif name == "conditional":
                p.update(program="conditional", writes=keys)
            else:
                p.update(program="increment", reads=[keys[0], keys[1]],
                         writes=[keys[0], keys[1]])
        elif name == "bridge":
            key = f"s{local}:k{1 if i % 3 else 0}"
            p.update(program="increment", reads=[key], writes=[key])
        elif name == "overwrite":
            key = f"s{local}:k0"
            p.update(program="increment" if i % 4 < 2 else "set", writes=[key],
                     reads=[key] if i % 4 < 2 else [])
        elif name in ("wan-mix", "transfer", "wan-independent") and broad:
            a, b = f"s{local}:k0", f"s{config.shards - 1}:k0"
            p.update(program="transfer", reads=[a, b], writes=[a, b],
                     compute=config.slow_ns, cohort="remote")
        elif name == "transfer":
            a, b = f"s{local}:k0", f"s{1-local}:k0"
            p.update(program="transfer", reads=[a, b], writes=[a, b], cohort="transfer")
        elif name == "wan-mix":
            key = f"s{local}:k0"
            p.update(program="increment", reads=[key], writes=[key])
        elif name == "wan-independent":
            key = f"s{local}:k{1 + i % (config.width - 1)}"
            p.update(program="increment", reads=[key], writes=[key])
        elif name not in ("points", "scan", "global-scan", "max-update", "bulk-rmw", "bulk-blind",
                           "conditional", "bridge", "hot", "overwrite"):
            raise ValueError(f"unknown workload {name}")
        plans.append(p)
    return plans


class Shard(Actor):
    """Journaled metadata transitions with persistent, idempotent request IDs.

    Only one journal write is in flight. Recovery is supported after a device
    reset, which cancels old in-flight writes; arbitrary process-only restart
    would require fencing/reconciliation and is deliberately not claimed here.
    """
    def __init__(self, index, initial, strategy, metadata_ns_per_entry=5):
        self.index, self.strategy = index, strategy
        self.versions = {k: [(0, v)] for k, v in initial.items() if shard_of(k) == index}
        self.tickets, self.reads, self.bounds, self.responses = {}, {}, {}, {}
        self.requests, self.waiting = {}, {}
        self.queue, self.queued = deque(), set()
        self.ready = self.writing = self.flush_pending = False
        self.sequence = 0
        self.leases = {}
        self.metadata_ns_per_entry = metadata_ns_per_entry

    def reply(self, ctx, request, result):
        send(ctx, request["reply"], "response", dict(request=request["request"],
             tx=request["tx"], shard=self.index, kind=request["kind"], **result))

    def blocked(self, request):
        tx, kind = request["tx"], request["kind"]
        if kind == "acquire":
            scopes = set(request["locks"])
            for other, t in self.tickets.items():
                held = not t.get("resolved") and (
                    self.strategy.ordering != "mv" or not t.get("released"))
                if other != tx and held and scopes.intersection(t["locks"]):
                    return f"allocation ticket {other}"
        return None

    def transition(self, request):
        tx, kind, rid = request["tx"], request["kind"], request["request"]
        if rid in self.responses:
            return self.responses[rid]
        if kind == "acquire":
            self.waiting[rid] = request
            return None
        elif kind == "announce":
            ticket = self.tickets[tx]
            minimum = 1 + max([0] + [self.bounds.get(k, 0) for k in ticket["locks"]]
                          + [p for k in ticket["locks"] for p, _ in self.versions[k]]
                          + [t.get("position", 0) for t in self.tickets.values()
                             if set(t["writes"]) & set(ticket["locks"])])
            ticket["minimum"] = minimum
            result = dict(minimum=minimum)
        elif kind == "fix":
            ticket = self.tickets[tx]
            if request["position"] < ticket["minimum"]:
                raise ValueError("position below announced minimum")
            ticket["position"] = request["position"]
            result = {}
        elif kind == "release":
            ticket = self.tickets[tx]
            if "position" not in ticket:
                raise ValueError("release before fixed position")
            ticket["released"] = True
            result = {}
        elif kind == "floor":
            minimum = max([0] + [p for k in request["keys"] for p, _ in self.versions[k]]
                          + [t.get("position", t.get("minimum", 0)) for t in self.tickets.values()
                             if set(t["writes"]) & set(request["keys"])])
            result = dict(minimum=minimum)
        elif kind == "read":
            for k in request["keys"]:
                self.bounds[k] = max(self.bounds.get(k, 0), request["position"])
            self.reads[rid] = request
            return None
        elif kind == "resolve":
            ticket = self.tickets[tx]
            if set(request["values"]) - set(ticket["writes"]):
                raise ValueError("effect escaped announced coverage")
            ticket["resolved"] = True
            for k, value in request["values"].items():
                self.versions[k].append((ticket["position"], value))
                self.versions[k].sort()
            result = {}
        else:
            raise ValueError(kind)
        self.responses[rid] = result
        return result

    def observe_at(self, tx, position, keys):
        """Inspect a cut without registering bounds or changing logical state.

        The caller must already have established the read's ordering/retention
        obligations. Ordinary reads do that in transition(); private queries
        require an independently established context before using this helper.
        """
        blockers = []
        for other, ticket in self.tickets.items():
            overlap = set(ticket["writes"]) & set(keys)
            if other == tx or ticket.get("resolved") or not overlap or "minimum" not in ticket:
                continue
            if ticket.get("position", ticket["minimum"]) > position:
                continue
            replaced = self.strategy.supersede and "position" in ticket and all(
                any(ticket["position"] < p <= position for p, _ in self.versions[k]) for k in overlap)
            if not replaced:
                blockers.append(other)
        if blockers and self.strategy.negative != "ignore-pending":
            return dict(pending=blockers)
        return dict(values={k: max((p, v) for p, v in self.versions[k] if p <= position)[1]
                            for k in keys})

    def settle(self):
        # Queue order is part of agreed state. A blocked broad waiter prevents
        # overtaking on any of its scopes, but disjoint work can still pass it.
        earlier = set()
        for rid, request in list(self.waiting.items()):
            scopes = set(request["locks"])
            if self.blocked(request) or scopes & earlier:
                earlier.update(scopes)
                continue
            self.tickets[request["tx"]] = dict(writes=request["writes"], locks=request["locks"])
            self.responses[rid] = {}
            del self.waiting[rid]
        for rid, request in self.reads.items():
            if rid in self.responses:
                continue
            observation = self.observe_at(request["tx"], request["position"], request["keys"])
            if "values" in observation:
                self.responses[rid] = observation

    def apply(self, request):
        """Fold one agreed input and its enabled continuations, without I/O."""
        rid = request["request"]
        if rid in self.requests:
            if self.requests[rid] != request:
                raise ValueError("request identity reused with different contents")
            return [(request, self.responses[rid])] if rid in self.responses else []
        previous = set(self.responses)
        self.requests[rid] = request
        self.transition(request)
        self.settle()
        return [(self.requests[key], value) for key, value in self.responses.items() if key not in previous]

    def logical_state(self):
        return clone(dict(versions=self.versions, tickets=self.tickets, reads=self.reads,
                          bounds=self.bounds, responses=self.responses, requests=self.requests,
                          waiting=list(self.waiting.values())))

    def deliver(self, ctx, outputs, trigger=None):
        for request, result in outputs:
            rid = request["request"]
            ctx.clear_wait(f"request/{rid}")
            if request["kind"] == "read":
                ctx.clear_wait(f"read/{rid}")
                ctx.note("traffic_read", op=request["tx"], shard=self.index,
                         position=request["position"], values=result["values"])
            # Timer-only delivery retains identical queue/ordering semantics;
            # only the notification of an already granted waiter is withheld.
            if (not self.strategy.wake_waiters and request["kind"] == "acquire"
                    and trigger is not None and rid != trigger):
                continue
            self.reply(ctx, request, result)

    def report_waits(self, ctx):
        for rid, request in self.waiting.items():
            ctx.wait(f"request/{rid}", "held scope or earlier conflicting waiter", tx=request["tx"])
        for rid, request in self.reads.items():
            if rid not in self.responses:
                ctx.wait(f"read/{rid}", "announced pending output", tx=request["tx"])

    def on(self, ctx, kind, data):
        if kind == "charged":
            ctx.release(data["lease"])
            return self.handle(ctx, data["kind"], data["data"])
        entries = 0
        if kind == "request" and data["kind"] == "floor" and self.ready:
            entries = len(self.tickets) + sum(len(self.versions[k]) for k in data["keys"])
        elif kind == "journaled":
            pending = [r for rid, r in self.reads.items() if rid not in self.responses]
            entries = len(pending) * len(self.tickets) + sum(
                len(self.versions[k]) for r in pending for k in r["keys"])
            entries += sum(len(r.get("keys", [])) * (len(self.tickets) + 1) for r in data["batch"])
        elif kind == "recovered" and data["ok"]:
            entries = sum(len(batch) for batch in data["records"].values()) * (len(self.versions) + 1)
        cost = entries * self.metadata_ns_per_entry
        if cost:
            lease = ctx.reserve(encoded_size(data) + 32, "metadata-continuation")
            if lease is not None and ctx.compute(cost, "charged", dict(kind=kind, data=data, lease=lease),
                                                 pool="control", leases=(lease,)):
                ctx.note("traffic_metadata_work", entries=entries, cost=cost)
                return
            if lease is not None:
                ctx.release(lease)
            ctx.timer(100_000, kind, data)
            return
        return self.handle(ctx, kind, data)

    def handle(self, ctx, kind, data):
        if kind in ("boot", "boot-retry"):
            if not ctx.scan("journal/", "recovered"):
                ctx.timer(100_000, "boot-retry")
        elif kind == "recovered":
            if not data["ok"]:
                ctx.timer(100_000, "boot-retry")
                return
            records = sorted(data["records"].items(), key=lambda kv: int(kv[0].split("/")[1]))
            for key, requests in records:
                self.sequence = max(self.sequence, int(key.split("/")[1]))
                for request in requests:
                    self.apply(request)
            allowance = 1024 + encoded_size(data["records"]) + encoded_size(self.versions)
            lease = ctx.reserve(allowance, "recovered-shard-history")
            if lease is None:
                ctx.wait("recovery", "resident history capacity")
                return
            self.leases["base"] = lease
            self.ready = True
            ctx.note("traffic_recovered", records=len(records))
            self.report_waits(ctx)
        elif kind == "request" and self.ready:
            rid = data["request"]
            if rid in self.responses:
                self.reply(ctx, data, self.responses[rid])
            elif rid not in self.queued and rid not in self.requests:
                lease = ctx.reserve(encoded_size(data) + 96, "retained-journal-metadata")
                if lease is None:
                    ctx.wait(f"request/{rid}", "metadata memory")
                    return
                self.leases[rid] = lease
                self.queue.append(data)
                self.queued.add(rid)
                if len(self.queue) >= self.strategy.batch:
                    self.flush(ctx)
                elif not self.flush_pending:
                    self.flush_pending = True
                    ctx.timer(self.strategy.batch_ns, "flush")
        elif kind == "flush":
            self.flush_pending = False
            self.flush(ctx)
        elif kind == "journaled":
            self.writing = False
            if not data["ok"]:
                raise ValueError("accepted journal operation failed without device reset")
            for request in data["batch"]:
                rid = request["request"]
                self.queued.remove(rid)
                ctx.clear_wait(f"request/{rid}")
                outputs = self.apply(request)
                ctx.note("traffic_transition", op=request["tx"], shard=self.index,
                         transition=request["kind"], request=request)
                self.deliver(ctx, outputs, request["request"])
            ctx.note("traffic_epoch", epoch=self.sequence, shard=self.index,
                     logical_hash=digest(self.logical_state()))
            self.report_waits(ctx)
            self.flush(ctx)
        elif kind == "metadata-ready":
            self.writing = False
            self.persist_batch(ctx, data["batch"])

    def flush(self, ctx):
        if self.writing or not self.queue:
            return
        batch = []
        # Persist offered commands in input order, including blocked requests.
        # The deterministic fold owns grant ordering and survives a restart.
        for _ in range(len(self.queue)):
            if len(batch) >= self.strategy.batch:
                break
            request = self.queue.popleft()
            batch.append(request)
        if not batch:
            return
        # Charge an explicit coarse linear traversal model, separately from the
        # Python implementation's deep copy. Sensitivity varies this coefficient;
        # it is not a measurement of a production metadata structure.
        entries = len(batch) * (len(self.tickets) + len(self.bounds) + 1) + len(self.reads)
        entries += sum(len(r.get("keys", [])) + len(r.get("locks", [])) for r in batch)
        cost = entries * self.metadata_ns_per_entry
        if cost and ctx.compute(cost, "metadata-ready", dict(batch=batch), pool="control"):
            self.writing = True
            ctx.note("traffic_metadata_work", entries=entries, cost=cost)
        elif cost:
            self.queue.extendleft(reversed(batch))
            if not self.flush_pending:
                self.flush_pending = True
                ctx.timer(100_000, "flush")
        else:
            self.persist_batch(ctx, batch)

    def persist_batch(self, ctx, batch):
        number = self.sequence + 1
        if ctx.persist(f"journal/{number}", batch, "journaled", dict(batch=batch)):
            self.sequence = number
            self.writing = True
        else:
            self.queue.extendleft(reversed(batch))
            ctx.wait("journal", "persistent capacity or device queue")
            if not self.flush_pending:
                self.flush_pending = True
                ctx.timer(max(100_000, self.strategy.retry_ns), "flush")


def effects(plan, values):
    name = plan["program"]
    if name == "set":
        return {k: plan["value"] for k in plan["writes"]}
    if name == "increment":
        return {k: values[k] + 1 for k in plan["writes"]}
    if name == "sum":
        return {plan["writes"][0]: sum(values.values())}
    if name == "max":
        key = max(values, key=lambda k: (values[k], k))
        return {key: values[key] + 1}
    if name == "conditional":
        return {k: values[k] + 1 for k in plan["writes"]} if values[plan["reads"][0]] % 2 == 0 else {}
    if name == "transfer":
        a, b = plan["reads"]
        return {a: values[a] - 1, b: values[b] + 1} if values[a] > 0 else {}
    raise ValueError(name)


class Coordinator(Actor):
    def __init__(self, strategy, config):
        self.strategy, self.config = strategy, config
        self.states, self.ready = {}, False

    def setup(self, plan, lease):
        locks = group(plan["writes"] if self.strategy.ordering == "mv"
                      else sorted(set(plan["reads"] + plan["writes"])))
        if self.strategy.ordering == "shard":
            keys = initial_state(self.config)
            locks = {s: [k for k in keys if shard_of(k) == s] for s in locks}
        return dict(plan=plan, lease=lease, locks=locks, writes=group(plan["writes"]),
                    reads=group(plan["reads"]), acquired=set(), announced={}, fixed=set(), floors={}, released=set(),
                    values={}, read_done=set(), installed=set(), phase="input",
                    attempt=0, generation=0, pending=False)

    def request(self, ctx, state, shard, kind, **fields):
        tx = state["plan"]["id"]
        rid = f"{tx}/{kind}/{shard}"
        send(ctx, f"shard{shard}", "request", dict(request=rid, tx=tx,
             reply=ctx.actor, kind=kind, **fields))

    def retry_delay(self, state):
        s = self.strategy
        multiplier = min(32, 2 ** min(5, state["attempt"])) if s.backoff else 1
        jitter = .75 + int(digest([state["plan"]["id"], state["attempt"]])[:8], 16) / 2**33 if s.jitter else 1
        return max(1, math.ceil(s.retry_ns * multiplier * jitter))

    def advance(self, ctx, state):
        p, phase = state["plan"], state["phase"]
        tx = p["id"]
        if phase == "input":
            if not state["pending"]:
                state["pending"] = ctx.persist(f"input/{tx}", p, "input-stored", dict(tx=tx))
        elif phase == "acquire":
            remaining = [s for s in state["locks"] if s not in state["acquired"]]
            if remaining:
                s = remaining[0]
                self.request(ctx, state, s, "acquire", locks=state["locks"][s], writes=state["writes"].get(s, []))
            else:
                state["phase"] = "announce"
                return self.advance(ctx, state)
        elif phase == "announce":
            for s in state["locks"]:
                if s not in state["announced"]:
                    self.request(ctx, state, s, "announce")
            if len(state["announced"]) == len(state["locks"]):
                state["phase"] = "floor"
                return self.advance(ctx, state)
        elif phase == "floor":
            for s, keys in state["reads"].items():
                if s not in state["floors"]:
                    self.request(ctx, state, s, "floor", keys=keys)
            if len(state["floors"]) == len(state["reads"]):
                if len(state["acquired"]) < len(state["locks"]):
                    state["phase"] = "acquire"
                    return self.advance(ctx, state)
                minimum = max([0] + list(state["announced"].values()) + list(state["floors"].values()))
                state["position"] = (minimum // 100_000 + 1) * 100_000 + tx
                state["phase"] = "prepare"
                return self.advance(ctx, state)
        elif phase == "prepare":
            if not state["pending"]:
                state["pending"] = ctx.persist(f"position/{tx}", state["position"], "position-stored", dict(tx=tx))
        elif phase == "fix":
            for s in state["locks"]:
                if s not in state["fixed"]:
                    self.request(ctx, state, s, "fix", position=state["position"])
            if len(state["fixed"]) == len(state["locks"]):
                state["phase"] = "release"
                return self.advance(ctx, state)
        elif phase == "release":
            for s in state["locks"]:
                if s not in state["released"]:
                    self.request(ctx, state, s, "release")
            if len(state["released"]) == len(state["locks"]):
                state["phase"] = "read"
                return self.advance(ctx, state)
        elif phase == "read":
            for s, keys in state["reads"].items():
                if s not in state["read_done"]:
                    self.request(ctx, state, s, "read", keys=keys, position=state["position"])
            if len(state["read_done"]) == len(state["reads"]):
                state["remaining"] = p["compute"]
                state["phase"] = "compute"
                return self.advance(ctx, state)
        elif phase == "compute":
            if not state["pending"]:
                quantum = min(state["remaining"], self.strategy.quantum_ns or state["remaining"])
                state["pending"] = ctx.compute(quantum, "computed", dict(tx=tx, quantum=quantum), leases=(state["lease"],))
        elif phase == "decide":
            if not state["pending"]:
                outcome = dict(position=state["position"], values=effects(p, state["values"]), observed=state["values"])
                state["pending"] = ctx.persist(f"outcome/{tx}", outcome, "outcome-stored", dict(tx=tx, outcome=outcome))
        elif phase == "install":
            for s in state["locks"]:
                if s not in state["installed"]:
                    self.request(ctx, state, s, "resolve", values={k: v for k, v in state["outcome"]["values"].items() if shard_of(k) == s})
            if len(state["installed"]) == len(state["locks"]):
                state["phase"] = "done"
                ctx.clear_wait(tx)
                ctx.note("traffic_complete", op=tx, position=state["position"], outcome=state["outcome"])
                send(ctx, "client", "done", dict(tx=tx))
                ctx.release(state["lease"])
                receipt = ctx.reserve(128, "completion-receipt")
                if receipt is None:
                    raise ValueError("released transaction state cannot hold its smaller receipt")
                state.clear()
                state.update(plan=dict(id=tx), phase="done", generation=0,
                             attempt=0, lease=receipt)
                return
        elif phase == "done":
            send(ctx, "client", "done", dict(tx=tx))
            return
        ctx.wait(tx, state["phase"])

    def arm(self, ctx, state):
        state["generation"] += 1
        ctx.timer(self.retry_delay(state), "retry", dict(tx=state["plan"]["id"], generation=state["generation"]))

    def on(self, ctx, kind, data):
        if kind in ("boot", "boot-retry"):
            if not ctx.scan("", "recovered"):
                ctx.timer(100_000, "boot-retry")
            return
        if kind == "recovered":
            if not data["ok"]:
                ctx.timer(100_000, "boot-retry")
                return
            records = data["records"]
            for key, p in records.items():
                if not key.startswith("input/"):
                    continue
                tx = p["id"]
                lease = ctx.reserve(encoded_size(p) + 512, "transaction-state")
                if lease is None:
                    ctx.wait("recovery", "transaction memory")
                    return
                state = self.setup(p, lease)
                state["phase"] = "floor" if self.strategy.read_floor_first else "acquire"
                if f"position/{tx}" in records:
                    state.update(position=records[f"position/{tx}"], phase="fix")
                if f"outcome/{tx}" in records:
                    state.update(outcome=records[f"outcome/{tx}"], phase="install")
                self.states[tx] = state
            self.ready = True
            ctx.note("traffic_recovered", records=len(records))
            for state in self.states.values():
                self.advance(ctx, state)
                self.arm(ctx, state)
            return
        if not self.ready:
            return
        if kind == "submit":
            p, tx = data, data["id"]
            if tx in self.states:
                if self.states[tx]["phase"] == "done":
                    send(ctx, "client", "done", dict(tx=tx))
                return
            lease = ctx.reserve(encoded_size(p) + 512, "transaction-state")
            if lease is None:
                # No durable input or output ticket exists; retryable refusal is
                # explicitly BEFORE acceptance. Client retains the obligation.
                ctx.note("traffic_admission_pressure", op=tx)
                return
            state = self.states[tx] = self.setup(p, lease)
            self.advance(ctx, state)
            self.arm(ctx, state)
            return
        tx = data.get("tx")
        if tx not in self.states:
            return
        state = self.states[tx]
        progress = False
        if kind == "retry":
            if data["generation"] != state["generation"] or state["phase"] == "done":
                return
            state["attempt"] += 1
        elif kind in ("input-stored", "position-stored", "outcome-stored"):
            state["pending"] = False
            if not data["ok"]:
                return
            if kind == "input-stored":
                state["phase"] = "floor" if self.strategy.read_floor_first else "acquire"
                ctx.note("traffic_accepted", op=tx, plan=state["plan"])
            elif kind == "position-stored":
                state["phase"] = "fix"
                ctx.note("traffic_position", op=tx, position=state["position"], writes=state["plan"]["writes"])
            else:
                state["phase"] = "install"
                state["outcome"] = data["outcome"]
                ctx.note("traffic_decision", op=tx, outcome=data["outcome"])
            progress = True
        elif kind == "computed":
            state["pending"] = False
            state["remaining"] -= data["quantum"]
            if state["remaining"] == 0:
                state["phase"] = "decide"
            progress = True
        elif kind == "response":
            s, operation = data["shard"], data["kind"]
            if operation == "acquire" and state["phase"] == "acquire":
                progress = s not in state["acquired"]
                state["acquired"].add(s)
            elif operation == "announce" and state["phase"] == "announce":
                progress = s not in state["announced"]
                state["announced"][s] = data["minimum"]
            elif operation == "floor" and state["phase"] == "floor":
                progress = s not in state["floors"]
                state["floors"][s] = data["minimum"]
            elif operation == "fix" and state["phase"] == "fix":
                progress = s not in state["fixed"]
                state["fixed"].add(s)
            elif operation == "release" and state["phase"] == "release":
                progress = s not in state["released"]
                state["released"].add(s)
            elif operation == "read" and state["phase"] == "read":
                progress = s not in state["read_done"]
                state["read_done"].add(s)
                state["values"].update(data["values"])
            elif operation == "resolve" and state["phase"] == "install":
                progress = s not in state["installed"]
                state["installed"].add(s)
            if not progress:
                return
        else:
            return
        if progress:
            state["attempt"] = 0
        self.advance(ctx, state)
        self.arm(ctx, state)


class Client(Actor):
    def __init__(self, strategy):
        self.strategy = strategy
        self.waiting, self.active, self.plans = defaultdict(deque), {}, {}
        self.leases, self.completed = {}, set()

    def lane(self, p):
        return f'{p["origin"]}/{p["cohort"]}' if self.strategy.lanes == "class" else str(p["origin"])

    def pump(self, ctx):
        for lane, queue in self.waiting.items():
            active = sum(self.lane(self.plans[tx]) == lane for tx in self.active)
            while queue and active < self.strategy.window:
                tx = queue.popleft()
                self.active[tx] = 0
                self.transmit(ctx, tx)
                active += 1

    def transmit(self, ctx, tx):
        plan = self.plans[tx]
        send(ctx, f'coordinator{plan["origin"]}', "submit", plan)
        attempt = self.active[tx]
        self.active[tx] += 1
        scale = min(32, 2 ** min(attempt, 5)) if self.strategy.backoff else 1
        ctx.timer(self.strategy.retry_ns * scale, "retry", dict(tx=tx))

    def on(self, ctx, kind, data):
        if kind == "offer":
            tx = data["id"]
            ctx.note("traffic_offer_received", op=tx)
            lease = ctx.reserve(encoded_size(data) + 96, "client-obligation")
            if lease is None:
                ctx.note("traffic_refused", op=tx)
                return
            self.plans[tx], self.leases[tx] = data, lease
            self.waiting[self.lane(data)].append(tx)
            self.pump(ctx)
        elif kind == "retry" and data["tx"] in self.active:
            self.transmit(ctx, data["tx"])
        elif kind == "done":
            tx = data["tx"]
            if tx in self.completed:
                return
            if tx not in self.active:
                raise ValueError("completion for unoffered transaction")
            self.completed.add(tx)
            self.active.pop(tx)
            ctx.release(self.leases.pop(tx))
            ctx.note("traffic_response", op=tx)
            self.pump(ctx)


def build(config=Config(), strategy=Strategy(), seed=1, replay=None, *, offers=True, factories=None):
    if strategy.ordering not in ("mv", "protection", "shard") or strategy.lanes not in ("origin", "class"):
        raise ValueError("unknown strategy")
    if min(config.count, config.width, config.shards, config.burst, config.slow_every,
           strategy.window, strategy.batch, strategy.retry_ns) <= 0 or config.count >= 100_000:
        raise ValueError("invalid bounded experiment dimensions")
    if config.shards < 2 or config.width < 2:
        raise ValueError("workloads need at least two shards and keys")
    factories = dict(factories or {})
    roles = {"client", "coordinator0", "coordinator1"} | {f"shard{s}" for s in range(config.shards)}
    if set(factories) - roles or any(not callable(factory) for factory in factories.values()):
        raise ValueError("unknown role or invalid actor factory")
    world = World(seed=seed, ordering="shuffle", replay=replay)
    domains = {f"h{s}": ("remote" if s == config.shards - 1 else "local") for s in range(config.shards)}
    domains["client"] = "local"
    for name, domain in domains.items():
        world.add_host(Host(name, domain=domain, workers=config.workers,
                           handler_ns=config.handler_ns, memory_bytes=config.memory_bytes,
                           durable_bytes=config.durable_bytes, disk_latency=config.disk_latency,
                           nic_bytes_per_ns=config.bandwidth, disk_bytes_per_ns=2, io_slots=4, queue_limit=256))
    for a in domains:
        for b in domains:
            if a == b:
                continue
            if config.topology == "lan":
                latency = 500
            elif config.topology == "man":
                latency = 12_000
            elif config.topology == "wan":
                latency = 40_000_000 if domains[a] != domains[b] else 12_000
            elif config.topology == "asymmetric":
                latency = 2_000_000 if domains[a] != domains[b] else 12_000
                if a == f"h{config.shards-1}":
                    latency *= 4
            else:
                raise ValueError("unknown topology")
            world.add_link(Link(a, b, latency=latency, bandwidth=config.bandwidth,
                                loss=config.loss, duplicate=config.duplicate, jitter=latency // 20))
    initial = initial_state(config)
    for s in range(config.shards):
        world.add_actor(f"shard{s}", f"h{s}", factories.get(f"shard{s}",
                        lambda s=s: Shard(s, initial, strategy, config.metadata_ns_per_entry)))
    for s in range(2):
        world.add_actor(f"coordinator{s}", f"h{s}", factories.get(f"coordinator{s}",
                        lambda: Coordinator(strategy, config)))
    world.add_actor("client", "client", factories.get("client", lambda: Client(strategy)))
    plans = workload(config, seed) if offers else []
    for plan in plans:
        world.inject("client", "offer", plan, at=plan["at"])
    start, duration = config.fault_at_ns, int(config.fault_duration_ns * config.severity)
    victim = f"h{config.shards - 1}"
    if config.incident == "partition":
        for h in ("h0", "h1"):
            world.fault(start, "partition", source_host=h, target_host=victim)
            world.fault(start + duration, "partition", source_host=h, target_host=victim, blocked=False)
    elif config.incident == "power":
        world.fault(start, "power_loss", host="h0")
        world.fault(start + duration, "power_on", host="h0")
    elif config.incident in ("disk", "cpu", "control"):
        resource = {"disk": "disk_bytes", "cpu": "workers", "control": "control"}[config.incident]
        world.fault(start, "slowdown", host="h0", resource=resource, factor=max(1, config.severity))
        world.fault(start + config.fault_duration_ns, "slowdown", host="h0", resource=resource, factor=1)
    elif config.incident == "pause":
        world.fault(start, "pause", host="h0", resource="disk_bytes")
        world.fault(start + duration, "pause", host="h0", resource="disk_bytes", paused=False)
    elif config.incident == "flap":
        for i in range(max(1, int(config.severity))):
            at = start + i * config.fault_duration_ns * 2
            world.fault(at, "pause", host="h0", resource="disk_bytes")
            world.fault(at + config.fault_duration_ns, "pause", host="h0", resource="disk_bytes", paused=False)
    elif config.incident != "none":
        raise ValueError("unknown incident")
    last_offer = max((p["at"] for p in plans), default=config.start_ns +
                     ((config.count - 1) // config.burst) * config.interval_ns)
    return world, plans, last_offer + config.interval_ns + config.drain_ns


def audit(world, plans, config):
    """Independent read/outcome reconstruction, not a call to effects()."""
    errors = []
    by_id = {p["id"]: p for p in plans}
    positions, outcomes, fixed, resolved, installed_values = {}, {}, {}, {}, []
    journals, durable_requests, completion_seen = defaultdict(dict), {}, set()
    reads = []

    def applied(request, shard, at):
        tx, kind = request["tx"], request["kind"]
        if kind == "fix":
            pos = request["position"]
            if tx in positions and positions[tx] != pos:
                errors.append(f"position changed {tx}")
            positions[tx] = pos
            fixed[(tx, shard)] = at
        if kind == "resolve":
            resolved[(tx, shard)] = at
            installed_values.append((tx, request["values"]))
            if tx not in outcomes:
                errors.append(f"install before durable decision {tx}")
            else:
                expected = {k: v for k, v in outcomes[tx]["values"].items() if shard_of(k) == shard}
                if request["values"] != expected or positions.get(tx) != outcomes[tx]["position"]:
                    errors.append(f"installed effect differs from decision {tx}/{shard}")

    for event in world.trace:
        kind, tx = event["kind"], event.get("op")
        if kind == "durable_write" and event["key"].startswith("outcome/"):
            tx = int(event["key"].split("/")[1])
            value = event["value"]
            if tx in outcomes and outcomes[tx] != value:
                errors.append(f"conflicting outcome {tx}")
            outcomes[tx] = value
        if kind == "durable_write" and event["actor"].startswith("shard") and event["key"].startswith("journal/"):
            journals[event["actor"]][event["key"]] = event["value"]
            for request in event["value"]:
                durable_requests[request["request"]] = request
        if kind == "traffic_recovered" and event["actor"].startswith("shard"):
            shard = int(event["actor"][5:])
            for _, batch in sorted(journals[event["actor"]].items(), key=lambda kv: int(kv[0].split("/")[1])):
                for request in batch:
                    applied(request, shard, event["time"])
        if kind == "traffic_transition":
            request = event["request"]
            if durable_requests.get(request["request"]) != request:
                errors.append(f"transition not backed by durable journal {request['request']}")
            applied(request, event["shard"], event["time"])
        if kind == "traffic_read":
            reads.append(event)
            for other, pos in positions.items():
                if other != tx and pos <= event["position"] and set(by_id[other]["writes"]) & set(event["values"]):
                    overlap = set(by_id[other]["writes"]) & set(event["values"])
                    superseded = all(any(key in values and pos < positions.get(replacement, 0) <= event["position"]
                                         for replacement, values in installed_values) for key in overlap)
                    if (other, event["shard"]) in fixed and (other, event["shard"]) not in resolved and not superseded:
                        errors.append(f"read {tx} skipped pending {other}")
        if kind == "traffic_complete":
            completion_seen.add(tx)
            if tx not in outcomes:
                errors.append(f"completion without durable outcome {tx}")
            for s in group(by_id[tx]["writes"]):
                if (tx, s) not in resolved:
                    errors.append(f"completion before install {tx}/{s}")
        if kind == "traffic_response" and tx not in completion_seen:
            errors.append(f"client response before durable installed completion {tx}")
    initial = initial_state(config)
    def snapshot(tx, position, key):
        candidates = [(0, initial[key])] + [(v["position"], v["values"][key]) for other, v in outcomes.items()
            if other != tx and v["position"] <= position and key in v["values"]]
        return max(candidates)[1]
    for event in reads:
        for key, observed in event["values"].items():
            if observed != snapshot(event["op"], event["position"], key):
                errors.append(f"wrong snapshot {event['op']}/{key}")
    for tx, outcome in outcomes.items():
        p, v = by_id[tx], outcome["observed"]
        if any(observed != snapshot(tx, outcome["position"], key) for key, observed in v.items()):
            errors.append(f"decision used wrong snapshot {tx}")
        name = p["program"]
        if set(v) != set(p["reads"]):
            errors.append(f"missing observations {tx}")
            continue
        if name == "set": expected = dict.fromkeys(p["writes"], p["value"])
        elif name == "increment": expected = {k: v[k] + 1 for k in p["writes"]}
        elif name == "sum": expected = {p["writes"][0]: sum(v[k] for k in p["reads"])}
        elif name == "max":
            key = sorted(v, key=lambda k: (v[k], k))[-1]
            expected = {key: v[key] + 1}
        elif name == "conditional": expected = {} if v[p["reads"][0]] % 2 else {k: v[k] + 1 for k in p["writes"]}
        elif name == "transfer":
            a, b = p["reads"]
            expected = {a: v[a] - 1, b: v[b] + 1} if v[a] > 0 else {}
        else: raise ValueError(name)
        if outcome["values"] != expected or set(expected) - set(p["writes"]):
            errors.append(f"wrong effects {tx}")
    # Inspect physical retained actor state only here, never in an algorithm.
    for name, actor_state in world.actors.items():
        if not name.startswith("shard") or actor_state.actor is None or not actor_state.actor.ready:
            continue
        shard = actor_state.actor
        for key, versions in shard.versions.items():
            expected = [(0, initial[key])] + [(outcomes[tx]["position"], vals[key]) for tx, vals in installed_values
                if key in vals and tx in outcomes and shard_of(key) == shard.index]
            if sorted(set(versions)) != sorted(set(expected)):
                errors.append(f"retained version history differs {key}")
    return sorted(set(errors))


def evaluate(case, seed, output_path=None):
    from campaigns import Observation, cohort
    config = Config(**case.get("config", {}))
    strategy = Strategy(**case.get("strategy", {}))
    world, plans, until = build(config, strategy, seed)
    try:
        world.run(until=until, max_events=case.get("event_budget", 1_000_000))
    except Exception:
        diagnostics(world, output_path)
        raise
    violations = audit(world, plans, config)
    finished, refused = {}, {}
    for event in world.trace:
        if event["kind"] == "traffic_response": finished.setdefault(str(event["op"]), event["time"])
        if event["kind"] == "traffic_refused": refused.setdefault(str(event["op"]), event["time"])
    end = max(p["at"] for p in plans) + config.interval_ns
    cohorts = {}
    for name in sorted({p["cohort"] for p in plans}):
        offered = {str(p["id"]): p["at"] for p in plans if p["cohort"] == name}
        cohorts[name] = cohort(offered, {k: v for k, v in finished.items() if k in offered},
                              {k: v for k, v in refused.items() if k in offered}, offered_until=end, until=until)
    wire = sum(e["size"] for e in world.trace if e["kind"] == "wire_transmitted")
    report = world.report()
    last = max(finished.values(), default=0)
    metrics = dict(wire_bytes=wire, offered=config.count, completed=len(finished),
        response_rate_per_s=sum(t <= end for t in finished.values()) * 1e9 / (end-config.start_ns),
        drain_ns=max(0, last-end) if len(finished)+len(refused) == len(plans) else None,
        retained_bytes=sum(h.used for h in world.hosts.values()),
        durable_bytes=sum(h.storage_used for h in world.hosts.values()),
        events=len(world.decisions), last_progress_ns=last,
        no_response_for_ns=until-last if len(finished)+len(refused)<len(plans) else 0)
    for name, c in cohorts.items():
        latencies = sorted(c["latency_ns"].values())
        metrics[f"{name}_max_ns"] = latencies[-1] if latencies else None
        metrics[f"{name}_p99_ns"] = latencies[max(0, math.ceil(len(latencies)*.99)-1)] if latencies else None
        metrics[f"{name}_within_500us"] = sum(t <= 500_000 for t in latencies) / c["offered"]
    metrics["backlog_at_offer_end"] = len(plans) - sum(t < end for t in finished.values()) - sum(t < end for t in refused.values())
    metrics["late_window_rate_per_s"] = sum(end - (end-config.start_ns)//2 <= t < end for t in finished.values()) * 2e9 / (end-config.start_ns)
    if violations or len(finished)+len(refused) < len(plans) or case.get("diagnostics"):
        diagnostics(world, output_path)
    return Observation(cohorts, metrics, violations,
        dict(config=asdict(config), strategy=asdict(strategy), waits=report["waits"],
             boundary="prepared single-authority execution, no quorum/election model"))


def diagnostics(world, path):
    if path is None:
        return
    import json
    path.mkdir(parents=True, exist_ok=True)
    with (path / "trace.jsonl").open("w") as stream:
        for row in world.trace:
            stream.write(json.dumps(row, separators=(",", ":")) + "\n")
    (path / "choices.json").write_text(json.dumps(world.decisions, separators=(",", ":")))
    (path / "unfinished.json").write_text(json.dumps(world.report()["waits"]))
