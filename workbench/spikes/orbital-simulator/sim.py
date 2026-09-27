"""Physical environment and actor ports for the Orbital simulator learning spike.

This module knows machines and resources, not transactions, votes or objects.
"""
from __future__ import annotations

from collections import Counter, deque
from dataclasses import dataclass, field
import math

from kernel import Kernel, ReplayMismatch, clone, digest


def encoded_size(value):
    import json
    return len(json.dumps(value, sort_keys=True).encode())


@dataclass
class Host:
    name: str
    domain: str = "local"
    workers: int = 2
    control: int = 1
    handler_ns: int = 1_000
    memory_bytes: int = 1 << 20
    durable_bytes: int = 16 << 20
    nic_bytes_per_ns: float = 1
    disk_bytes_per_ns: float = .5
    disk_latency: int = 20_000
    io_slots: int = 4
    queue_limit: int = 1024
    clock_offset: int = 0
    clock_ppm: int = 0


@dataclass
class Link:
    source: str
    target: str
    latency: int = 10_000
    bandwidth: float = 1
    loss: float = 0
    duplicate: float = 0
    jitter: int = 0
    queue_limit: int = 1024


class Actor:
    def on(self, ctx, kind, data):
        raise NotImplementedError


@dataclass
class ActorState:
    host: str
    factory: object
    actor: Actor | None = None
    incarnation: int = 0
    live: bool = False
    busy: bool = False
    inbox: deque = field(default_factory=deque)
    last_step: int = 0


@dataclass
class Machine:
    config: Host
    up: bool = True
    generation: int = 0
    used: int = 0
    peak: int = 0
    storage: dict = field(default_factory=dict)
    storage_used: int = 0
    storage_reserved: int = 0
    disk_active: int = 0
    disk_queue: deque = field(default_factory=deque)
    disk_requests: dict = field(default_factory=dict)


class Service:
    """Finite FIFO service; policy actors choose which pool receives work."""
    def __init__(self, world, name, slots, limit):
        self.world, self.name, self.slots, self.limit = world, name, slots, limit
        self.active = {}
        self.queue = deque()
        self.submitted = self.completed = self.busy_ns = 0
        self.cancelled = 0
        self.factor = 1
        self.paused = False

    def submit(self, duration, next_kind, data):
        if duration < 0 or not isinstance(duration, int):
            raise ValueError("service duration must be nonnegative integer ns")
        if len(self.queue) + len(self.active) >= self.limit:
            self.world._record("resource_refused", resource=self.name)
            return False
        self.submitted += 1
        job = dict(identity=self.submitted, duration=duration,
                   next_kind=next_kind, data=clone(data),
                   parent=self.world._record("resource_queue", resource=self.name,
                                             duration=duration, parents=[data.get("_origin", 0)]))
        self.queue.append(job)
        self.pump()
        return True

    def pump(self):
        while not self.paused and self.queue and len(self.active) < self.slots:
            job = self.queue.popleft()
            job["duration"] = math.ceil(job["duration"] * self.factor)
            job["started"] = self.world.kernel.now
            self.active[job["identity"]] = job
            cause = self.world.kernel.cause
            self.world.kernel.cause = self.world._record("resource_start", resource=self.name,
                               job=job["identity"], parents=[job["parent"]])
            self.world.kernel.schedule(job["duration"], "service_done",
                                       dict(resource=self.name, job=job["identity"]))
            self.world.kernel.cause = cause

    def finish(self, identity):
        job = self.active.pop(identity, None)
        if job is None:
            self.world._record("retired_service_completion", resource=self.name, job=identity)
            return
        self.completed += 1
        self.busy_ns += job["duration"]
        cause = self.world.kernel.cause
        self.world.kernel.cause = self.world._record("resource_done", resource=self.name, job=identity)
        self.world._dispatch(job["next_kind"], job["data"])
        self.pump()
        self.world.kernel.cause = cause

    def reset(self):
        """A device reset retires users; process death alone does not."""
        jobs = list(self.active.values()) + list(self.queue)
        for job in self.active.values():
            self.busy_ns += self.world.kernel.now - job["started"]
        self.active.clear()
        self.queue.clear()
        self.cancelled += len(jobs)
        for job in jobs:
            self.world._cancel_completion(job["next_kind"], job["data"])
        self.world._record("resource_reset", resource=self.name, cancelled=len(jobs))

    def report(self):
        active_busy = sum(self.world.kernel.now - job["started"] for job in self.active.values())
        return dict(submitted=self.submitted, completed=self.completed, cancelled=self.cancelled,
                    busy_ns=self.busy_ns + active_busy, active_busy_ns=active_busy,
                    active=len(self.active), queued=len(self.queue), paused=self.paused,
                    factor=self.factor)


class Context:
    """Capability-shaped API, not a Python security sandbox."""
    def __init__(self, world, actor):
        self.__world = world
        self.actor = actor
        process = world.actors[actor]
        self.host, self.incarnation = process.host, process.incarnation

    @property
    def now(self):
        host = self.__world.hosts[self.host].config
        return host.clock_offset + self.__world.kernel.now * (1_000_000 + host.clock_ppm) // 1_000_000

    def send(self, target, kind, data, size=128):
        return self.__world._send(self.actor, target, kind, data, size)

    def timer(self, delay, kind, data=None):
        host = self.__world.hosts[self.host].config
        delay = math.ceil(delay * 1_000_000 / (1_000_000 + host.clock_ppm))
        self.__world.kernel.schedule(delay, "deliver", dict(actor=self.actor,
            incarnation=self.incarnation, kind=kind, data=data or {}, held=[]))

    def compute(self, duration, then, data=None, pool="workers", leases=()):
        if pool not in ("workers", "control"):
            raise ValueError("unknown CPU pool")
        w = self.__world
        held = list(leases)
        for lease in held:
            w._check_owner(lease, self.actor, self.incarnation)
        for lease in held:
            w.leases[lease]["refs"] += 1
        accepted = w.resources[f"{self.host}/{pool}"].submit(duration, "deliver", dict(
            actor=self.actor, incarnation=self.incarnation, kind=then,
            data=data or {}, held=held))
        if not accepted:
            for lease in held:
                w._unpin(lease)
        return accepted

    def persist(self, key, value, then, data=None, size=None):
        return self.__world._disk(self.actor, "write", key, value, then,
                                  data or {}, size)

    def load(self, key, then, data=None):
        return self.__world._disk(self.actor, "read", key, None, then, data or {}, None)

    def scan(self, prefix, then, data=None):
        return self.__world._disk(self.actor, "scan", prefix, None, then, data or {}, None)

    def reserve(self, size, label):
        return self.__world._reserve(self.host, self.actor, self.incarnation, size, label)

    def release(self, lease):
        w = self.__world
        w._check_owner(lease, self.actor, self.incarnation)
        w.leases[lease]["owner"] = False
        w._unpin(lease)

    def note(self, kind, op=None, **fields):
        return self.__world._record(kind, actor=self.actor, incarnation=self.incarnation,
                                    op=op, **fields)

    def wait(self, op, reason, **fields):
        w = self.__world
        identity = self.note("wait", op=op, reason=reason, **fields)
        w.waits[(self.actor, str(op))] = dict(actor=self.actor, op=op,
            incarnation=self.incarnation, reason=reason, trace=identity, **clone(fields))

    def clear_wait(self, op):
        self.__world.waits.pop((self.actor, str(op)), None)
        self.note("wait_cleared", op=op)


class World:
    def __init__(self, seed=1, ordering="fifo", replay=None):
        self.kernel = Kernel(seed, ordering, replay)
        self.kernel.dispatch = self._dispatch
        self.kernel.listener = self._observe
        self.hosts: dict[str, Machine] = {}
        self.actors: dict[str, ActorState] = {}
        self.links: dict[tuple[str, str], Link] = {}
        self.blocked = set()
        self.resources = {}
        self.leases = {}
        self.lease_serial = 0
        self.packet_serial = Counter()
        self.disk_serial = 0
        self.waits = {}
        self.retired_waits = []
        self.triggers = []

    @property
    def trace(self):
        return self.kernel.trace

    @property
    def decisions(self):
        return self.kernel.decisions

    def _record(self, kind, **fields):
        return self.kernel.record(kind, **fields)

    def add_host(self, host):
        if host.name in self.hosts:
            raise ValueError("duplicate host")
        if min(host.workers, host.control, host.io_slots, host.queue_limit) <= 0:
            raise ValueError("machine service capacities must be positive")
        if min(host.nic_bytes_per_ns, host.disk_bytes_per_ns) <= 0:
            raise ValueError("machine bandwidth must be positive")
        if min(host.memory_bytes, host.durable_bytes, host.handler_ns, host.disk_latency) < 0:
            raise ValueError("negative machine budget")
        if host.clock_ppm <= -1_000_000:
            raise ValueError("local clock must advance")
        self.hosts[host.name] = Machine(host)
        for name, slots in (("workers", host.workers), ("control", host.control),
                            ("tx", 1), ("rx", 1), ("disk_bytes", 1)):
            key = f"{host.name}/{name}"
            self.resources[key] = Service(self, key, slots, host.queue_limit)
        return self

    def add_link(self, link):
        if link.source not in self.hosts or link.target not in self.hosts:
            raise ValueError("link endpoint missing")
        if min(link.latency, link.jitter) < 0 or link.bandwidth <= 0:
            raise ValueError("invalid link cost")
        if not (0 <= link.loss <= 1 and 0 <= link.duplicate <= 1):
            raise ValueError("invalid packet probability")
        self.links[(link.source, link.target)] = link
        key = f"link/{link.source}/{link.target}"
        self.resources[key] = Service(self, key, 1, link.queue_limit)
        return self

    def add_actor(self, name, host, factory):
        if name in self.actors or host not in self.hosts:
            raise ValueError("duplicate actor or missing host")
        self.actors[name] = ActorState(host, factory)
        self.restart(name)
        return self

    def inject(self, actor, kind, data=None, at=0):
        """Harness input, not a protocol's omniscient send operation."""
        if actor not in self.actors:
            raise ValueError("unknown actor")
        self.kernel.schedule(at - self.kernel.now, "inject", dict(
            actor=actor, kind=kind, data=data or {}))

    def fault(self, at, action, **args):
        if action not in ("crash", "restart", "power_loss", "power_on", "destroy", "partition",
                          "slowdown", "pause"):
            raise ValueError("unknown fault")
        self.kernel.schedule(at - self.kernel.now, "fault", dict(action=action, args=args))

    def when(self, kind, predicate, action, **args):
        self.triggers.append(dict(kind=kind, predicate=predicate, action=action, args=args))

    def _observe(self, entry):
        for trigger in list(self.triggers):
            if trigger["kind"] == entry["kind"] and trigger["predicate"](clone(entry)):
                self.triggers.remove(trigger)
                cause = self.kernel.cause
                self.kernel.cause = entry["id"]
                self.kernel.schedule(0, "fault", dict(action=trigger["action"], args=trigger["args"]))
                self.kernel.cause = cause

    def _valid(self, actor, incarnation):
        p = self.actors[actor]
        return p.live and p.incarnation == incarnation and self.hosts[p.host].up

    def _reserve(self, host, actor, incarnation, size, label, owner=True):
        if not isinstance(size, int) or size < 0:
            raise ValueError("invalid resident size")
        machine = self.hosts[host]
        if not machine.up or machine.used + size > machine.config.memory_bytes:
            self._record("memory_refused", host=host, actor=actor, size=size, label=label)
            return None
        self.lease_serial += 1
        identity = self.lease_serial
        self.leases[identity] = dict(host=host, actor=actor, incarnation=incarnation,
                                    size=size, label=label, refs=1, owner=owner)
        machine.used += size
        machine.peak = max(machine.peak, machine.used)
        self._record("memory_acquire", host=host, actor=actor, lease=identity, size=size, label=label)
        return identity

    def _check_owner(self, lease, actor, incarnation):
        item = self.leases.get(lease)
        if item is None or not item["owner"] or (item["actor"], item["incarnation"]) != (actor, incarnation):
            raise ValueError("lease does not belong to this actor incarnation")

    def _unpin(self, lease):
        item = self.leases[lease]
        item["refs"] -= 1
        assert item["refs"] >= 0
        if item["refs"] == 0:
            self.hosts[item["host"]].used -= item["size"]
            self._record("memory_release", host=item["host"], actor=item["actor"],
                         lease=lease, size=item["size"])
            del self.leases[lease]

    def crash(self, actor):
        process = self.actors[actor]
        process.live = False
        process.actor = None
        process.busy = False
        process.last_step = 0
        while process.inbox:
            for lease in process.inbox.popleft().get("held", []):
                self._unpin(lease)
        for identity, lease in list(self.leases.items()):
            if lease["actor"] == actor and lease["owner"]:
                lease["owner"] = False
                self._unpin(identity)
        for key, wait in list(self.waits.items()):
            if key[0] == actor:
                self.retired_waits.append(dict(wait, retirement="reporting actor lost"))
                self._record("wait_owner_lost", actor=actor, op=wait["op"],
                             parents=[wait["trace"]])
                del self.waits[key]
        self._record("process_crash", actor=actor, incarnation=process.incarnation)

    def restart(self, actor):
        process = self.actors[actor]
        if not self.hosts[process.host].up:
            raise ValueError("cannot start a process on a powered-off machine")
        if process.live:
            raise ValueError("crash a live process before restarting it")
        process.incarnation += 1
        process.actor = process.factory()
        process.live = True
        process.busy = False
        self._record("process_start", actor=actor, incarnation=process.incarnation)
        self.kernel.schedule(0, "deliver", dict(actor=actor,
            incarnation=process.incarnation, kind="boot", data={}, held=[]))

    def power_loss(self, host):
        machine = self.hosts[host]
        machine.up = False
        machine.generation += 1
        for name, process in self.actors.items():
            if process.host == host:
                self.crash(name)
        for name, resource in self.resources.items():
            if name.startswith(host + "/"):
                resource.reset()
        for request in machine.disk_requests.values():
            for lease in request["held"]:
                self._unpin(lease)
            self._record("disk_cancelled", actor=request["actor"], key=request["key"])
        machine.disk_requests.clear()
        machine.disk_queue.clear()
        machine.disk_active = machine.storage_reserved = 0
        self._record("power_loss", host=host)

    def _cancel_completion(self, kind, data):
        if kind in ("handler", "deliver"):
            for lease in data.get("held", []):
                self._unpin(lease)
        elif kind == "sent":
            self._unpin(data["lease"])
        elif kind == "disk_latency":
            pass  # The device owns these requests until its reset below.
        else:
            raise ValueError(f"unhandled device cancellation: {kind}")

    def power_on(self, host):
        machine = self.hosts[host]
        if machine.up:
            raise ValueError("machine already powered on")
        machine.up = True
        self._record("power_on", host=host)
        for name, process in self.actors.items():
            if process.host == host:
                self.restart(name)

    def destroy(self, host):
        self.power_loss(host)
        self.hosts[host].storage.clear()
        self.hosts[host].storage_used = 0
        self._record("storage_destroyed", host=host)

    def partition(self, source_host, target_host, blocked=True):
        edge = (source_host, target_host)
        self.blocked.add(edge) if blocked else self.blocked.discard(edge)
        self._record("partition", source=source_host, target=target_host, blocked=blocked)

    def slowdown(self, host, resource, factor):
        if factor <= 0 or not math.isfinite(factor):
            raise ValueError("service factor must be finite and positive")
        self.resources[f"{host}/{resource}"].factor = factor
        self._record("slowdown", host=host, resource=resource, factor=factor)

    def pause(self, host, resource, paused=True):
        """Pause new service starts; active operations keep their booked cost."""
        service = self.resources[f"{host}/{resource}"]
        service.paused = paused
        self._record("service_pause", host=host, resource=resource, paused=paused)
        service.pump()

    def durable(self, host, actor):
        return clone({k: v[0] for (a, k), v in self.hosts[host].storage.items() if a == actor})

    def _deliver(self, data):
        actor = data["actor"]
        if not self._valid(actor, data["incarnation"]):
            for lease in data.get("held", []):
                self._unpin(lease)
            self._record("stale_completion", actor=actor, incarnation=data["incarnation"])
            return
        origin = self._record("mailbox_enqueue", actor=actor, message_kind=data["kind"],
                              parents=[data.get("_origin", 0)])
        self.actors[actor].inbox.append(dict(data, _origin=origin))
        self._pump_actor(actor)

    def _pump_actor(self, actor):
        p = self.actors[actor]
        if not p.live or p.busy or not p.inbox:
            return
        message = p.inbox[0]
        accepted = self.resources[f"{p.host}/control"].submit(
            self.hosts[p.host].config.handler_ns, "handler", message)
        if accepted:
            p.inbox.popleft()
            p.busy = True
        else:
            self.kernel.schedule(max(1, self.hosts[p.host].config.handler_ns),
                                 "pump_actor", dict(actor=actor))

    def _handler(self, message):
        actor = message["actor"]
        if self._valid(actor, message["incarnation"]):
            p = self.actors[actor]
            cause = self.kernel.cause
            p.last_step = self._record("actor_step", actor=actor, incarnation=p.incarnation,
                         input_kind=message["kind"], parents=[p.last_step])
            self.kernel.cause = p.last_step
            p.actor.on(Context(self, actor), message["kind"], clone(message["data"]))
            self.kernel.cause = cause
            p.busy = False
        else:
            self._record("stale_completion", actor=actor, incarnation=message["incarnation"])
        for lease in message.get("held", []):
            self._unpin(lease)
        self._pump_actor(actor)

    def _send(self, actor, target, kind, data, size):
        if target not in self.actors:
            raise ValueError("unknown logical endpoint")
        p = self.actors[actor]
        lease = self._reserve(p.host, actor, p.incarnation, size, "send", owner=False)
        if lease is None:
            return False
        self.packet_serial[(actor, target, kind)] += 1
        packet = dict(source=p.host, target_host=self.actors[target].host,
                      source_actor=actor, source_incarnation=p.incarnation,
                      source_generation=self.hosts[p.host].generation,
                      actor=target, kind=kind, data=clone(data), size=size, lease=lease,
                      identity=[actor, target, kind, self.packet_serial[(actor, target, kind)]])
        if not self.resources[f"{p.host}/tx"].submit(
            math.ceil(size / self.hosts[p.host].config.nic_bytes_per_ns), "sent", packet):
            self._unpin(lease)
            return False
        self._record("send_accepted", actor=actor, target=target, message_kind=kind, size=size)
        return True

    def _sent(self, packet):
        self._unpin(packet["lease"])
        source = self.hosts[packet["source"]]
        # A submitted NIC operation can outlive process death, but not a device reset.
        if not source.up or source.generation != packet["source_generation"]:
            self._record("packet_drop", reason="sender_power", packet=packet["identity"])
            return
        self._record("packet_departed", actor=packet["source_actor"], target=packet["actor"],
                     packet=packet["identity"], size=packet["size"])
        edge = (packet["source"], packet["target_host"])
        if edge[0] == edge[1]:
            self.kernel.schedule(0, "arrive", packet)
        elif edge not in self.links:
            self._record("packet_drop", reason="no_route", packet=packet["identity"])
        else:
            link = self.links[edge]
            if not self.resources[f"link/{edge[0]}/{edge[1]}"].submit(
                math.ceil(packet["size"] / link.bandwidth), "wire", packet):
                self._record("packet_drop", reason="link_queue", packet=packet["identity"])

    def _wire(self, packet):
        edge = (packet["source"], packet["target_host"])
        link = self.links[edge]
        self._record("wire_transmitted", source=packet["source"], target=packet["target_host"],
                     source_actor=packet["source_actor"], target_actor=packet["actor"],
                     packet=packet["identity"], size=packet["size"])
        if edge in self.blocked or self.kernel.sample("loss", packet["identity"]) < link.loss:
            self._record("packet_drop", reason="wire", packet=packet["identity"])
            return
        delay = link.latency + int(link.jitter * self.kernel.sample("jitter", packet["identity"]))
        self.kernel.schedule(delay, "arrive", packet)
        if self.kernel.sample("duplicate", packet["identity"]) < link.duplicate:
            self.kernel.schedule(delay + 1, "arrive", packet)
            self._record("packet_duplicate", packet=packet["identity"])

    def _arrive(self, packet):
        p = self.actors[packet["actor"]]
        lease = self._reserve(p.host, packet["actor"], p.incarnation,
                              packet["size"], "receive", owner=False)
        if lease is None:
            self._record("packet_drop", reason="receive_memory", packet=packet["identity"])
            return
        data = dict(actor=packet["actor"], incarnation=p.incarnation,
                    kind=packet["kind"], data=packet["data"], held=[lease])
        if not self.resources[f"{p.host}/rx"].submit(
            math.ceil(packet["size"] / self.hosts[p.host].config.nic_bytes_per_ns),
            "deliver", data):
            self._unpin(lease)
            self._record("packet_drop", reason="receive_queue", packet=packet["identity"])

    def _disk(self, actor, mode, key, value, then, data, size):
        p = self.actors[actor]
        machine = self.hosts[p.host]
        storage = machine.storage
        parents = []
        if mode == "write":
            value = clone(value)
            size = encoded_size(value) if size is None else size
        elif mode == "read":
            record = storage.get((actor, key), (None, 1, 0))
            value, size = record[:2]
            parents = [record[2]]
        else:
            matching = {k: v for (a, k), v in storage.items() if a == actor and k.startswith(key)}
            value = {k: v[0] for k, v in matching.items()}
            size = max(1, sum(v[1] for v in matching.values()))
            parents = [v[2] for v in matching.values()]
        if len(machine.disk_queue) + machine.disk_active >= machine.config.queue_limit:
            self._record("disk_refused", actor=actor, reason="queue")
            return False
        if mode == "write" and machine.storage_used + machine.storage_reserved + size > machine.config.durable_bytes:
            self._record("disk_refused", actor=actor, reason="capacity")
            return False
        lease = self._reserve(p.host, actor, p.incarnation, size, "disk", owner=False)
        if lease is None:
            return False
        if mode == "write":
            machine.storage_reserved += size
        self.disk_serial += 1
        request = dict(actor=actor, incarnation=p.incarnation, host=p.host,
            generation=machine.generation, mode=mode, key=key, value=clone(value),
            size=size, then=then, data=clone(data), held=[lease], request=self.disk_serial)
        request["_origin"] = self._record("disk_submit", actor=actor, mode=mode,
                                           key=key, size=size, parents=parents)
        machine.disk_requests[self.disk_serial] = request
        machine.disk_queue.append(request)
        self._pump_disk(p.host)
        return True

    def _pump_disk(self, host):
        machine = self.hosts[host]
        while machine.disk_queue and machine.disk_active < machine.config.io_slots:
            request = machine.disk_queue.popleft()
            machine.disk_active += 1
            accepted = self.resources[f"{host}/disk_bytes"].submit(
                math.ceil(request["size"] / machine.config.disk_bytes_per_ns),
                "disk_latency", request)
            assert accepted, "disk queue bounds must cover the active device requests"

    def _disk_finish(self, request):
        machine = self.hosts[request["host"]]
        if machine.disk_requests.pop(request["request"], None) is None:
            self._record("retired_disk_completion", request=request["request"])
            return
        machine.disk_active -= 1
        ok = machine.up and machine.generation == request["generation"]
        result = dict(request["data"], ok=ok, key=request["key"])
        if request["mode"] == "write":
            machine.storage_reserved -= request["size"]
            if ok:
                key = (request["actor"], request["key"])
                old = machine.storage.get(key, (None, 0))[1]
                machine.storage_used += request["size"] - old
                origin = self._record("durable_write", actor=request["actor"], host=request["host"],
                             key=request["key"], value=request["value"], size=request["size"])
                machine.storage[key] = (request["value"], request["size"], origin)
        elif request["mode"] == "read":
            result["value"] = request["value"] if ok else None
        else:
            result["records"] = request["value"] if ok else {}
        self._deliver(dict(actor=request["actor"], incarnation=request["incarnation"],
                           kind=request["then"], data=result, held=request["held"]))
        self._pump_disk(request["host"])

    def _dispatch(self, kind, data):
        if kind == "service_done":
            self.resources[data["resource"]].finish(data["job"])
        elif kind == "fault":
            getattr(self, data["action"])(**data["args"])
        elif kind == "inject":
            data = dict(data, incarnation=self.actors[data["actor"]].incarnation, held=[])
            self._deliver(data)
        elif kind == "deliver": self._deliver(data)
        elif kind == "handler": self._handler(data)
        elif kind == "pump_actor": self._pump_actor(data["actor"])
        elif kind == "sent": self._sent(data)
        elif kind == "wire": self._wire(data)
        elif kind == "arrive": self._arrive(data)
        elif kind == "disk_latency":
            self.kernel.schedule(self.hosts[data["host"]].config.disk_latency, "disk_finish", data)
        elif kind == "disk_finish": self._disk_finish(data)
        else: raise ValueError(f"unknown environment event {kind}")

    def run(self, until=None, max_events=1_000_000):
        self.kernel.run(until, max_events)
        return self.report()

    def explain(self, op):
        waits = [self._wait_status(w) for w in self.waits.values() if w["op"] == op]
        roots = [e["id"] for e in self.trace if e.get("op") == op]
        return dict(op=op, waits=clone(waits), causal_history=self.kernel.slice(roots))

    def _wait_status(self, wait):
        p = self.actors[wait["actor"]]
        return dict(wait, reporting_incarnation_alive=p.live and
                    p.incarnation == wait["incarnation"])

    def report(self):
        return dict(time=self.kernel.now, pending_events=len(self.kernel.events),
            trace_hash=digest(self.trace), choices_hash=digest(self.decisions),
            counts=dict(Counter(e["kind"] for e in self.trace)),
            waits=[self._wait_status(w) for w in self.waits.values()],
            retired_waits=clone(self.retired_waits),
            hosts={name: dict(memory_used=m.used, memory_peak=m.peak,
                durable_used=m.storage_used, durable_reserved=m.storage_reserved,
                disk_pending=m.disk_active + len(m.disk_queue), up=m.up)
                for name, m in self.hosts.items()},
            resources={name: resource.report() for name, resource in self.resources.items()})
