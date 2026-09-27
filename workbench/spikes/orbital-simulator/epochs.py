"""A small application binding for fixed agreed histories and logical fixpoints.

Inputs are ALREADY AGREED shard epochs with assigned transaction positions. This
tests fold execution, not reservation acquisition or distributed atomic commit.
Object scopes here are sets of integer-valued cells, interpreted only here.
"""
from __future__ import annotations

from kernel import clone, digest
from sim import Actor, Host, World


class CellHistory:
    def __init__(self, initial, ignore_pending=False):
        self.versions = {key: [(0, value)] for key, value in initial.items()}
        self.outputs = {}
        self.reads = {}
        self.bounds = {}
        self.results = {}
        self.ignore_pending = ignore_pending

    def apply(self, command):
        kind, tx = command["kind"], command["tx"]
        if kind == "announce":
            effects = sorted(command["effects"])
            position = command["position"]
            if any(position <= self.bounds.get(scope, -1) for scope in effects):
                raise ValueError("position violates an earlier registered read")
            if tx in self.outputs:
                if self.outputs[tx]["position"] != position:
                    raise ValueError("transaction position changed")
                return
            self.outputs[tx] = dict(position=position, scopes=effects, status="pending")
        elif kind == "read":
            read_id = command.get("read_id", tx)
            if read_id in self.reads:
                if self.reads[read_id] != command:
                    raise ValueError("read identity reused with different inputs")
                return
            position = command["position"]
            for scope in command["scopes"]:
                self.bounds[scope] = max(position, self.bounds.get(scope, -1))
            self.reads[read_id] = clone(command)
        elif kind == "resolve":
            pending = self.outputs[tx]
            effects = command.get("values", {})
            if set(effects) - set(pending["scopes"]):
                raise ValueError("undeclared effect")
            if pending["status"] != "pending":
                raise ValueError("duplicate or contradictory outcome")
            pending["status"] = command.get("status", "commit")
            if pending["status"] == "commit":
                for scope, value in effects.items():
                    self.versions.setdefault(scope, []).append((pending["position"], value))
                    self.versions[scope].sort()
        else:
            raise ValueError(f"unknown application input {kind}")

    def blockers(self, tx):
        read = self.reads[tx]
        return sorted(other for other, output in self.outputs.items()
                      if other != read["tx"] and output["status"] == "pending"
                      and output["position"] <= read["position"]
                      and set(output["scopes"]) & set(read["scopes"]))

    def fixpoint(self, reverse=False):
        completed = []
        # Alternate enabled-work order is separate from the agreed command order.
        for tx in sorted(self.reads, reverse=reverse):
            if tx in self.results or (self.blockers(tx) and not self.ignore_pending):
                continue
            read = self.reads[tx]
            values = {}
            for scope in read["scopes"]:
                candidates = [(p, v) for p, v in self.versions.get(scope, [])
                              if p <= read["position"]]
                if not candidates:
                    raise ValueError("requested version has no reconstructible base")
                values[scope] = max(candidates)[1]
            self.results[tx] = values
            completed.append((tx, values))
        return completed

    def logical_state(self):
        return clone(dict(versions=self.versions, outputs=self.outputs,
                          bounds=self.bounds, reads=self.reads, results=self.results,
                          pending={tx: self.blockers(tx) for tx in self.reads
                                   if tx not in self.results}))


class EpochConsumer(Actor):
    def __init__(self, initial, cost=100, reverse=False, ignore_pending=False):
        self.history = CellHistory(initial, ignore_pending)
        self.cost, self.reverse = cost, reverse
        self.epochs = {}
        self.next_epoch = 1
        self.running = False
        self.ready = False

    def on(self, ctx, kind, data):
        if kind == "boot":
            if not ctx.scan("epoch/", "recovered"):
                ctx.timer(100, "boot")
        elif kind == "recovered":
            if not data["ok"]:
                ctx.timer(100, "boot")
                return
            for key, commands in data["records"].items():
                self.epochs[int(key.split("/")[1])] = commands
            self.ready = True
            self.start(ctx)
        elif kind == "epoch":
            epoch = data["epoch"]
            if epoch < self.next_epoch:
                return
            if not ctx.persist(f"epoch/{epoch}", data["commands"], "stored", data):
                ctx.wait(f"epoch-{epoch}", "journal capacity")
                ctx.timer(100, "epoch", data)
        elif kind == "stored" and data["ok"]:
            epoch = data["epoch"]
            old = self.epochs.get(epoch)
            if old is not None and old != data["commands"]:
                raise ValueError("conflicting agreed history")
            self.epochs[epoch] = data["commands"]
            ctx.clear_wait(f"epoch-{epoch}")
            self.start(ctx)
        elif kind == "fold":
            commands = self.epochs[self.next_epoch]
            for command in commands:
                self.history.apply(command)
                # A preceding logical effect may be read in this same epoch.
                self.observe(ctx)
            state = self.history.logical_state()
            ctx.note("epoch_fixpoint", epoch=self.next_epoch, state=state,
                     logical_hash=digest(state))
            self.next_epoch += 1
            self.running = False
            self.start(ctx)
        elif kind == "retry":
            self.start(ctx)

    def observe(self, ctx):
        for tx, values in self.history.fixpoint(self.reverse):
            ctx.clear_wait(tx)
            ctx.note("snapshot_result", op=tx, values=values)
        for tx in self.history.reads:
            if tx not in self.history.results:
                ctx.wait(tx, "earlier pending effects", predecessors=self.history.blockers(tx))

    def start(self, ctx):
        if self.ready and not self.running and self.next_epoch in self.epochs:
            cost = self.cost * max(1, len(self.epochs[self.next_epoch]))
            if ctx.compute(cost, "fold"):
                self.running = True
            else:
                ctx.timer(100, "retry")


def broad_read_history(width=16):
    initial = {f"s{i}": i for i in range(width)} | {"answer": 0, "unrelated": 0}
    first = [dict(kind="announce", tx="analysis", effects=["answer"], position=10),
             dict(kind="read", tx="analysis", scopes=[f"s{i}" for i in range(width)], position=10),
             dict(kind="read", tx="dependent", scopes=["answer"], position=15)]
    for i in range(width):
        first.extend([dict(kind="announce", tx=f"w{i}", effects=[f"s{i}"], position=11),
                      dict(kind="resolve", tx=f"w{i}", values={f"s{i}": 100 + i})])
    first.extend([dict(kind="announce", tx="local", effects=["unrelated"], position=12),
                  dict(kind="resolve", tx="local", values={"unrelated": 7}),
                  dict(kind="read", tx="local-result", scopes=["unrelated"], position=12)])
    second = [dict(kind="resolve", tx="analysis", values={"answer": sum(range(width))})]
    return initial, [first, second]


def build_scenario(width=16, seed=1, ordering="fifo", ignore_pending=False, replay=None):
    world = World(seed=seed, ordering=ordering, replay=replay)
    initial, epochs = broad_read_history(width)
    for i, speed in enumerate((20, 90, 150)):
        name = f"consumer{i}"
        world.add_host(Host(name, handler_ns=10, disk_latency=100,
                            disk_bytes_per_ns=20, nic_bytes_per_ns=20))
        world.add_actor(name, name, lambda speed=speed, i=i: EpochConsumer(
            initial, speed, reverse=bool(i % 2), ignore_pending=ignore_pending))
        # Different delivery order; journal ordinal, never arrival order, governs fold.
        world.inject(name, "epoch", dict(epoch=2, commands=epochs[1]), at=100_000 + i * 300)
        world.inject(name, "epoch", dict(epoch=1, commands=epochs[0]), at=100 + i * 80)
    return world


def audit(world, width=16):
    errors = []
    expected = {"analysis": {f"s{i}": i for i in range(width)},
                "dependent": {"answer": sum(range(width))}, "local-result": {"unrelated": 7}}
    results = [e for e in world.trace if e["kind"] == "snapshot_result"]
    for row in results:
        if row["values"] != expected[row["op"]]:
            errors.append(f"{row['actor']} returned wrong snapshot for {row['op']}")
    fixes = [e for e in world.trace if e["kind"] == "epoch_fixpoint"]
    for epoch in (1, 2):
        if len({e["logical_hash"] for e in fixes if e["epoch"] == epoch}) > 1:
            errors.append(f"epoch {epoch} has unequal logical fixpoints")
    return errors
