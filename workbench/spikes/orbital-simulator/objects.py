"""Small object/extension programs over the shared simulation ports.

Positions and initial accepted values are fixture inputs. This binding is not a
second transaction protocol, a UFFD implementation or a calibrated cost model.
It reconstructs actual values from persisted records and exposes publication as
a continuation that can later be connected to the admission implementation.
"""

from collections import OrderedDict, deque
import json

from sim import Actor, Host, World


PAGE_BYTES = 4096
META_BYTES = 65536  # Explicit space for ports/control, separate from the view cap.


class PortActor(Actor):
    """Retry local enqueue refusal; no invented acknowledgement of delivery."""

    def send(self, ctx, target, kind, data):
        if not ctx.send(target, kind, data):
            ctx.timer(1000, "retry_send", {"target": target, "kind": kind, "data": data})

    def common(self, ctx, kind, data):
        if kind == "retry_send":
            self.send(ctx, data["target"], data["kind"], data["data"])
            return True
        return False


class ObjectStore(PortActor):
    """One owner, versioned integer pages, and a bounded resident projection cache.

    Reconstruction reads the durable record through Context.load. Only the
    publication callback adds a write to the owner-visible committed history.
    The fixture serializes writes and one page preparation, but asynchronous
    users can retain evicted frames through the shared resource machine.
    """

    def __init__(self, policy="demand", resident_pages=2, window=2,
                 unsafe_latest=False):
        if policy not in ("full", "window", "demand"):
            raise ValueError(policy)
        self.policy = policy
        self.limit = resident_pages
        self.window = window
        self.unsafe_latest = unsafe_latest
        self.history = None
        self.cache = OrderedDict()
        self.reads = deque()
        self.writes = deque()
        self.active = None
        self.writing = None
        self.floor = 0

    def on(self, ctx, kind, data):
        if self.common(ctx, kind, data):
            return
        if kind == "boot":
            assert ctx.load("history", "reopened")
        elif kind == "reopened":
            if data["ok"] and data["value"] is not None:
                self.history = data["value"]
                ctx.note("object_reopened", pages=len(self.history["base"]))
                self.pump(ctx)
        elif kind == "initialize":
            record = {"base": list(data["values"]), "writes": []}
            assert ctx.persist("history", record, "initialized", {"record": record})
        elif kind == "initialized":
            assert data["ok"]
            self.history = data["record"]
            ctx.note("object_initialized", values=list(self.history["base"]))
            self.pump(ctx)
        elif kind == "context":
            self.floor = max(self.floor, data["cut"])
            ctx.note("context_registered", op=data["tx"], cut=data["cut"],
                     coverage="whole-fixture-object")
            self.send(ctx, data["reply"], "context_ready", {"cut": data["cut"]})
        elif kind == "read":
            self.reads.append(dict(data))
            self.pump(ctx)
        elif kind == "write":
            self.writes.append(dict(data))
            self.pump(ctx)
        elif kind == "written":
            assert data["ok"]
            request = self.writing
            self.history = data["record"]
            self.writing = None
            # An existing cut may have been cached before this supplied-position
            # write. The fixture never bypasses a known relevant pending write.
            for key in list(self.cache):
                if key[0] >= request["position"]:
                    _, lease = self.cache.pop(key)
                    ctx.release(lease)
            ctx.note("object_write", op=request["tx"], position=request["position"],
                     page=request["page"], value=request["value"])
            self.send(ctx, request["reply"], "write_result", request)
            self.pump(ctx)
        elif kind == "history_loaded":
            assert self.active is not None
            if not data["ok"] or data["value"] is None:
                self.fail_read(ctx, "history_unavailable")
                return
            self.active["record"] = data["value"]
            work = 1000 * (len(self.active["missing"]) + len(data["value"]["writes"]) + 1)
            leases = tuple(self.active["leases"].values())
            assert ctx.compute(work, "reconstructed", leases=leases)
        elif kind == "reconstructed":
            pending = self.active
            record = pending["record"]
            values = list(record["base"])
            cut = pending["request"]["cut"]
            for write in sorted(record["writes"], key=lambda item: item["position"]):
                if self.unsafe_latest or write["position"] <= cut:
                    values[write["page"]] = write["value"]
            for page in pending["missing"]:
                key = (cut, page)
                self.cache[key] = (values[page], pending["leases"][page])
                ctx.note("page_materialized", op=pending["request"]["id"],
                         cut=cut, page=page, value=values[page])
            self.finish_read(ctx)
        elif kind == "close":
            assert self.active is None and not self.reads
            for _, lease in self.cache.values():
                ctx.release(lease)
            self.cache.clear()
            ctx.note("views_closed")
        else:
            raise ValueError(kind)

    def pump(self, ctx):
        if self.history is None:
            return
        if self.writing is None and self.writes:
            request = self.writes.popleft()
            if request.get("fresh"):
                previous = [w["position"] for w in self.history["writes"]]
                request["position"] = max([self.floor, *previous]) + 1
            self.writing = request
            record = {"base": self.history["base"],
                      "writes": self.history["writes"] + [{
                          "position": request["position"], "page": request["page"],
                          "value": request["value"], "tx": request["tx"]}]}
            assert ctx.persist("history", record, "written", {"record": record})
        if self.active is not None or not self.reads:
            return
        request = self.reads[0]
        if self.writing and self.writing["position"] <= request["cut"]:
            ctx.wait(request["id"], "earlier_write", transaction=self.writing["tx"])
            return
        self.reads.popleft()
        ctx.clear_wait(request["id"])
        cut, page = request["cut"], request["page"]
        key = (cut, page)
        if key in self.cache:
            self.cache.move_to_end(key)
            value, _ = self.cache[key]
            self.reply_read(ctx, request, value)
            self.pump(ctx)
            return
        count = len(self.history["base"])
        if self.policy == "full":
            batch = list(range(count))
        elif self.policy == "window":
            start = page // self.window * self.window
            batch = list(range(start, min(start + self.window, count)))
        else:
            batch = [page]
        if len(batch) > self.limit:
            self.send(ctx, request["reply"], "read_result", {
                **request, "status": "refused", "reason": "preparation_exceeds_view_budget"})
            ctx.note("read_refused", op=request["id"], policy=self.policy)
            self.pump(ctx)
            return
        desired = {(cut, p) for p in batch}
        missing = [p for p in batch if (cut, p) not in self.cache]
        while len(self.cache) + len(missing) > self.limit:
            victim = next(k for k in self.cache if k not in desired)
            _, lease = self.cache.pop(victim)
            ctx.release(lease)
        leases = {}
        for p in missing:
            lease = ctx.reserve(PAGE_BYTES, f"view/{cut}/{p}")
            if lease is None:
                for old in leases.values():
                    ctx.release(old)
                self.send(ctx, request["reply"], "read_result", {
                    **request, "status": "refused", "reason": "shared_memory_pressure"})
                ctx.note("read_refused", op=request["id"], policy=self.policy)
                self.pump(ctx)
                return
            leases[p] = lease
        self.active = {"request": request, "missing": missing, "leases": leases}
        ctx.note("page_batch", op=request["id"], pages=list(missing), policy=self.policy)
        assert ctx.load("history", "history_loaded")

    def fail_read(self, ctx, reason):
        pending, self.active = self.active, None
        for lease in pending["leases"].values():
            ctx.release(lease)
        self.send(ctx, pending["request"]["reply"], "read_result", {
            **pending["request"], "status": "failed", "reason": reason})
        self.pump(ctx)

    def finish_read(self, ctx):
        request = self.active["request"]
        value, _ = self.cache[(request["cut"], request["page"])]
        self.active = None
        self.reply_read(ctx, request, value)
        self.pump(ctx)

    def reply_read(self, ctx, request, value):
        ctx.note("view_observed", op=request["id"], cut=request["cut"],
                 page=request["page"], value=value)
        self.send(ctx, request["reply"], "read_result", {
            **request, "status": "ok", "value": value})


class ReadProgram(PortActor):
    def __init__(self):
        self.steps = deque()
        self.results = []

    def on(self, ctx, kind, data):
        if self.common(ctx, kind, data) or kind == "boot":
            return
        if kind == "start":
            self.steps = deque(data["steps"])
            self.results = []
            self.name = data["name"]
            ctx.note("offered", op=self.name)
            self.advance(ctx)
        elif kind == "read_result":
            self.results.append(dict(data))
            self.advance(ctx)
        elif kind == "write_result":
            self.advance(ctx)
        else:
            raise ValueError(kind)

    def advance(self, ctx):
        if not self.steps:
            statuses = {r["status"] for r in self.results}
            status = "complete" if statuses <= {"ok"} else "refused"
            ctx.note(status, op=self.name, results=self.results)
            self.send(ctx, "store", "close", {})
            return
        step = self.steps.popleft()
        self.send(ctx, "store", step["kind"], {**step, "reply": ctx.actor})


class LeaseProgram(Actor):
    """Actor cancellation cannot retire a compute/backend borrow."""

    def __init__(self, capacity):
        self.capacity = capacity

    def on(self, ctx, kind, data):
        if kind == "boot":
            return
        if kind == "start":
            ctx.note("offered", op="lease")
            lease = ctx.reserve(self.capacity, "sealed-output")
            assert lease is not None
            assert ctx.compute(5000, "backend_done", leases=(lease,))
            ctx.release(lease)
            ctx.note("cancel_requested", op="lease")
            self.probe(ctx, "after_owner_release")
        elif kind == "backend_done":
            self.probe(ctx, "inside_completion")
            ctx.timer(1, "retired")
        elif kind == "retired":
            self.probe(ctx, "after_completion")
            ctx.note("complete", op="lease")
        else:
            raise ValueError(kind)

    def probe(self, ctx, phase):
        lease = ctx.reserve(self.capacity, "replacement")
        ctx.note("lease_probe", op="lease", phase=phase, acquired=lease is not None)
        if lease is not None:
            ctx.release(lease)


class MemoryPressure(Actor):
    """An independent backend borrower consumes the same machine's memory."""

    def on(self, ctx, kind, data):
        if kind == "boot":
            return
        if kind == "start":
            lease = ctx.reserve(data["bytes"], "other-work-output")
            assert lease is not None
            assert ctx.compute(500_000, "retired", leases=(lease,))
            ctx.release(lease)
            ctx.note("pressure_started", bytes=data["bytes"])
        elif kind == "retired":
            ctx.note("pressure_finished")
        else:
            raise ValueError(kind)


class Checker(PortActor):
    def on(self, ctx, kind, data):
        if self.common(ctx, kind, data) or kind == "boot":
            return
        if kind == "execute":
            self.plan = dict(data)
            assert ctx.compute(data["work"], "query")
        elif kind == "query":
            self.send(ctx, "store", "read", {"id": f"T/{ctx.actor}/query",
                "cut": self.plan["cut"], "page": self.plan["page"], "reply": ctx.actor})
        elif kind == "read_result":
            assert data["status"] == "ok"
            transcript = {"invocation": "T", "code": "fixture-native-v1",
                          "cut": self.plan["cut"], "request": {"page": self.plan["page"]},
                          "response": {"value": data["value"]},
                          "effects": [{"page": 3, "value": data["value"] + 1}],
                          "result": "OK", "completion": "return"}
            # Canonical bytes stand in for the BLAKE3 equality gate, not a VM.
            encoded = json.dumps(transcript, sort_keys=True, separators=(",", ":"))
            ctx.note("extension_transcript", op="T", checker=ctx.actor,
                     transcript=transcript, encoded=encoded)
            assert ctx.persist("report/T", {"transcript": transcript, "encoded": encoded},
                               "report_durable", {"transcript": transcript, "encoded": encoded})
        elif kind == "report_durable":
            assert data["ok"]
            self.send(ctx, "coordinator", "report", {"checker": ctx.actor,
                       "transcript": data["transcript"], "encoded": data["encoded"]})
        else:
            raise ValueError(kind)


class VerificationCoordinator(PortActor):
    def __init__(self, unsafe_final_only=False):
        self.unsafe_final_only = unsafe_final_only
        self.reports = {}

    def on(self, ctx, kind, data):
        if self.common(ctx, kind, data) or kind == "boot":
            return
        if kind == "start":
            self.plan = data
            self.required = tuple(data["checkers"])
            ctx.note("offered", op="T")
            ctx.note("verification_plan", op="T", required=list(self.required), cut=10)
            self.send(ctx, "store", "context", {"tx": "T", "cut": 10, "reply": ctx.actor})
        elif kind == "context_ready":
            for checker, specification in self.plan["checkers"].items():
                self.send(ctx, checker, "execute", {**specification, "cut": data["cut"]})
            self.send(ctx, "independent", "start", {})
            ctx.wait("T", "required_extension_checks", required=list(self.required))
        elif kind == "report":
            assert data["checker"] in self.required
            self.reports[data["checker"]] = data
            if set(self.reports) != set(self.required):
                return
            records = [self.reports[name] for name in self.required]
            compared = ([r["transcript"]["result"] for r in records]
                        if self.unsafe_final_only else [r["encoded"] for r in records])
            decision = "commit" if len(set(compared)) == 1 else "abort"
            value = {"decision": decision, "reports": records}
            assert ctx.persist("decision/T", value, "decision_durable", value)
        elif kind == "decision_durable":
            assert data["ok"]
            ctx.clear_wait("T")
            ctx.note("verification_decision", op="T", decision=data["decision"],
                     checked=list(self.required))
            if data["decision"] == "abort":
                ctx.note("aborted", op="T", reason="transcript_mismatch")
                return
            effect = data["reports"][0]["transcript"]["effects"][0]
            self.send(ctx, "store", "write", {"tx": "T", "position": 10,
                       **effect, "reply": ctx.actor})
        elif kind == "write_result":
            ctx.note("published", op="T", value=data["value"], position=data["position"])
            ctx.note("complete", op="T")
        else:
            raise ValueError(kind)


class IndependentWriter(PortActor):
    def on(self, ctx, kind, data):
        if self.common(ctx, kind, data) or kind == "boot":
            return
        if kind == "start":
            ctx.note("offered", op="U")
            self.send(ctx, "store", "write", {"tx": "U", "position": 0,
                       "fresh": True, "page": 2, "value": 7, "reply": ctx.actor})
        elif kind == "write_result":
            ctx.note("published", op="U", value=data["value"], position=data["position"])
            ctx.note("complete", op="U")
        else:
            raise ValueError(kind)


def object_world(policy="demand", resident_pages=2, unsafe_latest=False,
                 seed=1, ordering="fifo", scan=False, late_open=False,
                 pressure_bytes=0):
    world = World(seed=seed, ordering=ordering)
    world.add_host(Host("h", workers=2, memory_bytes=META_BYTES + resident_pages * PAGE_BYTES))
    world.add_actor("store", "h", lambda: ObjectStore(policy, resident_pages,
                                                     unsafe_latest=unsafe_latest))
    world.add_actor("reader", "h", ReadProgram)
    if pressure_bytes:
        world.add_actor("pressure", "h", MemoryPressure)
        world.inject("pressure", "start", {"bytes": pressure_bytes}, at=0)
    world.inject("store", "initialize", {"values": [10, 20, 30, 40]}, at=0)
    first_read = {"kind": "read", "id": "R/0", "cut": 10, "page": 0}
    steps = [] if late_open else [first_read]
    if not scan:
        steps.append({"kind": "write", "tx": "W", "position": 20,
                      "page": 1, "value": 99})
    if late_open:
        steps.append(first_read)
    steps.extend({"kind": "read", "id": f"R/{p}", "cut": 10, "page": p}
                 for p in (1, 2, 3))
    world.inject("reader", "start", {"name": "R", "steps": steps}, at=0)
    return world


def lease_world(seed=1, ordering="fifo"):
    world = World(seed=seed, ordering=ordering)
    capacity = 8192
    world.add_host(Host("h", memory_bytes=capacity))
    world.add_actor("lease", "h", lambda: LeaseProgram(capacity))
    world.inject("lease", "start", {}, at=0)
    return world


def extension_world(mismatch=True, unsafe_final_only=False, slow_work=1_000_000,
                    seed=1, ordering="fifo"):
    world = World(seed=seed, ordering=ordering)
    world.add_host(Host("h", workers=2, memory_bytes=META_BYTES + 2 * PAGE_BYTES))
    world.add_actor("store", "h", lambda: ObjectStore("demand", 2))
    world.add_actor("coordinator", "h", lambda: VerificationCoordinator(unsafe_final_only))
    world.add_actor("check-a", "h", Checker)
    world.add_actor("check-b", "h", Checker)
    world.add_actor("independent", "h", IndependentWriter)
    world.inject("store", "initialize", {"values": [0, 0, 0, 0]}, at=0)
    world.inject("coordinator", "start", {"checkers": {
        "check-a": {"page": 0, "work": 1000},
        "check-b": {"page": 1 if mismatch else 0, "work": slow_work}}}, at=0)
    return world


def run_objects(**kwargs):
    world = object_world(**kwargs)
    world.run(until=10_000_000)
    return world


def run_extensions(**kwargs):
    world = extension_world(**kwargs)
    world.run(until=10_000_000)
    return world
