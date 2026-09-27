"""A deliberately small, actor-local Orbital path on the resource simulator.

One producer, one immutable stream, a prepared static witness leader and two
followers. There is no election, reconfiguration, Byzantine authentication,
distributed transaction execution, or reclamation protocol here. Receipts and
votes have authenticated-sender semantics as a named model assumption. The
program is integer set/add; the independent observer reconstructs its result.

All persistence is append-only under stable identities. Recovery reads storage;
factories capture configuration, never recovered state. Each actor reserves a
fixed, bounded state allowance, with a bounded stream and payload size. Costs
are authored nanoseconds and bytes, not measured SixDB performance.
"""
from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass

from sim import Actor, Context, World, Host, Link


STREAM = "demo"
BALLOT = 1  # Prepared bootstrap assumption, not an executed phase-one quorum.
RETRY = 200_000
STATE_BYTES = 16_384
MAX_ENTRIES = 8
MAX_PAYLOAD_BYTES = 1_024
HOLDERS = {"producer": "az-a", "payload_b": "az-b", "payload_c": "az-c"}
WITNESSES = ("leader", "follower_b", "follower_c")
ACTOR_HOST = {
    "producer": "source", "payload_b": "copy_b", "payload_c": "copy_c",
    "leader": "witness_a", "follower_b": "witness_b", "follower_c": "witness_c",
    "consumer": "reader", "ordinary": "reader", "relay_a": "relay_a",
    "relay_b": "relay_b",
}


def encoded(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def digest(value):
    return hashlib.sha256(encoded(value)).hexdigest()


def payload(lsn, command):
    return {"stream": STREAM, "lsn": lsn, "command": command}


def receipt(holder, body):
    return {"holder": holder, "stream": STREAM, "lsn": body["lsn"], "digest": digest(body)}


def prefix_identity(entries):
    # Receipt arrival order does not change the journal value.
    return digest([{k: e[k] for k in ("stream", "lsn", "digest")} for e in entries])


def vote(voter, entries):
    return {"voter": voter, "ballot": BALLOT, "length": len(entries),
            "prefix": prefix_identity(entries)}


def valid_entries(entries, required_copies=2):
    if not isinstance(entries, list) or not 0 < len(entries) <= MAX_ENTRIES:
        return False
    for lsn, entry in enumerate(entries, 1):
        if entry.get("stream") != STREAM or entry.get("lsn") != lsn:
            return False
        domains = set()
        for r in entry.get("receipts", []):
            if (r.get("holder") in HOLDERS and r.get("stream") == STREAM
                    and r.get("lsn") == lsn and r.get("digest") == entry.get("digest")):
                domains.add(HOLDERS[r["holder"]])
        if len(domains) < required_copies:
            return False
    return True


def compatible(older, newer):
    return all(a["digest"] == b["digest"] for a, b in zip(older, newer))


def valid_proof(proof, entries):
    if not isinstance(proof, list) or len(proof) != 2:
        return False
    voters = {v.get("voter") for v in proof}
    return ("leader" in voters and len(voters) == 2 and voters <= set(WITNESSES)
            and all(v == vote(v["voter"], entries) for v in proof))


@dataclass(frozen=True)
class Config:
    via: str | None = None
    negative: str | None = None
    compute_ns: int = 30_000
    submitters: tuple[str, ...] = ()


class Endpoint(Actor):
    """Retries belong to endpoints; relays never manufacture acknowledgements."""
    def __init__(self, config):
        self.config = config
        self.via = config.via
        self.route_version = 0
        self.ready = False
        self.state_lease = None

    def send(self, ctx, target, kind, data):
        envelope = {"source": ctx.actor, "target": target, "kind": kind, "data": data}
        return ctx.send(self.via or target, "wire", envelope, size=len(encoded(envelope)) + 64)

    def on(self, ctx, kind, data):
        if kind in ("boot", "boot_retry"):
            if self.state_lease is None:
                self.state_lease = ctx.reserve(STATE_BYTES, "bounded-protocol-state")
            if self.state_lease is None or not ctx.scan("", "recovered"):
                ctx.timer(RETRY, "boot_retry")
            return
        if kind == "recovered":
            if not data["ok"]:
                ctx.timer(RETRY, "boot_retry")
                return
            records = data["records"]
            routes = [v for k, v in records.items() if k.startswith("route/")]
            if routes:
                route = max(routes, key=lambda x: x["version"])
                self.route_version, self.via = route["version"], route["via"]
            self.restore(ctx, records)
            self.ready = True
            ctx.note("protocol_recovered", records=len(records))
            self.tick(ctx)
            ctx.timer(RETRY, "tick")
            return
        if not self.ready:
            return  # Sender retries; no unbounded pre-recovery inbox.
        if kind == "route_update":
            if data["version"] > self.route_version:
                ctx.persist(f'route/{data["version"]}', data, "route_stored", data)
            return
        if kind == "route_stored":
            if data["ok"] and data["version"] > self.route_version:
                self.route_version, self.via = data["version"], data["via"]
                ctx.note("route_changed", via=self.via, version=self.route_version)
            return
        if kind == "tick":
            self.tick(ctx)
            ctx.timer(RETRY, "tick")
            return
        if kind == "wire":
            if data["target"] != ctx.actor:
                return
            self.message(ctx, data["source"], data["kind"], data["data"])
        else:
            self.local(ctx, kind, data)

    def restore(self, ctx, records):
        pass

    def tick(self, ctx):
        pass

    def message(self, ctx, source, kind, data):
        pass

    def local(self, ctx, kind, data):
        pass


class Relay(Actor):
    def on(self, ctx, kind, data):
        if kind == "wire":
            ctx.note("relayed", source=data["source"], target=data["target"])
            ctx.send(data["target"], "wire", data, size=len(encoded(data)) + 64)


class PayloadReplica(Endpoint):
    def __init__(self, config):
        super().__init__(config)
        self.bodies = {}
        self.pending = {}
        self.writing = set()

    def restore(self, ctx, records):
        self.bodies = {int(k.split("/")[1]): v for k, v in records.items()
                       if k.startswith("payload/")}

    def store(self, ctx, body):
        lsn = body.get("lsn", 0)
        if (body.get("stream") != STREAM or not 1 <= lsn <= MAX_ENTRIES
                or len(encoded(body)) > MAX_PAYLOAD_BYTES):
            return
        previous = self.bodies.get(lsn) or self.pending.get(lsn)
        if previous is not None:
            if previous != body:
                ctx.note("identity_conflict", lsn=lsn)
            elif lsn in self.bodies:
                self.received(ctx, body)
            return
        self.pending[lsn] = body
        self.write(ctx, body)

    def write(self, ctx, body):
        lsn = body["lsn"]
        if lsn not in self.writing and ctx.persist(
                f"payload/{lsn}", body, "payload_stored", {"body": body}):
            self.writing.add(lsn)

    def tick(self, ctx):
        # Refused submissions retry; accepted local I/O has one continuation.
        for body in self.pending.values():
            self.write(ctx, body)

    def received(self, ctx, body):
        self.send(ctx, "producer", "receipt", receipt(ctx.actor, body))

    def local(self, ctx, kind, data):
        if kind == "payload_stored":
            body = data["body"]
            self.writing.discard(body["lsn"])
            if not data["ok"]:
                return
            self.bodies[body["lsn"]] = body
            self.pending.pop(body["lsn"], None)
            ctx.note("payload_durable", lsn=body["lsn"], body=body, digest=digest(body))
            self.received(ctx, body)

    def message(self, ctx, source, kind, data):
        if kind == "replicate" and source == "producer":
            self.store(ctx, data)
        elif kind == "fetch" and source == "consumer":
            # Physical reads are exercised rather than treating the recovered
            # cache as a free network-serving store.
            ctx.load(f'payload/{data["lsn"]}', "payload_loaded", {"request": data})

    def on(self, ctx, kind, data):
        if kind == "payload_loaded" and self.ready:
            body = data.get("value")
            if data["ok"] and body and digest(body) == data["request"]["digest"]:
                self.send(ctx, "consumer", "payload", body)
            return
        super().on(ctx, kind, data)


class Producer(PayloadReplica):
    def __init__(self, config):
        super().__init__(config)
        self.receipts = {}
        self.applied = 0
        self.scanning = False

    def restore(self, ctx, records):
        super().restore(ctx, records)
        for lsn, body in self.bodies.items():
            self.receipts[lsn] = {"producer": receipt("producer", body)}

    def received(self, ctx, body):
        self.receipts.setdefault(body["lsn"], {})["producer"] = receipt("producer", body)
        self.tick(ctx)

    def local(self, ctx, kind, data):
        if kind == "submit":
            body = payload(data["lsn"], data["command"])
            if data["lsn"] not in self.bodies and data["lsn"] not in self.pending:
                ctx.note("offered", op=f'{STREAM}:{body["lsn"]}', body=body)
                ctx.wait(f'{STREAM}:{body["lsn"]}', "payload durability and chosen publication")
            self.store(ctx, body)
        elif kind == "payload_reconciled":
            self.scanning = False
            if data["ok"]:
                for body in data["records"].values():
                    lsn = body["lsn"]
                    if lsn not in self.bodies:
                        self.bodies[lsn] = body
                        self.receipts.setdefault(lsn, {})["producer"] = receipt("producer", body)
                        ctx.note("late_payload_recovered", lsn=lsn)
                self.push(ctx)
        else:
            super().local(ctx, kind, data)

    def tick(self, ctx):
        super().tick(ctx)
        # A process can restart and scan before a previous incarnation's
        # submitted write completes. Reconciliation is paid storage I/O, not
        # an assumed shutdown barrier or access to the world's pending jobs.
        if not self.scanning:
            self.scanning = ctx.scan("payload/", "payload_reconciled")
        self.push(ctx)

    def push(self, ctx):
        for lsn, body in sorted(self.bodies.items()):
            if lsn <= self.applied:
                continue
            for holder in ("payload_b", "payload_c"):
                if holder not in self.receipts.get(lsn, {}):
                    self.send(ctx, holder, "replicate", body)
            if self.config.negative == "publish-too-early":
                self.send(ctx, "consumer", "tentative", body)
        entries = []
        for lsn in range(1, len(self.bodies) + 1):
            if lsn not in self.bodies:
                break
            rs = list(self.receipts.get(lsn, {}).values())
            need = 1 if self.config.negative == "chosen-before-payload" else 2
            if len({HOLDERS[r["holder"]] for r in rs}) < need:
                break
            entries.append({"stream": STREAM, "lsn": lsn,
                            "digest": digest(self.bodies[lsn]), "receipts": rs})
        if len(entries) > self.applied:
            self.send(ctx, "leader", "frontier", {"entries": entries})

    def message(self, ctx, source, kind, data):
        if kind == "submit" and source in self.config.submitters:
            self.local(ctx, "submit", data)
        elif kind == "receipt" and source in HOLDERS and data.get("holder") == source:
            body = self.bodies.get(data["lsn"])
            if body and data == receipt(source, body):
                changed = source not in self.receipts.get(data["lsn"], {})
                self.receipts.setdefault(data["lsn"], {})[source] = data
                if changed:
                    self.tick(ctx)
        elif kind == "applied" and source == "consumer":
            self.applied = max(self.applied, data["lsn"])
            for lsn in range(1, self.applied + 1):
                ctx.clear_wait(f"{STREAM}:{lsn}")
        else:
            super().message(ctx, source, kind, data)


class Witness(Endpoint):
    def __init__(self, config, leader=False):
        super().__init__(config)
        self.is_leader = leader
        self.accepted = []
        self.pending = None
        self.wanted = None
        self.leader_vote = None
        self.writing = False

    def restore(self, ctx, records):
        accepted = [v for k, v in records.items() if k.startswith("accepted/")]
        if accepted:
            value = max(accepted, key=lambda v: len(v["entries"]))
            self.accepted = value["entries"]
            self.leader_vote = value["leader_vote"]

    def request(self, ctx, entries, leader_vote):
        need = 1 if self.config.negative == "chosen-before-payload" else 2
        if not valid_entries(entries, need) or not compatible(self.accepted, entries):
            return
        if len(entries) <= len(self.accepted):
            self.propagate(ctx)
            return
        if self.wanted is None or len(entries) > len(self.wanted["entries"]):
            if self.pending and not compatible(self.pending["entries"], entries):
                return
            self.wanted = {"entries": entries, "leader_vote": leader_vote}
        self.start_write(ctx)

    def start_write(self, ctx):
        if self.writing:
            return
        if self.pending is None and self.wanted:
            self.pending, self.wanted = self.wanted, None
        if self.pending:
            n = len(self.pending["entries"])
            self.writing = ctx.persist(f"accepted/{n}", self.pending, "accepted_stored", self.pending)

    def local(self, ctx, kind, data):
        if kind != "accepted_stored":
            return
        self.writing = False
        if not data["ok"]:
            return
        entries = data["entries"]
        if len(entries) > len(self.accepted):
            self.accepted, self.leader_vote = entries, data["leader_vote"]
            ctx.note("witness_accepted", vote=vote(ctx.actor, entries), entries=entries)
        if self.pending and len(self.pending["entries"]) <= len(self.accepted):
            self.pending = None
        self.propagate(ctx)
        self.start_write(ctx)

    def propagate(self, ctx):
        if not self.accepted:
            return
        own = vote(ctx.actor, self.accepted)
        if self.is_leader:
            for follower in WITNESSES[1:]:
                self.send(ctx, follower, "accept", {"entries": self.accepted, "leader_vote": own})
        else:
            proof = [self.leader_vote, own]
            ctx.note("prefix_chosen", entries=self.accepted, proof=proof)
            self.send(ctx, "consumer", "chosen", {"entries": self.accepted, "proof": proof})
            self.send(ctx, "leader", "accepted_ack", {"proof": proof})

    def tick(self, ctx):
        self.start_write(ctx)
        self.propagate(ctx)

    def message(self, ctx, source, kind, data):
        if self.is_leader and kind == "frontier" and source == "producer":
            self.request(ctx, data["entries"], vote("leader", data["entries"]))
        elif not self.is_leader and kind == "accept" and source == "leader":
            if data["leader_vote"] == vote("leader", data["entries"]):
                self.request(ctx, data["entries"], data["leader_vote"])
        elif kind == "sync" and source == "consumer":
            self.propagate(ctx)


class Consumer(Endpoint):
    def __init__(self, config):
        super().__init__(config)
        self.entries = []
        self.proof = None
        self.bodies = {}
        self.applied = 0
        self.values = {}
        self.busy = False
        self.persisting = None
        self.early = set()
        self.writing = False

    def restore(self, ctx, records):
        snapshots = [v for k, v in records.items() if k.startswith("applied/")]
        if snapshots:
            snapshot = max(snapshots, key=lambda x: x["lsn"])
            self.applied, self.values = snapshot["lsn"], snapshot["values"]
            self.entries, self.proof = snapshot["entries"], snapshot["proof"]
            ctx.note("consumer_restored", lsn=self.applied, values=self.values)
            # Completion responses may be retried; logical application is not.
            self.publish(ctx, snapshot, recovered=True)

    def tick(self, ctx):
        for witness in WITNESSES:
            self.send(ctx, witness, "sync", {})
        if self.persisting:
            self.persist_result(ctx)
        self.advance(ctx)
        if self.applied:
            self.send(ctx, "producer", "applied", {"lsn": self.applied})

    def advance(self, ctx):
        if self.busy or self.applied >= len(self.entries):
            return
        entry = self.entries[self.applied]
        lsn = entry["lsn"]
        body = self.bodies.get(lsn)
        if body is None:
            ctx.wait(f"consume:{lsn}", "chosen payload missing", holders=list(HOLDERS))
            for holder in HOLDERS:
                self.send(ctx, holder, "fetch", {"lsn": lsn, "digest": entry["digest"]})
            return
        if ctx.compute(self.config.compute_ns, "computed", {"body": body},
                       leases=(self.state_lease,)):
            self.busy = True
            ctx.clear_wait(f"consume:{lsn}")

    def message(self, ctx, source, kind, data):
        if kind == "status" and source in self.config.submitters:
            if self.applied:
                self.send(ctx, source, "result", {"lsn": self.applied, "values": self.values})
        elif kind == "read_version" and source in self.config.submitters:
            if 0 < data["lsn"] <= self.applied:
                ctx.load(f'applied/{data["lsn"]}', "version_read", {"reply": source})
        elif kind == "chosen" and source in WITNESSES:
            entries, proof = data["entries"], data["proof"]
            # Negative-control admission still has an actual witness quorum.
            need = 1 if self.config.negative == "chosen-before-payload" else 2
            if (valid_entries(entries, need) and valid_proof(proof, entries)
                    and compatible(self.entries, entries) and len(entries) > len(self.entries)):
                self.entries, self.proof = entries, proof
                self.advance(ctx)
        elif kind == "payload" and source in HOLDERS:
            lsn = data.get("lsn", 0)
            if 1 <= lsn <= len(self.entries) and digest(data) == self.entries[lsn - 1]["digest"]:
                self.bodies[lsn] = data
                self.advance(ctx)
        elif kind == "tentative" and self.config.negative == "publish-too-early":
            lsn = data["lsn"]
            if lsn not in self.early:
                self.early.add(lsn)
                ctx.note("published", op=f"{STREAM}:{lsn}", lsn=lsn,
                         values={data["command"]["key"]: data["command"]["value"]},
                         entries=[], proof=[])

    def local(self, ctx, kind, data):
        if kind == "version_read":
            if data["ok"] and data["value"] is not None:
                snapshot = data["value"]
                self.send(ctx, data["reply"], "version", dict(lsn=snapshot["lsn"], values=snapshot["values"]))
        elif kind == "computed":
            body = data["body"]
            command = body["command"]
            values = dict(self.values)
            if command["kind"] == "add":
                values[command["key"]] = values.get(command["key"], 0) + command["value"]
            elif command["kind"] == "set":
                values[command["key"]] = command["value"]
            else:
                raise ValueError("toy program supports set/add only")
            self.persisting = {"lsn": body["lsn"], "values": values,
                               "entries": self.entries, "proof": self.proof}
            self.persist_result(ctx)
        elif kind == "application_stored":
            self.writing = False
            if not data["ok"]:
                return
            snapshot = data["snapshot"]
            if snapshot["lsn"] <= self.applied:
                return
            assert snapshot["lsn"] == self.applied + 1
            self.applied, self.values = snapshot["lsn"], snapshot["values"]
            self.persisting, self.busy = None, False
            self.bodies.pop(self.applied, None)
            ctx.note("application_durable", lsn=self.applied, values=self.values)
            self.publish(ctx, snapshot)
            self.advance(ctx)

    def persist_result(self, ctx):
        snapshot = self.persisting
        if not self.writing:
            self.writing = ctx.persist(f'applied/{snapshot["lsn"]}', snapshot,
                                      "application_stored", {"snapshot": snapshot})
            ctx.wait(f'consume:{snapshot["lsn"]}', "durable application completion"
                     if self.writing else "durable application backpressure")

    def publish(self, ctx, snapshot, recovered=False):
        ctx.clear_wait(f'consume:{snapshot["lsn"]}')
        ctx.note("published", op=f'{STREAM}:{snapshot["lsn"]}', recovered=recovered, **snapshot)
        self.send(ctx, "producer", "applied", {"lsn": snapshot["lsn"]})
        for client in self.config.submitters:
            self.send(ctx, client, "result", dict(lsn=snapshot["lsn"], values=snapshot["values"]))


class Ordinary(Actor):
    """A separate ordinary application exercises contention for real resources."""
    def on(self, ctx, kind, data):
        if kind == "work":
            ctx.note("offered", op="ordinary")
            if not ctx.compute(20_000, "computed", {"value": 7}):
                ctx.timer(RETRY, "work")
        elif kind == "computed":
            if not ctx.persist("result", data, "done", data):
                ctx.timer(RETRY, "computed", data)
        elif kind == "done" and data["ok"]:
            ctx.note("ordinary_done", op="ordinary", value=data["value"])


DEFAULT_COMMANDS = (
    {"kind": "set", "key": "x", "value": 10},
    {"kind": "add", "key": "x", "value": 3},
    {"kind": "add", "key": "y", "value": 7},
    {"kind": "add", "key": "x", "value": -2},
)


def build_scenario(*, mode="direct", commands=None, negative=None, seed=1,
                   ordering="fifo", replay=None, loss=0, duplicate=0,
                   disk_latency=20_000, consumer_compute_ns=30_000,
                   world=None, offer=True, submitters=(), placement=None):
    """Build an unfaulted world; callers inject faults before running it.

    Relay mode uses two physical hops per protocol message. Both relays exist
    so callers can deliver versioned route_update inputs and partition a route.
    Actor bootstrap has static membership, domains and one prepared ballot.
    """
    if mode not in ("direct", "relay"):
        raise ValueError(mode)
    if negative not in (None, "chosen-before-payload", "publish-too-early"):
        raise ValueError(negative)
    commands = list(DEFAULT_COMMANDS if commands is None else commands)
    if not 0 < len(commands) <= MAX_ENTRIES:
        raise ValueError("bounded stream supports one to eight entries")
    w = world if world is not None else World(seed=seed, ordering=ordering, replay=replay)
    domains = {"source": "az-a", "copy_b": "az-b", "copy_c": "az-c",
               "witness_a": "az-a", "witness_b": "az-b", "witness_c": "az-c",
               "reader": "az-d", "relay_a": "az-e", "relay_b": "az-f"}
    for host, domain in domains.items():
        w.add_host(Host(host, domain=domain, workers=1, control=1,
                        memory_bytes=131_072, durable_bytes=131_072,
                        queue_limit=64, io_slots=2, disk_latency=disk_latency))
    for source in domains:
        for target in domains:
            if source != target:
                w.add_link(Link(source, target, latency=10_000, loss=loss, duplicate=duplicate))
    config = Config(via="relay_a" if mode == "relay" else None,
                    negative=negative, compute_ns=consumer_compute_ns, submitters=tuple(submitters))
    factories = {"producer": lambda: Producer(config),
                 "payload_b": lambda: PayloadReplica(config),
                 "payload_c": lambda: PayloadReplica(config),
                 "leader": lambda: Witness(config, leader=True),
                 "follower_b": lambda: Witness(config),
                 "follower_c": lambda: Witness(config),
                 "consumer": lambda: Consumer(config),
                 "ordinary": Ordinary, "relay_a": Relay, "relay_b": Relay}
    for actor, factory in factories.items():
        w.add_actor(actor, (placement or {}).get(actor, ACTOR_HOST[actor]), factory)
    if offer:
        for lsn, command in enumerate(commands, 1):
            w.inject("producer", "submit", {"lsn": lsn, "command": command}, at=100_000 + lsn * 40_000)
        w.inject("ordinary", "work", {}, at=150_000)
    return w


class AuditError(AssertionError):
    pass


def audit(world, *, raise_on_error=False):
    """Independent observer: reconstruct evidence and program from event facts.

    This uses physical environment durable_write events, not actors' assertions
    of durability. It does not prove cryptography or fsync hardware. It never
    calls actor methods or trusts a safety flag; replies are distinct from writes.
    """
    bodies, copies, accepts, choices, applications, published = {}, {}, set(), set(), {}, {}
    longest_chosen = []
    violations = []
    for e in world.trace:
        kind = e["kind"]
        if kind == "durable_write" and e["key"].startswith("payload/"):
            body = e["value"]
            claimed = digest(body)
            bodies[claimed] = body
            copies.setdefault(claimed, set()).add(e["actor"])
        elif kind == "durable_write" and e["key"].startswith("accepted/"):
            v = vote(e["actor"], e["value"]["entries"])
            accepts.add((e["actor"], v["ballot"], v["length"], v["prefix"]))
        elif kind == "prefix_chosen":
            entries, proof = e["entries"], e["proof"]
            good = valid_proof(proof, entries) and valid_entries(entries)
            if not valid_entries(entries):
                violations.append("chosen prefix lacks a validated two-domain frontier")
            for v in proof:
                if (v["voter"], v["ballot"], v["length"], v["prefix"]) not in accepts:
                    good = False
                    violations.append("choice lacks preceding matching durable witness vote")
            for entry in entries:
                domains = {world.hosts[world.actors[x].host].config.domain
                           for x in copies.get(entry["digest"], set()) if x in HOLDERS}
                if len(domains) < 2:
                    good = False
                    violations.append(f'chosen LSN {entry["lsn"]} lacks two durable payload domains')
                for r in entry.get("receipts", []):
                    if r["holder"] not in copies.get(entry["digest"], set()):
                        good = False
                        violations.append("receipt has no preceding durable payload fact")
            if good:
                if not compatible(longest_chosen, entries):
                    violations.append("incompatible prefixes were both chosen")
                if len(entries) > len(longest_chosen):
                    longest_chosen = entries
                choices.add(prefix_identity(entries))
            elif not valid_proof(proof, entries):
                violations.append("invalid chosen proof")
        elif (kind == "durable_write" and e["actor"] == "consumer"
              and e["key"].startswith("applied/")):
            applications[e["value"]["lsn"]] = e["value"]["values"]
        elif kind == "published":
            lsn, entries = e["lsn"], e["entries"]
            if (not valid_proof(e["proof"], entries) or prefix_identity(entries) not in choices
                    or len(entries) < lsn):
                violations.append(f"publication {lsn} lacks chosen evidence")
                continue
            expected = {}
            complete = True
            for entry in entries[:lsn]:
                body = bodies.get(entry["digest"])
                if body is None:
                    complete = False
                    break
                command = body["command"]
                key, number = command["key"], command["value"]
                expected[key] = number if command["kind"] == "set" else expected.get(key, 0) + number
            if not complete or expected != e["values"]:
                violations.append(f"publication {lsn} disagrees with independently folded payload")
            if applications.get(lsn) != e["values"]:
                violations.append(f"publication {lsn} precedes durable application")
            if lsn in published and published[lsn] != e["values"]:
                violations.append("duplicate publication changed its result")
            published[lsn] = e["values"]
    result = {"ok": not violations, "violations": sorted(set(violations)),
              "published": published, "values": published[max(published)] if published else {},
              "applications": len(applications), "chosen_prefixes": len(choices),
              "ordinary_completed": any(e["kind"] == "ordinary_done" for e in world.trace)}
    if violations and raise_on_error:
        raise AuditError("; ".join(result["violations"]))
    return result


def run_scenario(*, until=4_000_000, **kwargs):
    world = build_scenario(**kwargs)
    report = world.run(until=until)
    return {"world": world, "audit": audit(world), "report": report}


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("direct", "relay"), default="direct")
    parser.add_argument("--negative", choices=("chosen-before-payload", "publish-too-early"))
    args = parser.parse_args()
    result = run_scenario(mode=args.mode, negative=args.negative)
    print(json.dumps({k: v for k, v in result.items() if k != "world"}, sort_keys=True, indent=2))
