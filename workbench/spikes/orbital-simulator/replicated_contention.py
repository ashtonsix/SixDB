"""Prepared quorum admission composed with traffic.Shard's deterministic fold.

Each shard has one prepared leader, two follower witnesses and three consumers.
The leader sequences immutable, hash-chained batches. Witnesses persist the FULL
input, then independently send their own receipts directly to consumers. A
consumer requires the leader and one follower receipt before persisting/folding
an epoch. Receipt sender identity is authenticated as a model assumption; quorum
proofs are never constructed by the harness. This full-body path is deliberately
stronger and more costly than BRIEF's producer-copy/frontier admission path.

No elections, handoff, post-admission abort, log reclamation or arbitrary process
restart is claimed. Recovery tests reset the device, retire submitted writes,
and restore from actual local records or witness loads/messages. Retained state
has explicit bounded allowances; their coefficients are authored, not measured.
"""
from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field, replace

from kernel import clone, digest
from sim import Actor, Host, Link, World, encoded_size
from traffic import Config, Strategy, Shard, Coordinator, Client, initial_state, workload, shard_of, group

BALLOT = 1
RETRY = 200_000
MAX_REQUESTS = 4096


@dataclass(frozen=True)
class Assembly:
    """Explicit additions/overrides to this experiment's physical construction.

    Hosts and directed links are supplied using the existing physical model.
    Added hosts inherit no connectivity or costs. Same-host traffic uses shared
    host resources, so a same-host Link would silently have no effect and is
    rejected. Factories are zero-argument configuration-only constructors for
    existing roles; World uses the same factory again after device recovery.
    """
    placement: dict[str, str] = field(default_factory=dict)
    hosts: tuple[Host, ...] = ()
    links: tuple[Link, ...] = ()
    factories: dict = field(default_factory=dict)


def witness_names(shard):
    return (f"shard{shard}", f"witness{shard}_1", f"witness{shard}_2")


def consumer_names(shard):
    return tuple(f"consumer{shard}_{i}" for i in range(3))


def wire(ctx, target, kind, data):
    envelope = dict(source=ctx.actor, kind=kind, data=data)
    return ctx.send(target, "wire", envelope, size=encoded_size(envelope) + 48)


def receipt(actor, batch):
    return dict(voter=actor, ballot=BALLOT, shard=batch["shard"],
                epoch=batch["epoch"], digest=digest(batch))


def proof_valid(shard, batch, proof):
    voters = {r["voter"] for r in proof}
    names = set(witness_names(shard))
    return (len(proof) == 2 and len(voters) == 2 and witness_names(shard)[0] in voters
            and voters <= names and all(r == receipt(r["voter"], batch) for r in proof))


class Endpoint(Actor):
    def __init__(self, shard):
        self.shard = shard
        self.ready = False
        self.lease = None
        self.leases = {}

    def retain(self, ctx, identity, value, multiplier=2):
        if identity in self.leases:
            return True
        lease = ctx.reserve(256 + multiplier * encoded_size(value), "replicated-retained-input")
        if lease is None:
            ctx.wait(identity, "retained input capacity")
            return False
        self.leases[identity] = lease
        ctx.clear_wait(identity)
        return True

    def on(self, ctx, kind, data):
        if kind in ("boot", "boot-retry"):
            if self.lease is None:
                self.lease = ctx.reserve(2048, "replicated-endpoint-base")
            if self.lease is None or not ctx.scan("", "recovered"):
                ctx.timer(RETRY, "boot-retry")
            return
        if kind == "recovered":
            if not data["ok"] or not self.restore(ctx, data["records"]):
                ctx.timer(RETRY, "boot-retry")
                return
            self.ready = True
            ctx.note("replicated_recovered", shard=self.shard, records=len(data["records"]))
            self.tick(ctx)
            ctx.timer(RETRY, "tick")
            return
        if not self.ready:
            return
        if kind == "tick":
            self.tick(ctx)
            ctx.timer(RETRY, "tick")
        elif kind == "wire":
            self.message(ctx, data["source"], data["kind"], data["data"])
        else:
            self.local(ctx, kind, data)


class Witness(Endpoint):
    def __init__(self, shard, strategy, leader=False):
        super().__init__(shard)
        self.strategy, self.leader = strategy, leader
        self.batches, self.requests = {}, {}
        self.pending = {}
        self.queue, self.queued = deque(), set()
        self.writing = None
        self.flush_pending = False
        self.followers = {name: 0 for name in witness_names(shard)[1:]}
        self.sent_epochs = {name: 0 for name in self.followers}

    def restore(self, ctx, records):
        for key, batch in sorted(records.items(), key=lambda row: int(row[0].split("/")[1])):
            if not self.retain(ctx, key, batch):
                return False
            self.accept_local(batch)
        return True

    def accept_local(self, batch):
        epoch = batch["epoch"]
        if epoch != len(self.batches) + 1:
            if self.batches.get(epoch) == batch:
                return
            raise ValueError("witness retained history has a gap or fork")
        previous = digest(self.batches[epoch - 1]) if epoch > 1 else None
        if batch["previous"] != previous or batch["shard"] != self.shard:
            raise ValueError("witness retained history has incompatible predecessor")
        self.batches[epoch] = batch
        for request in batch["requests"]:
            rid = request["request"]
            if rid in self.requests and self.requests[rid] != (epoch, request):
                raise ValueError("request identity reused with different input")
            self.requests[rid] = (epoch, request)

    def propagate(self, ctx, batch, target=None):
        message = dict(batch=batch, receipt=receipt(ctx.actor, batch))
        for consumer in (target,) if target else consumer_names(self.shard):
            wire(ctx, consumer, "receipt", message)
        if not self.leader:
            wire(ctx, witness_names(self.shard)[0], "accepted",
                 dict(epoch=batch["epoch"], digest=digest(batch)))

    def tick(self, ctx):
        if self.leader:
            self.pump_replication(ctx, retry=True)
            self.flush(ctx)
        else:
            self.persist_pending(ctx)

    def pump_replication(self, ctx, retry=False):
        for follower, frontier in self.followers.items():
            epoch = frontier + 1
            # Progress sends a new outstanding epoch once. A local append or
            # another follower's ACK cannot multiply this follower's retries.
            # The periodic timer explicitly retries accepted-but-unanswered sends.
            if epoch in self.batches and (retry or self.sent_epochs[follower] != epoch):
                if wire(ctx, follower, "append", self.batches[epoch]):
                    self.sent_epochs[follower] = epoch

    def message(self, ctx, source, kind, data):
        if kind == "append" and not self.leader and source == witness_names(self.shard)[0]:
            epoch = data["epoch"]
            if epoch in self.batches:
                if self.batches[epoch] != data:
                    raise ValueError("prepared leader equivocated")
                self.propagate(ctx, data)
            elif epoch == len(self.batches) + 1:
                if epoch in self.pending and self.pending[epoch] != data:
                    raise ValueError("prepared leader reused sequence")
                if self.retain(ctx, f"log/{epoch}", data):
                    self.pending[epoch] = data
                    self.persist_pending(ctx)
        elif kind == "accepted" and self.leader and source in self.followers:
            epoch = data["epoch"]
            if (epoch in self.batches and epoch > self.followers[source]
                    and data["digest"] == digest(self.batches[epoch])):
                self.followers[source] = epoch
                self.pump_replication(ctx)
        elif kind == "fetch" and source in consumer_names(self.shard):
            epoch = data["epoch"]
            if epoch in self.batches:
                ctx.load(f"log/{epoch}", "loaded", dict(target=source))

    def local(self, ctx, kind, data):
        if kind == "request" and self.leader:
            rid = data["request"]
            if rid in self.requests:
                _, original = self.requests[rid]
                if original != data:
                    raise ValueError("client reused request identity")
                wire(ctx, consumer_names(self.shard)[0], "repeat", data)
            elif rid not in self.queued:
                if len(self.requests) + len(self.queued) >= MAX_REQUESTS:
                    ctx.wait("admission", "bounded experiment request capacity")
                    return
                if not self.retain(ctx, f"request/{rid}", data):
                    return
                self.queued.add(rid)
                self.queue.append(clone(data))
                if len(self.queue) >= self.strategy.batch:
                    self.flush(ctx)
                elif not self.flush_pending:
                    self.flush_pending = True
                    ctx.timer(self.strategy.batch_ns, "flush")
        elif kind == "flush":
            self.flush_pending = False
            self.flush(ctx)
        elif kind == "stored":
            self.writing = None
            if not data["ok"]:
                return
            batch = data["batch"]
            self.accept_local(batch)
            self.pending.pop(batch["epoch"], None)
            for request in batch["requests"]:
                self.queued.discard(request["request"])
            ctx.note("replicated_accepted", shard=self.shard, epoch=batch["epoch"], batch=batch)
            self.propagate(ctx, batch)
            if self.leader:
                self.pump_replication(ctx)
                self.flush(ctx)
            else:
                self.persist_pending(ctx)
        elif kind == "loaded" and data["ok"] and data.get("value"):
            self.propagate(ctx, data["value"], target=data["target"])

    def persist_pending(self, ctx):
        if self.writing is not None:
            return
        epoch = len(self.batches) + 1
        batch = self.pending.get(epoch)
        if batch is not None and ctx.persist(f"log/{epoch}", batch, "stored", dict(batch=batch)):
            self.writing = epoch

    def flush(self, ctx):
        if not self.leader or self.writing is not None:
            return
        if self.pending:
            self.persist_pending(ctx)
            return
        if not self.queue:
            return
        requests = [self.queue.popleft() for _ in range(min(self.strategy.batch, len(self.queue)))]
        epoch = len(self.batches) + 1
        batch = dict(shard=self.shard, ballot=BALLOT, epoch=epoch,
                     previous=digest(self.batches[epoch - 1]) if epoch > 1 else None,
                     requests=requests)
        if not self.retain(ctx, f"log/{epoch}", batch):
            self.queue.extendleft(reversed(requests))
            return
        self.pending[epoch] = batch
        self.persist_pending(ctx)


class Consumer(Endpoint):
    def __init__(self, shard, initial, strategy, replica, fold_ns=0):
        super().__init__(shard)
        self.history = Shard(shard, initial, strategy, metadata_ns_per_entry=0)
        self.replica, self.fold_ns = replica, fold_ns
        self.inputs, self.votes, self.epochs = {}, {}, {}
        self.applied = 0
        self.writing = self.running = False
        self.outputs = {}
        self.state_hashes, self.output_hashes = {}, {}

    def restore(self, ctx, records):
        # Recovered records were persisted with actual received matching receipts.
        if not self.retain(ctx, "initial", self.history.logical_state(), multiplier=2):
            return False
        for key, record in sorted(records.items(), key=lambda row: int(row[0].split("/")[1])):
            if not self.retain(ctx, key, record, multiplier=4):
                return False
            batch = record["batch"]
            if not proof_valid(self.shard, batch, record["proof"]):
                raise ValueError("invalid recovered quorum proof")
            self.epochs[batch["epoch"]] = record
        return True

    def tick(self, ctx):
        self.pump(ctx)
        self.fetch_next(ctx)

    def fetch_next(self, ctx):
        # Ask holders for the next missing agreed input. This also obtains fresh
        # individual receipts after a callback, transport or consumer failure.
        epoch = max(self.applied, max(self.epochs, default=0)) + 1
        for witness in witness_names(self.shard):
            wire(ctx, witness, "fetch", dict(epoch=epoch))

    def message(self, ctx, source, kind, data):
        if kind == "receipt" and source in witness_names(self.shard):
            batch, vote = data["batch"], data["receipt"]
            epoch = batch["epoch"]
            if vote != receipt(source, batch) or batch["shard"] != self.shard:
                raise ValueError("invalid receipt")
            if epoch <= self.applied or epoch in self.epochs:
                return
            if epoch in self.inputs and self.inputs[epoch] != batch:
                raise ValueError("conflicting quorum inputs")
            if not self.retain(ctx, f"epoch/{epoch}", data, multiplier=4):
                return
            self.inputs[epoch] = batch
            self.votes.setdefault(epoch, {})[source] = vote
            self.pump(ctx)
        elif kind == "repeat" and source == witness_names(self.shard)[0] and self.replica == 0:
            rid = data["request"]
            if rid in self.outputs:
                request, result = self.outputs[rid]
                if request != data:
                    raise ValueError("replayed response identity changed")
                self.history.reply(ctx, request, result)

    def local(self, ctx, kind, data):
        if kind == "epoch-stored":
            self.writing = False
            if data["ok"]:
                epoch = data["record"]["batch"]["epoch"]
                self.epochs[epoch] = data["record"]
                self.inputs.pop(epoch, None)
                self.votes.pop(epoch, None)
                self.fetch_next(ctx)
            self.pump(ctx)
        elif kind == "fold":
            epoch = self.applied + 1
            record = self.epochs[epoch]
            batch = record["batch"]
            previous = digest(self.epochs[epoch - 1]["batch"]) if epoch > 1 else None
            if batch["previous"] != previous:
                raise ValueError("consumer's contiguous history has a fork")
            produced = []
            for request in batch["requests"]:
                results = self.history.apply(request)
                ctx.note("replicated_transition", shard=self.shard, replica=self.replica,
                         epoch=epoch, op=request["tx"], request=request)
                for original, result in results:
                    self.outputs[original["request"]] = (original, result)
                    produced.append([original, result])
                    ctx.note("replicated_output", shard=self.shard, replica=self.replica,
                             epoch=epoch, op=original["tx"], request=original, result=result)
                if self.replica == 0:
                    self.history.deliver(ctx, results, trigger=request["request"])
                    self.history.report_waits(ctx)
            state = self.history.logical_state()
            self.state_hashes[epoch] = digest(state)
            self.output_hashes[epoch] = digest(produced)
            ctx.note("replicated_fixpoint", shard=self.shard, replica=self.replica,
                     epoch=epoch, state=state, logical_hash=digest(state),
                     outputs=produced, output_hash=digest(produced), input_hash=digest(batch))
            self.applied = epoch
            self.running = False
            self.pump(ctx)

    def pump(self, ctx):
        epoch = self.applied + 1
        if epoch in self.epochs and not self.running:
            count = len(self.epochs[epoch]["batch"]["requests"])
            if ctx.compute(max(1, self.fold_ns * count), "fold"):
                self.running = True
        if self.writing:
            return
        epoch = max(self.epochs, default=0) + 1
        if epoch not in self.inputs:
            return
        votes = self.votes[epoch]
        leader, *followers = witness_names(self.shard)
        follower = next((name for name in followers if name in votes), None)
        if leader not in votes or follower is None:
            return
        record = dict(batch=self.inputs[epoch], proof=[votes[leader], votes[follower]])
        if not proof_valid(self.shard, record["batch"], record["proof"]):
            raise ValueError("invalid live quorum proof")
        if ctx.persist(f"epoch/{epoch}", record, "epoch-stored", dict(record=record)):
            self.writing = True


def build(config=Config(count=12, topology="lan"), strategy=Strategy(), seed=1,
          replica_delays=(0, 3_000, 9_000), replay=None, offers=True, assembly=None):
    """Reuse traffic roles, with explicit optional placement/implementation choices."""
    assembly = assembly if assembly is not None else Assembly()
    if not isinstance(assembly, Assembly):
        raise TypeError("assembly must be an Assembly")
    host_shards = {"client": 0}
    for shard in range(config.shards):
        host_shards[f"h{shard}"] = shard
        for replica in range(3):
            host_shards[f"c{shard}_{replica}"] = shard
        for follower in (1, 2):
            host_shards[f"w{shard}_{follower}"] = shard
    placement = {"client": "client", "coordinator0": "h0", "coordinator1": "h1"}
    for shard in range(config.shards):
        placement.update({name: f"h{shard}" if i == 0 else f"w{shard}_{i}"
                          for i, name in enumerate(witness_names(shard))})
        placement.update({name: f"c{shard}_{i}" for i, name in enumerate(consumer_names(shard))})
    unknown = (set(assembly.placement) | set(assembly.factories)) - set(placement)
    if unknown:
        raise ValueError(f"unknown actor roles: {sorted(unknown)}")
    known_hosts = set(host_shards)
    for host in assembly.hosts:
        if not isinstance(host, Host):
            raise TypeError("assembly hosts must be Host values")
        if host.name in known_hosts:
            raise ValueError(f"duplicate host: {host.name}")
        known_hosts.add(host.name)
    placement.update(assembly.placement)
    missing = set(placement.values()) - known_hosts
    if missing:
        raise ValueError(f"placement names unknown hosts: {sorted(missing)}")
    known_links = {(a, b) for a in host_shards for b in host_shards if a != b}
    for link in assembly.links:
        if not isinstance(link, Link):
            raise TypeError("assembly links must be Link values")
        if link.source not in known_hosts or link.target not in known_hosts:
            raise ValueError("link endpoint names an unknown host")
        if link.source == link.target:
            raise ValueError("same-host traffic uses host resources, not a Link")
        edge = (link.source, link.target)
        if edge in known_links:
            raise ValueError(f"duplicate link: {edge}")
        known_links.add(edge)
    if any(not callable(factory) for factory in assembly.factories.values()):
        raise TypeError("actor factories must be callable")
    world = World(seed=seed, ordering="shuffle", replay=replay)
    for name, shard in host_shards.items():
        world.add_host(Host(name, domain=name, workers=config.workers,
                           handler_ns=config.handler_ns, memory_bytes=config.memory_bytes,
                           durable_bytes=config.durable_bytes, disk_latency=config.disk_latency,
                           nic_bytes_per_ns=config.bandwidth, disk_bytes_per_ns=2,
                           io_slots=4, queue_limit=256))
    for source, a in host_shards.items():
        for target, b in host_shards.items():
            if source == target:
                continue
            remote = (a == config.shards - 1) != (b == config.shards - 1)
            latency = 500 if config.topology == "lan" else 12_000
            if config.topology == "wan" and remote:
                latency = 40_000_000
            if config.topology == "asymmetric" and remote:
                latency = 8_000_000 if a == config.shards - 1 else 2_000_000
            world.add_link(Link(source, target, latency=latency, bandwidth=config.bandwidth,
                                loss=config.loss, duplicate=config.duplicate, jitter=latency // 20))
    # Copy caller-owned physical descriptions so one reused assembly cannot
    # accidentally share mutable host/link configuration between experiments.
    for host in assembly.hosts:
        world.add_host(replace(host))
    for link in assembly.links:
        world.add_link(replace(link))
    initial = initial_state(config)
    for shard in range(config.shards):
        for i, name in enumerate(witness_names(shard)):
            factory = assembly.factories.get(name, lambda shard=shard, i=i: Witness(shard, strategy, leader=i == 0))
            world.add_actor(name, placement[name], factory)
        for i, name in enumerate(consumer_names(shard)):
            factory = assembly.factories.get(name, lambda shard=shard, i=i: Consumer(
                shard, initial, strategy, i, replica_delays[i]))
            world.add_actor(name, placement[name], factory)
    for shard in range(2):
        name = f"coordinator{shard}"
        factory = assembly.factories.get(name, lambda: Coordinator(strategy, config))
        world.add_actor(name, placement[name], factory)
    world.add_actor("client", placement["client"], assembly.factories.get("client", lambda: Client(strategy)))
    plans = workload(config, seed) if offers else []
    for plan in plans:
        world.inject("client", "offer", plan, at=plan["at"])
    # Harness registry used only by observers; actors never receive World.
    world.contention_offers, world.contention_config = plans, config
    world.contention_strategy = strategy
    until = config.start_ns + config.count * config.interval_ns + config.drain_ns
    return world, plans, until


def audit(world):
    """Quorum/replica checks plus independent transaction and publication checks."""
    errors, writes, snapshots, folded = [], {}, {}, {}
    for event in world.trace:
        if event["kind"] == "durable_write":
            writes[(event["actor"], event["key"])] = event["value"]
        if event["kind"] == "replicated_transition":
            record = writes.get((event["actor"], f'epoch/{event["epoch"]}'))
            if record is None or event["request"] not in record["batch"]["requests"]:
                errors.append(f'transition without retained agreed input {event["actor"]}/{event["epoch"]}')
        if event["kind"] != "replicated_fixpoint":
            continue
        actor, epoch = event["actor"], event["epoch"]
        record = writes.get((actor, f"epoch/{epoch}"))
        if record is None:
            errors.append(f"fold without durable epoch {actor}/{epoch}")
            continue
        batch = record["batch"]
        if not proof_valid(event["shard"], batch, record["proof"]):
            errors.append(f"fold without matching quorum {actor}/{epoch}")
        previous = writes.get((actor, f"epoch/{epoch - 1}"))
        predecessor = digest(previous["batch"]) if previous else None
        if (batch["epoch"] != epoch or batch["shard"] != event["shard"]
                or batch["ballot"] != BALLOT or batch["previous"] != predecessor
                or event["input_hash"] != digest(batch)):
            errors.append(f"fold input differs from retained contiguous history {actor}/{epoch}")
        for vote in record["proof"]:
            if writes.get((vote["voter"], f"log/{epoch}")) != batch:
                errors.append(f"receipt without actual durable input {vote['voter']}/{epoch}")
        identity = (actor, event["incarnation"])
        if epoch != folded.get(identity, 0) + 1:
            errors.append(f"noncontiguous fold {actor}/{epoch}")
        folded[identity] = epoch
        signature = (event["input_hash"], event["logical_hash"], event["output_hash"])
        key = (event["shard"], epoch)
        if key in snapshots and snapshots[key] != signature:
            errors.append(f"replica state or protocol-output divergence {key}")
        snapshots[key] = signature
        if digest(event["state"]) != event["logical_hash"] or digest(event["outputs"]) != event["output_hash"]:
            errors.append(f"incorrect emitted state digest {actor}/{epoch}")
    return sorted(set(errors + audit_transactions(world)))


def audit_transactions(world):
    """Reconstruct application snapshots/outcomes independently of Shard.apply.

    The offered-plan registry is harness input, not algorithm evidence. Accepted
    positions/outcomes and installed effects are taken from actual writes and
    primary consumer folds. Replica agreement alone cannot establish correctness.
    """
    plans = {p["id"]: p for p in world.contention_offers}
    if not plans:  # Direct protocol-input tests have no application transactions.
        return []
    errors, positions, outcomes, installed = [], {}, {}, {}
    complete, reads = set(), []
    pending = {}
    for event in world.trace:
        kind, tx = event["kind"], event.get("op")
        if kind == "durable_write" and event["key"].startswith("outcome/"):
            tx = int(event["key"].split("/")[1])
            value = event["value"]
            if tx in outcomes and outcomes[tx] != value:
                errors.append(f"conflicting durable outcome {tx}")
            outcomes[tx] = value
        elif kind == "durable_write" and event["key"].startswith("position/"):
            tx = int(event["key"].split("/")[1])
            if tx in positions and positions[tx] != event["value"]:
                errors.append(f"position changed {tx}")
            positions[tx] = event["value"]
        elif kind == "replicated_transition" and event["replica"] == 0:
            request, shard = event["request"], event["shard"]
            tx = request["tx"]
            if request["kind"] == "fix":
                if positions.get(tx) != request["position"]:
                    errors.append(f"installed position differs from durable assignment {tx}")
                pending[tx, shard] = request["position"]
            if request["kind"] == "resolve":
                if tx not in outcomes:
                    errors.append(f"install before durable decision {tx}")
                else:
                    expected = {k: v for k, v in outcomes[tx]["values"].items() if shard_of(k) == shard}
                    if request["values"] != expected:
                        errors.append(f"installed effect differs from decision {tx}/{shard}")
                installed[tx, shard] = request["values"]
        elif kind == "replicated_output" and event["replica"] == 0:
            request, shard = event["request"], event["shard"]
            tx = request["tx"]
            if request["kind"] == "announce" and (tx, shard) not in pending:
                pending[tx, shard] = event["result"]["minimum"]
            if request["kind"] == "read":
                for (other, output_shard), position in pending.items():
                    if (other == tx or output_shard != shard or position > request["position"]
                            or (other, shard) in installed):
                        continue
                    overlap = set(plans[other]["writes"]) & set(request["keys"])
                    superseded = world.contention_strategy.supersede and all(
                        any(key in values and position < positions.get(replacement, 0) <= request["position"]
                            for (replacement, location), values in installed.items() if location == shard)
                        for key in overlap)
                    if overlap and not superseded:
                        errors.append(f"read {tx} skipped pending output {other}/{shard}")
        elif kind == "traffic_read":
            reads.append(event)
        elif kind == "traffic_complete":
            if tx not in plans or tx not in outcomes:
                errors.append(f"completion without offered durable outcome {tx}")
            elif any((tx, shard) not in installed for shard in group(plans[tx]["writes"])):
                errors.append(f"completion before all output installs {tx}")
            else:
                complete.add(tx)
        elif kind == "traffic_response" and tx not in complete:
            errors.append(f"client response without complete durable transaction {tx}")
    initial = initial_state(world.contention_config)
    def snapshot(tx, position, key):
        candidates = [(0, initial[key])] + [(outcome["position"], outcome["values"][key])
            for other, outcome in outcomes.items() if other != tx and outcome["position"] <= position
            and key in outcome["values"]]
        return max(candidates)[1]
    for event in reads:
        for key, value in event["values"].items():
            if value != snapshot(event["op"], event["position"], key):
                errors.append(f"wrong snapshot {event['op']}/{key}")
    for tx, outcome in outcomes.items():
        if tx not in plans:
            errors.append(f"durable outcome without offered transaction {tx}")
            continue
        plan, values = plans[tx], outcome["observed"]
        if outcome["position"] != positions.get(tx):
            errors.append(f"decision differs from durable position {tx}")
        if set(values) != set(plan["reads"]):
            errors.append(f"missing observations {tx}")
            continue
        if any(value != snapshot(tx, outcome["position"], key) for key, value in values.items()):
            errors.append(f"decision used wrong snapshot {tx}")
        name = plan["program"]
        if name == "set": expected = dict.fromkeys(plan["writes"], plan["value"])
        elif name == "increment": expected = {k: values[k] + 1 for k in plan["writes"]}
        elif name == "sum": expected = {plan["writes"][0]: sum(values[k] for k in plan["reads"])}
        elif name == "max":
            key = sorted(values, key=lambda k: (values[k], k))[-1]
            expected = {key: values[key] + 1}
        elif name == "conditional":
            expected = {} if values[plan["reads"][0]] % 2 else {k: values[k] + 1 for k in plan["writes"]}
        elif name == "transfer":
            a, b = plan["reads"]
            expected = {a: values[a] - 1, b: values[b] + 1} if values[a] > 0 else {}
        else:
            raise ValueError(name)
        if outcome["values"] != expected or set(expected) - set(plan["writes"]):
            errors.append(f"wrong effects {tx}")
    for actor in world.actors.values():
        if not isinstance(actor.actor, Consumer) or not actor.actor.ready:
            continue
        consumer = actor.actor
        for key, versions in consumer.history.versions.items():
            expected = [(0, initial[key])]
            for tx, ticket in consumer.history.tickets.items():
                if ticket.get("resolved") and tx in outcomes and key in outcomes[tx]["values"]:
                    expected.append((outcomes[tx]["position"], outcomes[tx]["values"][key]))
            if sorted(set(versions)) != sorted(set(expected)):
                errors.append(f"retained version history differs {consumer.shard}/{consumer.replica}/{key}")
    return errors
