"""Partitioned application traffic over the shared actor/physical simulator.

Actors know only Context and immutable deployment configuration. All costs are
synthetic; this is an application composition, not an Orbital protocol or VM.
"""
from __future__ import annotations

from dataclasses import dataclass, replace
import math

from sim import Actor, Host, Link, World


@dataclass(frozen=True)
class FlowConfig:
    enhance: bool = True
    placement: str = "source"  # source, destination, consumer
    chunks: int = 4
    rows: int = 32
    window: int = 2
    relay_slots: int = 4
    sink_slots: int = 8
    retry_ns: int = 80_000
    permit_delay_ns: int = 0
    filter_ns_per_row: int = 120
    consume_ns_per_row: int = 10
    memory_bytes: int = 1 << 18
    durable_bytes: int = 1 << 20
    wan_bytes_per_ns: float = .25
    foreground_count: int = 20
    foreground_period_ns: int = 4_000
    foreground_service_ns: int = 600
    duplicate: float = 0
    seed: int = 1
    ordering: str = "fifo"

    def validate(self):
        if self.placement not in {"source", "destination", "consumer"}:
            raise ValueError("unknown relay placement")
        if min(self.chunks, self.rows, self.window, self.relay_slots,
               self.sink_slots, self.retry_ns) <= 0:
            raise ValueError("positive workload and window bounds required")
        if min(self.permit_delay_ns, self.filter_ns_per_row,
               self.consume_ns_per_row, self.foreground_count) < 0:
            raise ValueError("negative work or delay")


def selected(value):
    return value % 3 == 0


def payload_size(rows):
    return 96 + 8 * len(rows)


def message_size(message):
    return payload_size(message["rows"]) + (math.ceil(len(message["rows"]) / 8)
                                            if "mask" in message else 0)


def token(envelope):
    return f'{envelope["job"]}/{envelope["source"]}/{envelope["seq"]}'


def binding(envelope):
    return tuple(envelope[k] for k in ("job", "source", "seq", "total", "snapshot", "code"))


def manifest(config, source):
    """External authored input. Never called by an actor or recovery factory."""
    index = int(source[-1])
    return dict(job="job-1", source=source, snapshot="snapshot-7", code="filter-1",
                chunks=[[index * 10_000 + chunk * config.rows + row
                         for row in range(config.rows)] for chunk in range(config.chunks)])


class Source(Actor):
    def __init__(self, config, relay, sinks):
        self.config, self.relay, self.sinks = config, relay, tuple(sinks)
        self.job = None
        self.lease = None
        self.active = {}
        self.next_seq = 0
        self.finished = False
        self.loading = False

    def on(self, ctx, kind, data):
        if kind in {"boot", "load_retry"}:
            if not ctx.load("manifest", "loaded"):
                ctx.timer(self.config.retry_ns, "load_retry")
        elif kind == "offer":
            if self.job is not None or self.loading or self.finished:
                ctx.note("flow_refused", op=data["job"], reason="source_busy")
                return
            size = sum(payload_size(rows) for rows in data["chunks"])
            self.lease = ctx.reserve(size, "source_replay")
            if self.lease is None:
                ctx.note("flow_refused", op=data["job"], reason="source_memory")
                return
            self.job, self.loading = data, True
            ctx.note("flow_offered", op=data["job"], chunks=len(data["chunks"]))
            self._save(ctx)
        elif kind == "save_retry":
            self._save(ctx)
        elif kind == "manifest_saved":
            if data["ok"]:
                self.loading = False
                self._start(ctx)
            else:
                ctx.timer(self.config.retry_ns, "save_retry")
        elif kind == "loaded":
            # A fresh process recovers only bytes returned through its storage port.
            if data["ok"] and data["value"] is not None and self.job is None:
                value = data["value"]
                size = sum(payload_size(rows) for rows in value["chunks"])
                self.lease = ctx.reserve(size, "source_replay")
                if self.lease is None:
                    ctx.wait(value["job"], "source_recovery_memory")
                    ctx.timer(self.config.retry_ns, "load_retry")
                    return
                self.job = value
                self._start(ctx)
        elif kind == "retry":
            if not self.finished and self.job is not None and not self.loading:
                for seq in tuple(self.active):
                    self._send(ctx, seq)
                ctx.timer(self.config.retry_ns, "retry")
        elif kind == "permit":
            if not self.finished and data["seq"] in self.active:
                ctx.send(self.relay, "permit", data, size=96)
        elif kind == "ack" and self.job is not None and not self.finished:
            seq = data["seq"]
            if (seq not in self.active or data["sink"] not in self.sinks
                    or binding(data) != binding(self._envelope(seq))):
                return
            self.active[seq].add(data["sink"])
            if len(self.active[seq]) == len(self.sinks):
                del self.active[seq]
                ctx.note("flow_chunk_acked", op=token(data))
                self._fill(ctx)
                if not self.active and self.next_seq == len(self.job["chunks"]):
                    self.finished = True
                    ctx.clear_wait(self.job["job"])
                    ctx.note("flow_source_finished", op=self.job["job"])
                    ctx.release(self.lease)
                    self.lease = None
                    self.job = None

    def _save(self, ctx):
        if not ctx.persist("manifest", self.job, "manifest_saved",
                           size=sum(payload_size(rows) for rows in self.job["chunks"])):
            ctx.wait(self.job["job"], "source_persistence")
            ctx.timer(self.config.retry_ns, "save_retry")

    def _start(self, ctx):
        ctx.wait(self.job["job"], "durable_sink_receipts")
        self._fill(ctx)
        ctx.timer(self.config.retry_ns, "retry")

    def _envelope(self, seq):
        return {**{k: self.job[k] for k in ("job", "source", "snapshot", "code")},
                "seq": seq, "total": len(self.job["chunks"])}

    def _fill(self, ctx):
        while len(self.active) < self.config.window and self.next_seq < len(self.job["chunks"]):
            seq = self.next_seq
            self.next_seq += 1
            self.active[seq] = set()
            self._send(ctx, seq)

    def _send(self, ctx, seq):
        env = self._envelope(seq)
        rows = self.job["chunks"][seq]
        ctx.send(self.relay, "rows", dict(env, rows=rows), size=payload_size(rows))
        ctx.timer(self.config.permit_delay_ns, "permit", env)
        ctx.note("flow_attempt", op=token(env))


class Relay(Actor):
    def __init__(self, config, sinks):
        self.config, self.sinks = config, tuple(sinks)
        self.pending = {}
        self.serial = 0

    def on(self, ctx, kind, data):
        if kind in {"rows", "permit"}:
            key = token(data)
            item = self.pending.get(key)
            if item is None:
                if len(self.pending) >= self.config.relay_slots:
                    ctx.note("flow_relay_refused", op=key, reason="window")
                    return
                lease = ctx.reserve(96, "relay_envelope")
                if lease is None:
                    ctx.note("flow_relay_refused", op=key, reason="memory")
                    return
                self.serial += 1
                item = dict(env={k: v for k, v in data.items() if k != "rows"},
                            rows=None, permit=False, leases=[lease], computing=False,
                            serial=self.serial, mask_reserved=False)
                self.pending[key] = item
                ctx.timer(self.config.retry_ns * 2, "expire", dict(key=key, serial=self.serial, mask_reserved=False))
            if binding(item["env"]) != binding(data):
                ctx.note("flow_rejected", op=key, reason="binding_mismatch")
                return
            if kind == "rows" and item["rows"] is None:
                lease = ctx.reserve(8 * len(data["rows"]), "relay_rows")
                if lease is None:
                    return  # Source retains the obligation and will retry.
                item["leases"].append(lease)
                item["rows"] = data["rows"]
            elif kind == "permit":
                item["permit"] = True
            self._compute(ctx, key)
        elif kind == "compute_retry":
            self._compute(ctx, data["key"])
        elif kind == "enhanced":
            key = data["key"]
            item = self.pending.get(key)
            if item is None:
                return
            rows = item["rows"]
            message = dict(item["env"], rows=rows)
            if self.config.enhance:
                message["mask"] = [selected(value) for value in rows]
            size = message_size(message)
            ctx.note("flow_relay_ready", op=key, enhanced=self.config.enhance, size=size)
            for sink in self.sinks:
                ctx.send(sink, "chunk", message, size=size)
            self._retire(ctx, key)
        elif kind == "expire":
            item = self.pending.get(data["key"])
            if item and item["serial"] == data["serial"] and not item["computing"]:
                ctx.note("flow_relay_expired", op=data["key"])
                self._retire(ctx, data["key"])

    def _compute(self, ctx, key):
        item = self.pending.get(key)
        if item is None or item["computing"]:
            return
        if item["rows"] is None or not item["permit"]:
            ctx.wait(key, "relay_join_inputs", rows=item["rows"] is not None,
                     permit=item["permit"])
            return
        if self.config.enhance and not item["mask_reserved"]:
            lease = ctx.reserve(math.ceil(len(item["rows"]) / 8), "relay_enhancement")
            if lease is None:
                ctx.wait(key, "relay_enhancement_memory")
                ctx.timer(self.config.retry_ns, "compute_retry", dict(key=key))
                return
            item["leases"].append(lease)
            item["mask_reserved"] = True
        duration = 40 + (len(item["rows"]) * self.config.filter_ns_per_row
                         if self.config.enhance else 0)
        if ctx.compute(duration, "enhanced", dict(key=key), leases=item["leases"]):
            item["computing"] = True
            ctx.wait(key, "relay_compute")
        else:
            ctx.timer(self.config.retry_ns, "compute_retry", dict(key=key))

    def _retire(self, ctx, key):
        item = self.pending.pop(key)
        for lease in item["leases"]:
            ctx.release(lease)
        ctx.clear_wait(key)


class Sink(Actor):
    def __init__(self, config, sources):
        self.config, self.sources = config, tuple(sources)
        self.ready = False
        self.records = {}
        self.pending = {}
        self.receipt_leases = []
        self.materialization = None
        self.publishing = False

    def on(self, ctx, kind, data):
        if kind in {"boot", "recover_retry"}:
            if not ctx.scan("", "recovered"):
                ctx.timer(self.config.retry_ns, "recover_retry")
        elif kind == "recovered":
            if not data["ok"]:
                ctx.timer(self.config.retry_ns, "recover_retry")
                return
            recovered = data["records"]
            self.materialization = recovered.get("materialization")
            receipts = {k[7:]: v for k, v in recovered.items() if k.startswith("record/")}
            lease = ctx.reserve(128 if self.materialization else 96 * len(receipts), "sink_receipts")
            if lease is None:
                ctx.wait("job-1", "sink_recovery_memory")
                ctx.timer(self.config.retry_ns, "recover_retry")
                return
            self.receipt_leases.append(lease)
            self.records = {} if self.materialization else receipts
            self.ready = True
            ctx.note("flow_recovered", op="job-1", records=len(receipts),
                     materialized=self.materialization is not None)
            if self.materialization:
                ctx.note("flow_materialized", op="job-1", result=self.materialization, recovered=True)
            else:
                self._materialize(ctx)
        elif kind == "chunk":
            self._chunk(ctx, data)
        elif kind == "computed":
            key = data["key"]
            item = self.pending[key]
            message = item["message"]
            rows = message["rows"]
            mask = message.get("mask")
            values = [v for i, v in enumerate(rows) if (mask[i] if mask is not None else selected(v))]
            item["record"] = {**{k: v for k, v in message.items() if k not in {"rows", "mask"}},
                              "sum": sum(values), "count": len(values)}
            self._persist(ctx, key)
        elif kind == "persist_retry":
            if data["key"] in self.pending:
                self._persist(ctx, data["key"])
        elif kind == "saved":
            key = data["contribution"]
            item = self.pending[key]
            if not data["ok"]:
                ctx.timer(self.config.retry_ns, "persist_retry", dict(key=key))
                return
            record = item["record"]
            self.records[key] = record
            self.receipt_leases.append(item["receipt"])
            ctx.release(item["payload"])
            del self.pending[key]
            ctx.clear_wait(key)
            ctx.note("flow_receipt", op=key, record=record)
            self._ack(ctx, record)
            self._materialize(ctx)
        elif kind == "materialization_retry":
            self.publishing = False
            self._materialize(ctx)
        elif kind == "materialized":
            if not data["ok"]:
                ctx.timer(self.config.retry_ns, "materialization_retry")
                return
            self.materialization = data["result"]
            for lease in self.receipt_leases:
                ctx.release(lease)
            self.receipt_leases = [data["lease"]]
            self.records.clear()
            ctx.clear_wait("job-1")
            ctx.note("flow_materialized", op="job-1", result=self.materialization, recovered=False)

    def _chunk(self, ctx, message):
        key = token(message)
        if not self.ready:
            return
        if message["source"] not in self.sources or message["code"] != "filter-1":
            ctx.note("flow_rejected", op=key, reason="unknown_binding")
            return
        if self.materialization:
            pub = self.materialization
            if (message["job"], message["snapshot"], message["code"]) == tuple(pub[k] for k in ("job", "snapshot", "code")) and 0 <= message["seq"] < pub["coverage"][message["source"]] and message["total"] == pub["coverage"][message["source"]]:
                self._ack(ctx, message)
            return
        if key in self.records:
            if binding(self.records[key]) == binding(message):
                ctx.note("flow_duplicate", op=key)
                self._ack(ctx, message)
            return
        if key in self.pending:
            return
        if len(self.pending) >= self.config.sink_slots:
            ctx.note("flow_sink_refused", op=key, reason="window")
            return
        receipt = ctx.reserve(96, "sink_receipts")
        payload = ctx.reserve(message_size(message), "sink_working")
        if receipt is None or payload is None:
            for lease in (receipt, payload):
                if lease is not None:
                    ctx.release(lease)
            return
        duration = len(message["rows"]) * self.config.consume_ns_per_row
        if "mask" not in message:
            duration += len(message["rows"]) * self.config.filter_ns_per_row
        if not ctx.compute(duration, "computed", dict(key=key), leases=(payload,)):
            ctx.release(payload)
            ctx.release(receipt)
            return
        self.pending[key] = dict(message=message, payload=payload, receipt=receipt)
        ctx.wait(key, "sink_compute_then_durability")

    def _persist(self, ctx, key):
        item = self.pending[key]
        if not ctx.persist("record/" + key, item["record"], "saved",
                           dict(contribution=key), size=96):
            ctx.wait(key, "sink_durability_pressure")
            ctx.timer(self.config.retry_ns, "persist_retry", dict(key=key))

    def _ack(self, ctx, record):
        env = {k: record[k] for k in ("job", "source", "seq", "total", "snapshot", "code")}
        ctx.send(record["source"], "ack", dict(env, sink=ctx.actor), size=96)

    def _materialize(self, ctx):
        if self.publishing or self.materialization or not self.records:
            return
        records = list(self.records.values())
        identities = {(r["job"], r["snapshot"], r["code"]) for r in records}
        if len(identities) != 1:
            ctx.wait("job-1", "incompatible_input_binding")
            return
        coverage = {}
        for source in self.sources:
            part = [r for r in records if r["source"] == source]
            totals = {r["total"] for r in part}
            if len(totals) != 1:
                return
            total = next(iter(totals))
            if total <= 0 or {r["seq"] for r in part} != set(range(total)):
                ctx.wait("job-1", "incomplete_partition_coverage")
                return
            coverage[source] = total
        job, snapshot, code = next(iter(identities))
        result = dict(job=job, snapshot=snapshot, code=code, coverage=coverage,
                      sum=sum(r["sum"] for r in records), count=sum(r["count"] for r in records))
        lease = ctx.reserve(128, "materialized_result")
        if lease is not None and ctx.persist("materialization", result, "materialized",
                                            dict(result=result, lease=lease), size=128):
            self.publishing = True
        else:
            if lease is not None:
                ctx.release(lease)
            ctx.wait(job, "materialization_capacity")
            ctx.timer(self.config.retry_ns, "materialization_retry")


class Foreground(Actor):
    """Independent open-loop small operations on a consumer's worker pool."""
    def __init__(self, config):
        self.config = config

    def on(self, ctx, kind, data):
        if kind == "start":
            for i in range(self.config.foreground_count):
                ctx.timer(i * self.config.foreground_period_ns, "offer", dict(seq=i))
        elif kind == "offer":
            op = f'foreground/{data["seq"]}'
            ctx.note("foreground_offered", op=op)
            if not ctx.compute(self.config.foreground_service_ns, "done",
                               dict(op=op, started=ctx.now)):
                ctx.note("foreground_refused", op=op)
        elif kind == "done":
            ctx.note("foreground_done", op=data["op"], latency=ctx.now - data["started"])


def build(config=FlowConfig(), replay=None, offer=True, sink_factory=Sink):
    """Programmatic scenario; only the harness may inspect or fault the world."""
    config.validate()
    world = World(seed=config.seed, ordering=config.ordering, replay=replay)
    for name in ("p0", "p1", "d0", "d1", "c0", "c1"):
        world.add_host(Host(name, domain="origin" if name[0] == "p" else "destination",
                            workers=1, control=1, handler_ns=20,
                            memory_bytes=config.memory_bytes, durable_bytes=config.durable_bytes,
                            nic_bytes_per_ns=4, disk_bytes_per_ns=4, disk_latency=400,
                            queue_limit=128))
    for source, machine in world.hosts.items():
        for target, other in world.hosts.items():
            if source != target:
                wan = machine.config.domain != other.config.domain
                world.add_link(Link(source, target, latency=4_000 if wan else 100,
                                    bandwidth=config.wan_bytes_per_ns if wan else 4,
                                    duplicate=config.duplicate))
    sources, sinks = ("source0", "source1"), ("sink0", "sink1")
    prefix = {"source": "p", "destination": "d", "consumer": "c"}[config.placement]
    for i in range(2):
        world.add_actor(sources[i], f"p{i}", lambda i=i: Source(config, f"relay{i}", sinks))
        world.add_actor(f"relay{i}", f"{prefix}{i}", lambda: Relay(config, sinks))
        world.add_actor(sinks[i], f"c{i}", lambda: sink_factory(config, sources))
        if offer:
            world.inject(sources[i], "offer", manifest(config, sources[i]), at=2_000)
    world.add_actor("foreground", "c0", lambda: Foreground(config))
    world.inject("foreground", "start", at=2_000)
    return world


def oracle(config):
    """Whole-input oracle, independent of actor reductions and enhancement masks."""
    rows = [value for source in ("source0", "source1")
            for chunk in manifest(config, source)["chunks"] for value in chunk]
    values = [value for value in rows if value % 3 == 0]
    return dict(job="job-1", snapshot="snapshot-7", code="filter-1",
                coverage={"source0": config.chunks, "source1": config.chunks},
                sum=sum(values), count=len(values))


def summarize(world, config):
    materializations = [row for row in world.trace if row["kind"] == "flow_materialized"]
    expected = oracle(config)
    foreground = [row["latency"] for row in world.trace if row["kind"] == "foreground_done"]
    wire_bytes = wan_bytes = cost_units = attempted_wire_bytes = 0
    for row in world.trace:
        if row["kind"] == "packet_departed":
            source = world.actors[row["actor"]].host
            target = world.actors[row["target"]].host
            if source != target:
                attempted_wire_bytes += row["size"]
        if row["kind"] != "wire_transmitted":
            continue
        wire_bytes += row["size"]
        origin = world.hosts[row["source"]].config.domain
        destination = world.hosts[row["target"]].config.domain
        if origin != destination:
            wan_bytes += row["size"]
            cost_units += row["size"] * (3 if origin == "origin" else 1)
    report = world.report()
    return dict(materializations=len({r["actor"] for r in materializations}),
                correct=all(r["result"] == expected for r in materializations),
                materialized_at={name: min(r["time"] for r in materializations if r["actor"] == name)
                              for name in {r["actor"] for r in materializations}},
                finished_sources=len({r["actor"] for r in world.trace if r["kind"] == "flow_source_finished"}),
                wire_bytes=wire_bytes, wan_bytes=wan_bytes, modeled_cost_units=cost_units,
                attempted_wire_bytes=attempted_wire_bytes,
                worker_ns=sum(v["busy_ns"] for k, v in report["resources"].items() if k.endswith("/workers")),
                foreground=dict(offered=sum(r["kind"] == "foreground_offered" for r in world.trace),
                                completed=len(foreground),
                                refused=sum(r["kind"] == "foreground_refused" for r in world.trace),
                                max_ns=max(foreground, default=None),
                                mean_ns=sum(foreground) / len(foreground) if foreground else None),
                hosts=report["hosts"], waits=report["waits"],
                trace_hash=report["trace_hash"], counts=report["counts"])


def comparisons():
    result = {}
    base = FlowConfig()
    for placement in ("source", "destination", "consumer"):
        for enhance in (False, True):
            config = replace(base, placement=placement, enhance=enhance)
            world = build(config)
            world.run(until=1_000_000)
            result[f'{placement}/{"enhanced" if enhance else "raw"}'] = summarize(world, config)
    return result


if __name__ == "__main__":
    import json
    print(json.dumps(comparisons(), indent=2, sort_keys=True))
