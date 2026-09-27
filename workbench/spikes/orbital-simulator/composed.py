"""One path from distributed work through durable admission to versioned reads.

Materialization is private application state. Only the ordinary witness/consumer
path publishes the submitted result. There is no observer-driven bridge.
"""
from __future__ import annotations

from dataflow_scenario import FlowConfig, Sink, build as build_flow, oracle
from protocol import Config, Endpoint, RETRY, audit as audit_protocol, build_scenario, encoded
from sim import Link


class ResultSink(Sink):
    def __init__(self, config, sources):
        super().__init__(config, sources)
        self.acknowledged = False

    def on(self, ctx, kind, data):
        if kind == "boot":
            ctx.timer(RETRY, "result_retry")
        elif kind == "result_received":
            if self.materialization == data.get("result"):
                self.acknowledged = True
            return
        elif kind == "result_retry":
            if not self.acknowledged:
                if self.materialization:
                    ctx.send("application", "artifact", dict(source=ctx.actor, result=self.materialization),
                             size=len(encoded(self.materialization)) + 64)
                ctx.timer(RETRY, "result_retry")
            return
        super().on(ctx, kind, data)


class Application(Endpoint):
    """Recoverable outbox and required-result join; every fact arrives by a port."""
    def __init__(self, config):
        super().__init__(config)
        self.outbox = {}
        self.saving = set()
        self.reports = {}
        self.completed = 0
        self.recording = set()
        self.reads = {}
        self.failed = False

    def restore(self, ctx, records):
        self.outbox = {int(k.split("/")[1]): v for k, v in records.items() if k.startswith("outbox/")}
        complete = [int(k.split("/")[1]) for k in records if k.startswith("complete/")]
        self.completed = max(complete, default=0)
        self.failed = "rejected" in records
        self.ensure(ctx, 1, dict(kind="set", key="unrelated", value=7))
        ctx.note("job_client_recovered", outbox=sorted(self.outbox), completed=self.completed)

    def ensure(self, ctx, lsn, command):
        if lsn not in self.outbox and lsn not in self.saving:
            if ctx.persist(f"outbox/{lsn}", command, "outbox_saved", dict(lsn=lsn, command=command)):
                self.saving.add(lsn)

    def tick(self, ctx):
        self.ensure(ctx, 1, dict(kind="set", key="unrelated", value=7))
        self.join(ctx)
        for lsn, command in sorted(self.outbox.items()):
            if lsn > self.completed:
                self.send(ctx, "producer", "submit", dict(lsn=lsn, command=command))
        self.send(ctx, "consumer", "status", {})
        if self.completed >= 2:
            result = self.outbox[2]["artifact"]
            for sink in ("sink0", "sink1"):
                ctx.send(sink, "result_received", dict(result=result), size=len(encoded(result)) + 64)
            for lsn in (1, 2):
                if lsn not in self.reads:
                    self.send(ctx, "consumer", "read_version", dict(lsn=lsn))

    def join(self, ctx):
        if self.failed or 2 in self.outbox or 2 in self.saving:
            return
        if set(self.reports) != {"sink0", "sink1"}:
            ctx.wait("aggregate", "required application artifacts", received=sorted(self.reports))
            return
        left, right = self.reports["sink0"], self.reports["sink1"]
        if left != right:
            if ctx.persist("rejected", self.reports, "rejected"):
                self.failed = True
            return
        self.ensure(ctx, 2, dict(kind="set", key="aggregate", value=left["sum"], artifact=left))

    def local(self, ctx, kind, data):
        if kind == "artifact":
            if data["source"] in ("sink0", "sink1"):
                previous = self.reports.get(data["source"])
                if previous is not None and previous != data["result"]:
                    raise ValueError("stable artifact identity changed")
                self.reports[data["source"]] = data["result"]
                ctx.note("artifact_received", op="aggregate", source=data["source"], result=data["result"])
                self.join(ctx)
        elif kind == "outbox_saved":
            self.saving.discard(data["lsn"])
            if data["ok"]:
                self.outbox[data["lsn"]] = data["command"]
                ctx.note("result_proposed", op="aggregate" if data["lsn"] == 2 else "point",
                         lsn=data["lsn"])
                self.tick(ctx)
        elif kind == "completed_recorded":
            self.recording.discard(data["lsn"])
            if data["ok"]:
                self.completed = max(self.completed, data["lsn"])
                if self.completed >= 2:
                    ctx.clear_wait("aggregate")
                self.tick(ctx)
        elif kind == "rejected" and data["ok"]:
            ctx.clear_wait("aggregate")
            ctx.note("job_rejected", op="aggregate")

    def message(self, ctx, source, kind, data):
        if source != "consumer":
            return
        if kind == "result" and data["lsn"] > self.completed:
            lsn = data["lsn"]
            if lsn in self.outbox and lsn not in self.recording:
                if ctx.persist(f"complete/{lsn}", data, "completed_recorded", data):
                    self.recording.add(lsn)
        elif kind == "version":
            self.reads[data["lsn"]] = data["values"]
            ctx.note("job_version_read", op="aggregate", **data)


def build(seed=1, ordering="shuffle", replay=None, hold_artifact_until=None):
    config = FlowConfig(seed=seed, ordering=ordering, placement="consumer", chunks=3,
                        rows=24, duplicate=.2)
    world = build_flow(config, replay=replay, sink_factory=ResultSink)
    build_scenario(world=world, offer=False, submitters=("application",),
                   placement={"consumer": "c0", "ordinary": "c0"})
    world.add_actor("application", "reader", lambda: Application(Config(submitters=("application",))))
    # Physical topology is harness-owned; actors get endpoints, not this graph.
    for source, machine in world.hosts.items():
        for target, other in world.hosts.items():
            if source != target and (source, target) not in world.links:
                world.add_link(Link(source, target,
                    latency=8_000 if machine.config.domain != other.config.domain else 100))
    if hold_artifact_until is not None:
        world.partition("c1", "reader")
        world.fault(hold_artifact_until, "partition", source_host="c1", target_host="reader", blocked=False)
    return world, config


def audit(world, config):
    checked = audit_protocol(world)
    expected = oracle(config)
    errors = list(checked["violations"])
    materialized = {}
    submitted = {}
    published = {}
    read_versions = {}
    for row in world.trace:
        if row["kind"] == "flow_materialized":
            materialized[row["actor"]] = row["result"]
        elif row["kind"] == "durable_write" and row["actor"] == "producer" and row["key"] == "payload/2":
            submitted[2] = row["value"]
            if set(materialized) != {"sink0", "sink1"} or any(r != expected for r in materialized.values()):
                errors.append("aggregate proposed without the complete correct artifacts")
            if row["value"]["command"].get("artifact") != expected:
                errors.append("proposal lost artifact version/coverage/interpretation")
        elif row["kind"] == "published":
            published[row["lsn"]] = row["time"]
        elif row["kind"] == "job_version_read":
            expected_values = {"unrelated": 7}
            if row["lsn"] == 2:
                expected_values["aggregate"] = expected["sum"]
            if row["values"] != expected_values:
                errors.append("versioned read returned the wrong logical state")
            if row["lsn"] not in published:
                errors.append("version served before journal-backed publication")
            read_versions[row["lsn"]] = row["values"]
    if 2 in published and 2 not in submitted:
        errors.append("published aggregate without submitted body")
    return dict(ok=not errors, violations=sorted(set(errors)),
                complete=set(read_versions) == {1, 2}, versions=read_versions,
                publication_ns=published, materialized=len(materialized),
                foreground_completed=sum(r["kind"] == "foreground_done" for r in world.trace))
