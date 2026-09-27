"""An old-cut checked transform composed with the existing contention fold.

The agreed context command is a prototype realization of the brief's full read
bounds prerequisite. Checker queries are private, read-only observations, not
new journal commands. Integer values and fixture digests stand in for application
data and transcript equality; this is neither a BLAKE3 nor a native VM model.
Private values come from retained logical fold history, not physical object page
reconstruction. Projection leases have authored sizes; no UFFD/COW or reclamation
claim follows from these finite-capacity cases.
"""
from __future__ import annotations

from dataclasses import asdict, dataclass, replace
import json

from kernel import clone, digest
from local_release import LocalReleaseCoordinator, LocalReleaseShard
from replicated_contention import Consumer
from sim import Actor, Host, Link, encoded_size
from traffic import Config, Strategy, initial_state, group, effects, send


CHECKERS = ("checker_a", "checker_b")
SOURCE_VERSION = "integer-object-v1"
CODE_VERSION = "checked-sum-v1"
PROFILE = "fixture-canonical-transcript-v1"


def context_for(plan, position):
    context = dict(tx=plan["id"], position=position, keys=sorted(plan["reads"]),
                   effects=sorted(plan["writes"]), required=plan["required_checkers"],
                   source_version=plan["source_version"], code_version=plan["code_version"], profile=plan["profile"])
    return dict(context, invocation=digest(context))


class ContextShard(LocalReleaseShard):
    """The common deterministic fold, with explicit agreed read registration."""
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.contexts = {}

    def transition(self, request):
        if request["kind"] != "register-context":
            return super().transition(request)
        tx, rid, context = request["tx"], request["request"], request["context"]
        if self.tickets[tx].get("position") != context["position"]:
            raise ValueError("context does not name the transaction's fixed position")
        if tx in self.contexts and self.contexts[tx] != context:
            raise ValueError("context identity changed")
        self.contexts[tx] = clone(context)
        for key in context["keys"]:
            self.bounds[key] = max(self.bounds.get(key, 0), context["position"])
        result = dict(context_digest=digest(context))
        self.responses[rid] = result
        return result

    def logical_state(self):
        return dict(super().logical_state(), contexts=clone(self.contexts))


def private_read(ctx, history, data, ready):
    """No state mutation: missing agreed context/effects yield private pending."""
    context = history.contexts.get(data["tx"]) if ready else None
    if context is None:
        result = dict(status="pending", reason="context not folded")
    elif (data["invocation"] != context["invocation"] or data["key"] not in context["keys"]
          or data["position"] != context["position"]):
        result = dict(status="rejected", reason="outside registered context")
    else:
        before = digest(history.logical_state())
        observed = history.observe_at(data["tx"], data["position"], [data["key"]])
        if "pending" in observed:
            result = dict(status="pending", reason="earlier pending output", predecessors=observed["pending"])
        else:
            result = dict(status="ok", values=observed["values"])
        after = digest(history.logical_state())
        ctx.note("checked_private_observation", op=data["tx"], checker=data["reply"],
                 key=data["key"], position=data["position"], invocation=data["invocation"],
                 before=before, after=after, **result)
    send(ctx, data["reply"], "private-result", dict(data, **result))


class SourceShard(ContextShard):
    def reply(self, ctx, request, result):
        ctx.note("checked_source_output", op=request["tx"], shard=self.index, request=request, result=result)
        super().reply(ctx, request, result)

    def on(self, ctx, kind, data):
        if kind == "private-read":
            return private_read(ctx, self, data, self.ready)
        super().on(ctx, kind, data)


class SourceConsumer(Consumer):
    def __init__(self, shard, initial, strategy, replica, fold_ns):
        super().__init__(shard, initial, strategy, replica, fold_ns)
        self.history = ContextShard(shard, initial, strategy, metadata_ns_per_entry=0)

    def on(self, ctx, kind, data):
        if kind == "private-read":
            return private_read(ctx, self.history, data, self.ready)
        super().on(ctx, kind, data)


class CheckedCoordinator(LocalReleaseCoordinator):
    def setup(self, plan, lease):
        state = super().setup(plan, lease)
        state.update(context_ready=False, reports={}, report_leases={}, context_lease=None, verification_pending=False)
        return state

    def advance(self, ctx, state):
        if state["plan"].get("checked"):
            if state["phase"] == "read":
                context = context_for(state["plan"], state["position"])
                if state["context_lease"] is None:
                    state["context_lease"] = ctx.reserve(encoded_size(context) + 96, "checked-context")
                    if state["context_lease"] is None:
                        ctx.wait(state["plan"]["id"], "checked context capacity")
                        return
                state["context"] = context
                self.request(ctx, state, 0, "register-context", context=context)
                ctx.wait(state["plan"]["id"], "agreed checked context")
                return
            if state["phase"] == "verify":
                for checker in CHECKERS:
                    send(ctx, checker, "invoke", dict(context=state["context"], plan=state["plan"], reply=ctx.actor))
                ctx.wait(state["plan"]["id"], "required checked reports")
                return
            if state["phase"] == "mismatch":
                ctx.wait(state["plan"]["id"], "verification mismatch; agreed abort not modeled")
                return
            if state["phase"] == "checked-decide":
                if not state["pending"]:
                    outcome = dict(position=state["position"], values=state["verified_effects"],
                                   observed=state["values"], invocation=state["context"]["invocation"])
                    state["pending"] = ctx.persist(f"outcome/{state['plan']['id']}", outcome, "outcome-stored",
                                                  dict(tx=state["plan"]["id"], outcome=outcome))
                return
            if state["phase"] == "install" and len(state["installed"]) == len(state["locks"]):
                for lease in state["report_leases"].values():
                    ctx.release(lease)
                if state["context_lease"] is not None:
                    ctx.release(state["context_lease"])
        return super().advance(ctx, state)

    def on(self, ctx, kind, data):
        if kind == "recovered" and data.get("ok"):
            records = data["records"]
            for key, plan in records.items():
                if not key.startswith("input/") or not plan.get("checked"):
                    continue
                tx = plan["id"]
                if f"outcome/{tx}" in records:
                    evidence = records.get(f"verification/{tx}")
                    position = records.get(f"position/{tx}")
                    outcome = records[f"outcome/{tx}"]
                    if (not valid_verification(evidence, context_for(plan, position))
                            or not verified_outcome(evidence, outcome)):
                        ctx.note("checked_recovery_rejected", op=tx)
                        ctx.wait("recovery", "missing or invalid verification evidence")
                        return
        tx = data.get("tx")
        state = self.states.get(tx)
        if kind == "response" and data.get("kind") == "register-context" and state is not None:
            if state["phase"] != "read":
                return
            if data["context_digest"] != digest(state["context"]):
                raise ValueError("context acknowledgement disagrees")
            state["phase"] = "verify"
            ctx.note("checked_context_ready", op=tx, position=state["position"], context=state["context"])
            self.advance(ctx, state)
            self.arm(ctx, state)
            return
        if kind == "checked-report" and state is not None:
            if state["phase"] != "verify":
                return
            report, context = data["report"], state["context"]
            checker = data["checker"]
            if checker not in CHECKERS or not isinstance(report, dict) or report.get("context") != context:
                ctx.note("checked_report_rejected", op=tx, checker=checker)
                return
            if checker not in state["report_leases"]:
                lease = ctx.reserve(encoded_size(report) + 96, "checked-report-evidence")
                if lease is None:
                    ctx.wait(tx, "verification evidence capacity")
                    return
                state["report_leases"][checker] = lease
            state["reports"][checker] = report
            if set(state["reports"]) != set(CHECKERS) or state["verification_pending"]:
                return
            if len({digest(report) for report in state["reports"].values()}) != 1:
                state["phase"] = "mismatch"
                ctx.note("checked_mismatch", op=tx)
                self.advance(ctx, state)
                return
            evidence = dict(context=context, reports=state["reports"])
            if not valid_verification(evidence, context):
                state["phase"] = "mismatch"
                ctx.note("checked_verification_rejected", op=tx, reason="unsupported complete report")
                self.advance(ctx, state)
                return
            state["verification_pending"] = ctx.persist(f"verification/{tx}", evidence,
                "verification-stored", dict(tx=tx, evidence=evidence))
            if not state["verification_pending"]:
                self.arm(ctx, state)
            return
        if kind == "verification-stored" and state is not None:
            state["verification_pending"] = False
            if not data["ok"]:
                self.arm(ctx, state)
                return
            evidence = data["evidence"]
            if not valid_verification(evidence, state["context"]):
                raise ValueError("invalid stored verification")
            report = evidence["reports"][CHECKERS[0]]
            state["values"] = report["observed"]
            state["verified_effects"] = report["effects"]
            state["phase"] = "checked-decide"
            ctx.note("checked_verification_durable", op=tx, evidence=evidence)
            self.advance(ctx, state)
            self.arm(ctx, state)
            return
        return super().on(ctx, kind, data)


def valid_verification(evidence, context):
    if not isinstance(evidence, dict) or not isinstance(context, dict) or evidence.get("context") != context:
        return False
    reports = evidence.get("reports", {})
    return (isinstance(reports, dict) and set(reports) == set(context["required"])
            and all(isinstance(r, dict) and r.get("context") == context for r in reports.values())
            and all(set(r) == {"context", "observed", "queries", "effects", "result"}
                    and r.get("result") == "returned" and isinstance(r.get("observed"), dict)
                    and set(r["observed"]) == set(context["keys"])
                    and all(type(value) is int for value in r["observed"].values())
                    and r.get("queries") == [dict(key=k, value=r["observed"][k]) for k in context["keys"]]
                    and isinstance(r.get("effects"), dict) and set(r["effects"]) <= set(context["effects"])
                    and all(type(value) is int for value in r["effects"].values())
                    for r in reports.values())
            and len({digest(r) for r in reports.values()}) == 1)


def verified_outcome(evidence, outcome):
    report = next(iter(evidence["reports"].values()))
    return (outcome.get("position") == evidence["context"]["position"]
            and outcome.get("observed") == report.get("observed")
            and outcome.get("values") == report.get("effects"))


class Checker(Actor):
    """One recoverable fixture invocation; physical inputs have explicit leases."""
    def __init__(self, endpoint, delay_ns=0, missing=False, mismatch=False,
                 retain_inputs=False, page_bytes=2048):
        self.endpoint, self.delay_ns = endpoint, delay_ns
        self.missing, self.mismatch = missing, mismatch
        self.retain_inputs, self.page_bytes = retain_inputs, page_bytes
        self.job = self.report = self.lease = None
        self.values, self.frames = {}, []
        self.ready = self.pending = self.delayed = False

    def start_job(self, ctx, job):
        self.job = clone(job)
        self.lease = ctx.reserve(encoded_size(job) + 1024, "checked-invocation")
        if self.lease is None:
            self.job = None
            ctx.wait("checker", "invocation capacity")
            return False
        return True

    def pump(self, ctx):
        if not self.ready or self.job is None or self.pending:
            return
        tx = self.job["context"]["tx"]
        if self.report is not None:
            send(ctx, self.job["reply"], "checked-report", dict(tx=tx, checker=ctx.actor, report=self.report))
            return
        if self.missing:
            ctx.wait(tx, "required checker unavailable")
            return
        if not self.delayed:
            self.pending = ctx.compute(max(1, self.delay_ns), "delay-complete", leases=(self.lease,))
            return
        missing = [k for k in self.job["context"]["keys"] if k not in self.values]
        if missing:
            context = self.job["context"]
            send(ctx, self.endpoint, "private-read", dict(tx=tx, key=missing[0],
                position=context["position"], invocation=context["invocation"], reply=ctx.actor))
            return
        context, plan = self.job["context"], self.job["plan"]
        report = dict(context=context, observed=self.values,
                      queries=[dict(key=k, value=self.values[k]) for k in context["keys"]],
                      effects=effects(plan, self.values), result="returned")
        if self.mismatch:
            report["result"] = "different native result"
        self.pending = ctx.persist("report", report, "report-stored", dict(report=report))

    def on(self, ctx, kind, data):
        if kind in ("boot", "boot-retry"):
            if not ctx.scan("", "reopened"):
                ctx.timer(100_000, "boot-retry")
            return
        if kind == "reopened":
            if not data["ok"]:
                ctx.timer(100_000, "boot-retry")
                return
            records = data["records"]
            if "invocation" in records:
                if not self.start_job(ctx, records["invocation"]):
                    ctx.timer(100_000, "boot-retry")
                    return
                self.report = records.get("report")
                ctx.note("checker_recovered", op=self.job["context"]["tx"], has_report=self.report is not None)
            self.ready = True
            self.pump(ctx)
            ctx.timer(100_000, "tick")
            return
        if not self.ready:
            return
        if kind == "tick":
            self.pump(ctx)
            ctx.timer(100_000, "tick")
        elif kind == "invoke":
            if self.job is not None:
                if data != self.job:
                    raise ValueError("checker invocation identity reused")
                self.pump(ctx)
            elif self.start_job(ctx, data):
                self.pending = ctx.persist("invocation", self.job, "invocation-stored")
                if not self.pending:
                    ctx.release(self.lease)
                    self.job = self.lease = None
        elif kind == "invocation-stored":
            self.pending = False
            if data["ok"]:
                ctx.note("checker_invocation_durable", op=self.job["context"]["tx"], context=self.job["context"])
                self.pump(ctx)
        elif kind == "delay-complete":
            self.pending = False
            self.delayed = True
            self.pump(ctx)
        elif kind == "private-result":
            if self.job is None or self.report is not None:
                return
            if data["status"] != "ok":
                ctx.wait(self.job["context"]["tx"], "private query " + data["status"],
                         detail=data.get("reason"), predecessors=data.get("predecessors", []))
                return
            context = self.job["context"]
            if data["invocation"] != context["invocation"] or data["position"] != context["position"]:
                return
            key = data["key"]
            if key in self.values or self.pending:
                return
            frame = ctx.reserve(self.page_bytes, "checked-input-projection")
            if frame is None:
                ctx.wait(context["tx"], "checker input capacity")
                return
            self.pending = ctx.compute(1000, "decoded", dict(key=key, value=data["values"][key], frame=frame), leases=(frame,))
            if not self.pending:
                ctx.release(frame)
        elif kind == "decoded":
            self.pending = False
            self.values[data["key"]] = data["value"]
            if self.retain_inputs:
                self.frames.append(data["frame"])
            else:
                ctx.release(data["frame"])
            ctx.clear_wait(self.job["context"]["tx"])
            ctx.note("checker_private_read", op=self.job["context"]["tx"], key=data["key"],
                     position=self.job["context"]["position"], value=data["value"],
                     invocation=self.job["context"]["invocation"])
            self.pump(ctx)
        elif kind == "report-stored":
            self.pending = False
            if data["ok"]:
                self.report = data["report"]
                for frame in self.frames:
                    ctx.release(frame)
                self.frames.clear()
                ctx.note("checked_report_durable", op=self.job["context"]["tx"], report=self.report)
                self.pump(ctx)
        else:
            raise ValueError(kind)


@dataclass(frozen=True)
class Case:
    replicated: bool = True
    point_count: int = 24
    point_interval_ns: int = 30_000
    delayed_checker_ns: int = 1_500_000
    restart_checker: bool = False
    source_reset: bool = False
    incident: str = "none"
    missing_checker: bool = False
    mismatch_checker: bool = False
    checker_memory_bytes: int = 32_768
    retain_inputs: bool = False
    until_ns: int = 12_000_000


def build(case=Case(), seed=1, replay=None):
    from local_release import build as base_build
    from replicated_contention import Assembly
    from scenario import Scenario, Input, Fault
    config = Config(workload="points", topology="lan", width=4, shards=2,
                    count=case.point_count + 1, start_ns=100_000,
                    interval_ns=case.point_interval_ns, drain_ns=case.until_ns)
    strategy, initial = Strategy(), initial_state(config)
    factories = {f"coordinator{i}": lambda: CheckedCoordinator(strategy, config) for i in (0, 1)}
    if case.replicated:
        for shard in (0, 1):
            for replica in range(3):
                factories[f"consumer{shard}_{replica}"] = lambda shard=shard, replica=replica: SourceConsumer(
                    shard, initial, strategy, replica, (0, 3000, 9000)[replica])
        base_hosts = ["client", "h0", "h1"] + [f"c{s}_{r}" for s in (0, 1) for r in range(3)]
        base_hosts += [f"w{s}_{r}" for s in (0, 1) for r in (1, 2)]
        assembly = Assembly(placement={"coordinator0": "coord0"},
            hosts=(Host("coord0", workers=2, handler_ns=100, disk_latency=8000),),
            links=tuple(Link(a, b, latency=500, bandwidth=2)
                        for h in base_hosts for a, b in (("coord0", h), (h, "coord0"))))
    else:
        assembly = None
        for shard in (0, 1):
            factories[f"shard{shard}"] = lambda shard=shard: SourceShard(shard, initial, strategy, 5)
    world, _, _ = base_build(config, strategy, seed, replicated=case.replicated, offers=False,
                              assembly=assembly, factories=factories, replay=replay)
    for index, checker in enumerate(CHECKERS):
        host = f"check_host{index}"
        peers = list(world.hosts)
        world.add_host(Host(host, memory_bytes=case.checker_memory_bytes, workers=1,
                            handler_ns=100, disk_latency=8000))
        for peer in peers:
            world.add_link(Link(host, peer, latency=500, bandwidth=2))
            world.add_link(Link(peer, host, latency=500, bandwidth=2))
        endpoint = f"consumer0_{index}" if case.replicated else "shard0"
        world.add_actor(checker, host, lambda index=index, endpoint=endpoint: Checker(
            endpoint, delay_ns=case.delayed_checker_ns if index else 0,
            missing=case.missing_checker and index == 1,
            mismatch=case.mismatch_checker and index == 1, retain_inputs=case.retain_inputs))
    checked = dict(id=1, at=config.start_ns, origin=0, cohort="checked", program="sum",
                   reads=[f"s0:k{i}" for i in range(config.width)], writes=["s0:answer"],
                   compute=0, source_version=SOURCE_VERSION, code_version=CODE_VERSION,
                   profile=PROFILE, required_checkers=list(CHECKERS), checked=True)
    points = [dict(id=i+2, at=0, origin=i%2, cohort="independent-point" if i%3 == 2 else "source-point",
                   program="set", reads=[], writes=[f"s{1 if i%3 == 2 else 0}:k{i%config.width}"], value=1000+i, compute=2000,
                   source_version=SOURCE_VERSION) for i in range(case.point_count)]
    plans = [checked, *points]
    # The registry is observer input; actors receive only actual offered plans.
    world.contention_offers, world.contention_config, world.contention_strategy = plans, config, strategy
    world.demanding_case = case
    world.inject("client", "offer", checked, at=checked["at"])
    scenario = Scenario(world).when("source-writes", "checked_context_ready", lambda e: e["op"] == 1,
        [Input((i+1)*case.point_interval_ns, "client", "offer", plan, time_field="at")
         for i, plan in enumerate(points)])
    if case.incident not in ("none", "checker-restart", "source-reset", "coordinator-verification-reset", "coordinator-outcome-reset"):
        raise ValueError("unsupported demanding-case incident")
    if case.restart_checker or case.incident == "checker-restart":
        scenario.when("checker-restart", "durable_write",
            lambda e: e["actor"] == "checker_b" and e["key"] == "invocation",
            [Fault(0, "crash", dict(actor="checker_b")), Fault(200_000, "restart", dict(actor="checker_b"))])
    if case.source_reset or case.incident == "source-reset":
        source_host = "c0_1" if case.replicated else "h0"
        scenario.when("source-reset", "traffic_response", lambda e: e["op"] == 5,
            [Fault(0, "power_loss", dict(host=source_host)), Fault(100_000, "power_on", dict(host=source_host))])
    if case.incident.startswith("coordinator-"):
        key = "verification/1" if case.incident == "coordinator-verification-reset" else "outcome/1"
        host = world.actors["coordinator0"].host
        scenario.when(case.incident, "durable_write", lambda e: e["actor"] == "coordinator0" and e["key"] == key,
            [Fault(0, "power_loss", dict(host=host)), Fault(200_000, "power_on", dict(host=host))])
    world.demanding_scenario = scenario
    return world, plans, case.until_ns, scenario


def audit(world, plans, case):
    from replicated_contention import audit as replicated_audit
    from traffic import audit as traffic_audit
    errors = replicated_audit(world) if case.replicated else traffic_audit(world, plans, world.contention_config)
    inputs = {p["id"]: p for p in plans}
    initial = initial_state(world.contention_config)
    all_outcomes = {}
    for e in world.trace:
        if e["kind"] == "durable_write" and e["key"].startswith("outcome/"):
            all_outcomes[int(e["key"].split("/")[1])] = e["value"]
    def expected_context(tx, position):
        p = inputs[tx]
        expected = dict(tx=tx, position=position, keys=sorted(p["reads"]), effects=sorted(p["writes"]),
            required=p["required_checkers"], source_version=p["source_version"],
            code_version=p["code_version"], profile=p["profile"])
        return dict(expected, invocation=digest(expected))
    def snapshot(tx, position, key):
        return max([(0, initial[key])] + [(outcome["position"], outcome["values"][key])
            for other, outcome in all_outcomes.items() if other != tx and outcome["position"] <= position
            and key in outcome["values"]])[1]
    def check_report(report, tx, position):
        expected = expected_context(tx, position)
        if report.get("context") != expected:
            errors.append(f"checked source/code/profile/context identity changed {tx}")
            return
        observed = report.get("observed", {})
        values = {key: snapshot(tx, position, key) for key in inputs[tx]["reads"]}
        if observed != values:
            errors.append(f"checked report used wrong old-cut snapshot {tx}")
        if report.get("queries") != [dict(key=k, value=observed.get(k)) for k in expected["keys"]]:
            errors.append(f"checked report omitted or changed query transcript {tx}")
        if report.get("effects") != {inputs[tx]["writes"][0]: sum(values.values())}:
            errors.append(f"checked report effects differ from old-cut program {tx}")
    contexts, reports, verifications, positions, installed_contexts, invocations = {}, {}, {}, {}, {}, {}
    pending_outputs, installed_outputs = {}, {}
    source_journals = {}
    for e in world.trace:
        if e["kind"] == "process_crash":
            installed_contexts = {key:value for key,value in installed_contexts.items() if key[0] != e["actor"]}
        if e["kind"] == "durable_write" and e["actor"].startswith("shard") and e["key"].startswith("journal/"):
            source_journals.setdefault(e["actor"], {})[e["key"]] = e["value"]
        if e["kind"] == "traffic_recovered" and e["actor"].startswith("shard"):
            for _, batch in sorted(source_journals.get(e["actor"], {}).items(), key=lambda item:int(item[0].split("/")[1])):
                for request in batch:
                    if request["kind"] == "register-context":
                        installed_contexts[e["actor"], request["tx"]] = request["context"]
        if e["kind"] in ("checked_source_output", "replicated_output"):
            request = e["request"]
            identity = e["actor"], request["tx"]
            if request["kind"] == "announce":
                pending_outputs.setdefault(identity, e["result"]["minimum"])
            elif request["kind"] == "fix":
                pending_outputs[identity] = request["position"]
            elif request["kind"] == "resolve":
                installed_outputs[identity] = request["values"]
        if e["kind"] in ("traffic_transition", "replicated_transition") and e["request"]["kind"] == "register-context":
            tx, context = e["request"]["tx"], e["request"]["context"]
            if context != expected_context(tx, positions.get(tx)):
                errors.append(f"agreed checked context differs from durable invocation position {tx}")
            installed_contexts[e["actor"], tx] = context
        if e["kind"] == "checked_context_ready":
            if e["context"] != expected_context(e["op"], positions.get(e["op"])):
                errors.append(f"checked source/code/profile/context identity changed {e['op']}")
            if e["op"] in contexts and contexts[e["op"]] != e["context"]:
                errors.append("checked context changed after recovery")
            contexts[e["op"]] = e["context"]
        if e["kind"] == "checked_private_observation":
            tx = e["op"]
            if e["before"] != e["after"]:
                errors.append("private query changed agreed logical state")
            if e["status"] == "ok":
                context = installed_contexts.get((e["actor"], tx))
                if (context is None or e["position"] != context["position"]
                        or e["invocation"] != context["invocation"] or e["key"] not in context["keys"]):
                    errors.append(f"private query before locally agreed context {tx}")
                if e["values"] != {e["key"]: snapshot(tx, e["position"], e["key"])}:
                    errors.append(f"private query returned wrong old-cut snapshot {tx}")
                for (actor, other), position in pending_outputs.items():
                    if (actor != e["actor"] or other == tx or position > e["position"]
                            or (actor, other) in installed_outputs or e["key"] not in inputs[other]["writes"]):
                        continue
                    superseded = world.contention_strategy.supersede and any(
                        replica == actor and e["key"] in values and position < positions.get(replacement, 0) <= e["position"]
                        for (replica, replacement), values in installed_outputs.items())
                    if not superseded:
                        errors.append(f"private query skipped pending predecessor {other}")
        if e["kind"] == "checker_private_read":
            if e["value"] != snapshot(e["op"], e["position"], e["key"]):
                errors.append(f"checker used wrong old-cut snapshot {e['op']}")
        if e["kind"] != "durable_write":
            continue
        if e["key"].startswith("position/"):
            positions[int(e["key"].split("/")[1])] = e["value"]
        if e["actor"] in CHECKERS and e["key"] == "invocation":
            invocation = e["value"]
            tx = invocation["context"]["tx"]
            if invocation["context"] != expected_context(tx, positions.get(tx)):
                errors.append(f"durable checker source/code/profile/context identity changed {tx}")
            invocations[e["actor"]] = invocation
        if e["actor"] in CHECKERS and e["key"] == "report":
            report = e["value"]
            tx = report["context"]["tx"]
            if invocations.get(e["actor"], {}).get("context") != report["context"]:
                errors.append(f"checker report lacks matching durable invocation {tx}")
            check_report(report, tx, positions.get(tx))
            reports[e["actor"]] = e["value"]
        if e["key"].startswith("verification/"):
            tx = int(e["key"].split("/")[1])
            evidence = e["value"]
            if not valid_verification(evidence, expected_context(tx, positions.get(tx))):
                errors.append(f"invalid verification evidence {tx}")
            if any(reports.get(checker) != report for checker, report in evidence.get("reports", {}).items()):
                errors.append(f"verification before actual durable checker report {tx}")
            for report in evidence.get("reports", {}).values():
                check_report(report, tx, positions.get(tx))
            verifications[tx] = evidence
        if e["key"].startswith("outcome/"):
            tx = int(e["key"].split("/")[1])
            if inputs.get(tx, {}).get("checked") and tx not in verifications:
                errors.append(f"checked outcome before durable matching reports {tx}")
            elif inputs.get(tx, {}).get("checked") and not verified_outcome(verifications[tx], e["value"]):
                errors.append(f"checked outcome does not match verified report {tx}")
    return sorted(set(errors))


def run_case(case=Case(), seed=1, replay=None):
    from campaigns import cohort
    world, plans, until, scenario = build(case, seed, replay)
    world.run(until=until, max_events=1_000_000)
    errors = audit(world, plans, case)
    arrivals = {"1": plans[0]["at"]}
    arrivals.update({str(r["data"]["id"]): r["at"] for r in scenario.scheduled_inputs})
    finished, refused = {}, {}
    for e in world.trace:
        if e["kind"] == "traffic_response": finished.setdefault(str(e["op"]), e["time"])
        if e["kind"] == "traffic_refused": refused.setdefault(str(e["op"]), e["time"])
    cohorts = {}
    end = max(arrivals.values())+1
    for name in sorted({p["cohort"] for p in plans}):
        ids = {str(p["id"]) for p in plans if p["cohort"] == name}
        active = {k:v for k,v in arrivals.items() if k in ids}
        c = cohort(active, {k:v for k,v in finished.items() if k in ids},
                   {k:v for k,v in refused.items() if k in ids}, offered_until=end, until=until)
        c["declared_but_untriggered"] = len(ids - set(arrivals))
        cohorts[name] = c
    return dict(case=asdict(case), seed=seed, cohorts=cohorts, violations=errors,
                coverage=scenario.coverage(), hosts=world.report()["hosts"],
                trace_hash=world.report()["trace_hash"], choices_hash=world.report()["choices_hash"]), world
