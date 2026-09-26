#!/usr/bin/env python3
"""Deterministic semantic core for BRIEF2's snapshot and output certification.

The caller supplies the agreed operation order, attempt-owned positions, complete
local output groups, and delivery of decisions. This is not an epoch scheduler,
SQL engine, durability model, timestamp allocator, or elapsed-time benchmark.
Scopes have a fixed declared key universe; None represents a known absent row.
Metadata and unresolved promises are retained indefinitely in this small model.
An in_flight call is an original request whose participant has not received
abort; the caller must fence it on abort delivery. It never revives a decision.
Optional capture waiting and a forced commit minimum support comparator policies;
the default calls follow BRIEF2's fail-fast captures and optional promotion.
"""

from collections import Counter
from dataclasses import dataclass, field
import json


Label = tuple[int, str]
ZERO: Label = (0, "")


def next_owned(counter_floor: int, owner: str,
               strict_after: list[Label] | tuple[Label, ...] = ()) -> Label:
    """Smallest (counter, owner) above every strict bound and at the floor."""
    assert owner and counter_floor >= 0
    counter = max([counter_floor, *(bound[0] for bound in strict_after)])
    if any((counter, owner) <= bound for bound in strict_after):
        counter += 1
    return (counter, owner)


@dataclass(frozen=True)
class Version:
    position: Label
    value: int | None


@dataclass
class Promise:
    owner: str
    keys: tuple[str, ...]
    minimum: Label
    remaining: set[str]

    @property
    def b(self) -> Label:
        return self.minimum


@dataclass
class Attempt:
    owner: str
    s: Label
    c: Label | None = None
    reads: dict[str, dict[str, int | None]] = field(default_factory=dict)
    writes: dict[str, int | None] = field(default_factory=dict)
    observations: list[dict] = field(default_factory=list)
    renewed: set[str] = field(default_factory=set)
    manifest: dict[str, int | None] | None = None
    installed: set[str] = field(default_factory=set)
    rejected: bool = False
    rejection_reason: str | None = None
    last_wait_reason: str | None = None
    wait_blockers: list[str] = field(default_factory=list)
    decision: str | None = None
    counters: Counter = field(default_factory=Counter)

    @property
    def status(self) -> str:
        if self.decision is not None:
            return "committed" if self.decision == "commit" else "aborted"
        return "rejected" if self.rejected else "pending"


class Store:
    def __init__(self, initial: dict[str, int | None],
                 scopes: dict[str, tuple[str, ...]]):
        self.initial = dict(initial)
        self.scopes = {name: tuple(sorted(set(keys)))
                       for name, keys in scopes.items()}
        assert all(set(keys) <= initial.keys() for keys in self.scopes.values())
        assert all(any(keys == (key,) for keys in self.scopes.values())
                   for key in initial), "declare point scopes, including absent keys"
        self.key_scopes = {
            key: tuple(sorted(name for name, keys in self.scopes.items() if key in keys))
            for key in initial
        }
        self.versions = {key: [Version(ZERO, value)] for key, value in initial.items()}
        self.R = dict.fromkeys(self.scopes, ZERO)
        self.W = dict.fromkeys(self.scopes, ZERO)
        self.attempts: dict[str, Attempt] = {}
        self.promises: dict[str, list[Promise]] = {}
        self.claims: dict[str, Promise] = {}
        self.trace: list[dict] = []
        self.counters: Counter = Counter()

    def _count(self, attempt: Attempt, name: str, amount: int = 1):
        self.counters[name] += amount
        attempt.counters[name] += amount

    def _event(self, attempt: Attempt, operation: str, **detail):
        self.trace.append({"owner": attempt.owner, "operation": operation, **detail})

    def _active(self, attempt: Attempt, in_flight: bool = False) -> bool:
        assert self.attempts[attempt.owner] is attempt
        if in_flight:
            # The caller fences these original requests once their participant
            # receives abort. A rejection elsewhere is not instant cancellation.
            assert attempt.decision != "commit"
            return True
        assert attempt.decision is None
        return not attempt.rejected

    def _reject(self, attempt: Attempt, reason: str, **detail) -> bool:
        attempt.rejected = True
        if attempt.rejection_reason is None:
            attempt.rejection_reason = reason
        self._count(attempt, "rejections")
        self._count(attempt, "rejected_" + reason)
        self._event(attempt, "reject", reason=reason, **detail)
        return False

    def begin(self, owner: str, s: Label) -> Attempt:
        assert owner not in self.attempts, "attempt identities cannot be reused"
        assert s[1] == owner and s > ZERO, "snapshot positions belong to their attempt"
        attempt = Attempt(owner, s)
        self.attempts[owner] = attempt
        self.promises[owner] = []
        self._count(attempt, "attempts")
        self._event(attempt, "begin", s=s)
        return attempt

    def snapshot(self, scope: str, position: Label) -> dict[str, int | None]:
        """Raw retained versions, without certification; not a public read API."""
        return {
            key: max((v for v in self.versions[key] if v.position <= position),
                     key=lambda v: v.position).value
            for key in self.scopes[scope]
        }

    def _blocking(self, attempt: Attempt, scope: str, position: Label) -> list[str]:
        return sorted({self.claims[key].owner for key in self.scopes[scope]
                       if key in self.claims
                       and self.claims[key].owner != attempt.owner
                       and self.claims[key].minimum <= position})

    def capture(self, attempt: Attempt, scope: str, *, in_flight: bool = False,
                wait: bool = False) -> bool:
        if not self._active(attempt, in_flight):
            return False
        assert attempt.manifest is None and attempt.c is None, "computation precedes promises"
        self._count(attempt, "capture_calls")
        attempt.last_wait_reason = None
        attempt.wait_blockers = []
        blockers = self._blocking(attempt, scope, attempt.s)
        if blockers:
            if wait:
                attempt.last_wait_reason = "capture_promise"
                attempt.wait_blockers = blockers
                self._count(attempt, "capture_waits")
                self._event(attempt, "capture_wait", scope=scope, blockers=blockers)
                return False
            return self._reject(attempt, "capture_promise", scope=scope, blockers=blockers)
        values = self.snapshot(scope, attempt.s)
        overlay = {key: attempt.writes[key] for key in values if key in attempt.writes}
        values.update(overlay)
        attempt.reads[scope] = dict(values)
        attempt.observations.append({"scope": scope, "position": attempt.s,
                                     "values": dict(values), "overlay": overlay})
        self.R[scope] = max(self.R[scope], attempt.s)
        self._count(attempt, "captured_scopes")
        self._count(attempt, "captured_rows", len(values))
        self._event(attempt, "capture", scope=scope, values=values)
        return True

    def _manifest(self, attempt: Attempt):
        assert set(attempt.writes) <= self.versions.keys()
        if attempt.manifest is None:
            attempt.manifest = dict(attempt.writes)
        assert attempt.manifest == attempt.writes, "writes are immutable after the first promise"

    def promise(self, attempt: Attempt, keys, *, in_flight: bool = False) -> bool:
        """Atomically acquire the complete local output set supplied by the caller."""
        keys = tuple(sorted(set(keys)))
        assert keys and set(keys) <= attempt.writes.keys()
        if attempt.decision == "commit":
            assert self.attempts[attempt.owner] is attempt and in_flight
            assert any(p.keys == keys for p in self.promises[attempt.owner]), "only duplicate committed promises"
            self._manifest(attempt)
            self._count(attempt, "promise_calls")
            self._count(attempt, "duplicate_promises")
            return True
        if not self._active(attempt, in_flight):
            return False
        self._manifest(attempt)
        self._count(attempt, "promise_calls")
        for previous in self.promises[attempt.owner]:
            if previous.keys == keys:
                self._count(attempt, "duplicate_promises")
                return True
        assert attempt.c is None, "no new participant after choosing the commit position"
        assert not any(key in self.claims and self.claims[key].owner == attempt.owner
                       for key in keys), "participant output groups must not overlap"
        blockers = sorted({self.claims[key].owner for key in keys if key in self.claims})
        if blockers:
            return self._reject(attempt, "output_promise", keys=keys, blockers=blockers)
        affected = sorted({scope for key in keys for scope in self.key_scopes[key]})
        heads = [self.versions[key][-1].position for key in keys]
        foreign_reads = [self.R[scope] for scope in affected
                         if self.R[scope][1] != attempt.owner]
        # Before c is chosen, this attempt can only have read at its own s.
        assert all(self.R[scope] <= attempt.s or self.R[scope][1] != attempt.owner
                   for scope in affected)
        b = next_owned(attempt.s[0], attempt.owner, strict_after=heads + foreign_reads)
        claim = Promise(attempt.owner, keys, b, set(keys))
        self.promises[attempt.owner].append(claim)
        for key in keys:
            self.claims[key] = claim
        self._count(attempt, "promised_groups")
        self._count(attempt, "promised_keys", len(keys))
        self._event(attempt, "promise", keys=keys, minimum=b)
        return True

    def choose(self, attempt: Attempt, minimum: Label | None = None) -> Label:
        assert self._active(attempt)
        assert minimum is None or minimum[1] == attempt.owner, "commit minima belong to their attempt"
        self._manifest(attempt)
        if attempt.c is not None:
            assert minimum is None or minimum <= attempt.c, "the chosen position cannot be raised"
            return attempt.c
        promised = {key for claim in self.promises[attempt.owner] for key in claim.keys}
        assert promised == attempt.writes.keys(), "all outputs must be promised before choose"
        attempt.c = max([attempt.s, minimum or attempt.s,
                         *(p.minimum for p in self.promises[attempt.owner])])
        self._count(attempt, "positions_chosen")
        if attempt.c > attempt.s:
            self._count(attempt, "promotions")
        self._event(attempt, "choose", c=attempt.c)
        return attempt.c

    def renew(self, attempt: Attempt, scope: str, *, in_flight: bool = False) -> bool:
        if not self._active(attempt, in_flight):
            return False
        assert attempt.c is not None and scope in attempt.reads
        self._count(attempt, "renew_calls")
        if attempt.c == attempt.s:
            return True
        # This is prewrite evidence even when this attempt also writes the scope.
        if self.W[scope] > attempt.s:
            return self._reject(attempt, "source_changed", scope=scope, W=self.W[scope])
        blockers = self._blocking(attempt, scope, attempt.c)
        if blockers:
            return self._reject(attempt, "renew_promise", scope=scope, blockers=blockers)
        self.R[scope] = max(self.R[scope], attempt.c)
        attempt.renewed.add(scope)
        self._count(attempt, "renewed_scopes")
        self._event(attempt, "renew", scope=scope, c=attempt.c)
        return True

    def decide(self, attempt: Attempt, commit: bool):
        assert self.attempts[attempt.owner] is attempt
        decision = "commit" if commit else "abort"
        if attempt.decision is not None:
            assert attempt.decision == decision, "a durable decision cannot be reversed"
            self._count(attempt, "duplicate_decisions")
            return
        if commit:
            assert not attempt.rejected
            self._manifest(attempt)
            if not attempt.writes and attempt.c is None:
                self.choose(attempt)
            assert attempt.c is not None
            assert all(key in self.claims and self.claims[key].owner == attempt.owner
                       for key in attempt.writes)
            assert attempt.c == attempt.s or attempt.reads.keys() <= attempt.renewed
        attempt.decision = decision
        self._count(attempt, "commit_decisions" if commit else "abort_decisions")
        self._event(attempt, "decide", decision=decision, c=attempt.c)

    def install(self, attempt: Attempt, keys):
        assert attempt.decision == "commit" and attempt.c is not None
        self._manifest(attempt)
        keys = tuple(sorted(set(keys)))
        assert set(keys) <= attempt.writes.keys()
        fresh = [key for key in keys if key not in attempt.installed]
        # Validate the entire supplied installation before changing any key.
        for key in fresh:
            assert key in self.claims and self.claims[key].owner == attempt.owner
            assert self.claims[key].minimum <= attempt.c
            assert self.versions[key][-1].position < attempt.c, "no backdated output insertion"
        for key in fresh:
            self.versions[key].append(Version(attempt.c, attempt.writes[key]))
            for scope in self.key_scopes[key]:
                self.W[scope] = max(self.W[scope], attempt.c)
            attempt.installed.add(key)
            self.claims[key].remaining.remove(key)
            del self.claims[key]
        self._count(attempt, "installed_keys", len(fresh))
        self._count(attempt, "duplicate_installed_keys", len(keys) - len(fresh))
        self._event(attempt, "install", keys=keys, fresh=fresh)

    def release(self, attempt: Attempt, keys):
        assert attempt.decision == "abort", "only a definitive abort releases unwritten promises"
        keys = tuple(sorted(set(keys)))
        assert set(keys) <= attempt.writes.keys()
        released = []
        for key in keys:
            if key in self.claims and self.claims[key].owner == attempt.owner:
                self.claims[key].remaining.remove(key)
                del self.claims[key]
                released.append(key)
        self._count(attempt, "released_keys", len(released))
        self._event(attempt, "release", keys=released)

    def head(self) -> dict[str, int | None]:
        return {key: versions[-1].value for key, versions in self.versions.items()}

    def check_serial(self, require_installed: bool = True) -> dict[str, int | None]:
        """Check committed captured values and fixed effects against final c order.

        Programs/constraints are caller-owned: this does not prove their output
        computation correct. Aborted observations have no published result.
        """
        state = dict(self.initial)
        committed = sorted((a for a in self.attempts.values() if a.decision == "commit"),
                           key=lambda a: a.c)
        for attempt in committed:
            for observation in attempt.observations:
                expected = {key: state[key] for key in self.scopes[observation["scope"]]}
                expected.update(observation["overlay"])
                assert expected == observation["values"], (attempt.owner, expected, observation)
            state.update(attempt.manifest)
            if require_installed:
                assert attempt.installed == attempt.writes.keys(), attempt.owner
        if require_installed:
            assert state == self.head(), (state, self.head())
        return state


def probe() -> list[dict]:
    """Small authored safety histories, independent of any event scheduler."""
    results = []

    def store(initial, **collections):
        return Store(initial, {**{key: (key,) for key in initial}, **collections})

    def finish(db, attempt, groups=None):
        if groups is None:
            groups = [tuple(attempt.writes)] if attempt.writes else []
        for keys in groups:
            assert db.promise(attempt, keys)
        db.choose(attempt)
        if attempt.c > attempt.s:
            for scope in attempt.reads:
                assert db.renew(attempt, scope)
        db.decide(attempt, True)
        for keys in groups:
            db.install(attempt, keys)

    def record(name, db):
        results.append({"name": name, "final": db.check_serial(),
                        "outcomes": {name: a.status for name, a in db.attempts.items()},
                        "counters": dict(db.counters)})

    db = store({"a": 100, "b": None, "out": None}, source=("a", "b"))
    scan = db.begin("scan", (10, "scan"))
    assert db.capture(scan, "source")
    scan.writes["out"] = max(v for v in scan.reads["source"].values() if v is not None)
    insert = db.begin("insert", (20, "insert"))
    insert.writes["b"] = 200
    finish(db, insert)
    finish(db, scan)
    assert scan.c == scan.s and db.head()["out"] == 100
    record("max-plus-mark-survives-source-insert", db)

    db = store({"a": 100, "b": None, "out": None}, source=("a", "b"))
    scan = db.begin("scan", (10, "scan"))
    assert db.capture(scan, "source")
    scan.writes["out"] = 100
    insert = db.begin("insert", (20, "insert"))
    assert db.capture(insert, "out")
    insert.writes["b"] = 200
    finish(db, insert)
    assert db.promise(scan, ("out",))
    assert db.choose(scan) > scan.s
    assert not db.renew(scan, "source")
    db.decide(scan, False)
    db.release(scan, scan.writes)
    assert db.head()["out"] is None
    record("marker-backedge-rejects-stale-maximum", db)

    db = store({"x": 0, "y": 0}, all=("x", "y"))
    bulk = db.begin("bulk", (10, "bulk"))
    assert db.capture(bulk, "all")
    bulk.writes.update({key: value + 1 for key, value in bulk.reads["all"].items()})
    report = db.begin("report", (20, "report"))
    assert db.capture(report, "all")
    finish(db, report)
    finish(db, bulk, [("x",), ("y",)])
    assert bulk.c > report.s and bulk.counters["captured_rows"] == 2
    assert bulk.renewed == {"all"} and db.head() == {"x": 1, "y": 1}
    record("bulk-promotes-past-report-without-recomputation", db)

    db = store({"x": 0, "y": 0})
    bulk = db.begin("bulk", (10, "bulk"))
    bulk.writes.update(x=4, y=4)
    later = db.begin("later", (20, "later"))
    later.writes["x"] = 9
    finish(db, later)
    finish(db, bulk)
    assert bulk.c > later.c and db.head() == {"x": 4, "y": 4}
    record("blind-bulk-promotes-instead-of-backdating", db)

    db = store({"x": 0})
    writer = db.begin("writer", (10, "writer"))
    writer.writes["x"] = 1
    assert db.promise(writer, ("x",))
    frozen = db.promises[writer.owner][0].minimum
    future = db.begin("future", (20, "future"))
    assert not db.capture(future, "x")
    db.decide(future, False)
    historical = db.begin("historical", (5, "historical"))
    assert db.capture(historical, "x")
    finish(db, historical)
    assert db.promises[writer.owner][0].minimum == frozen
    finish(db, writer)
    record("frozen-promise-rejects-new-read-admits-old-read", db)

    db = store({"x": 0, "y": 0})
    writer = db.begin("writer", (10, "writer"))
    writer.writes.update(x=1, y=1)
    assert db.promise(writer, ("x",)) and db.promise(writer, ("y",))
    db.choose(writer)
    db.decide(writer, True)
    db.install(writer, ("x",))
    db.install(writer, ("x",))
    future = db.begin("future", (20, "future"))
    assert db.capture(future, "x") and future.reads["x"]["x"] == 1
    assert not db.capture(future, "y")
    db.decide(future, False)
    old = db.begin("old", (5, "old"))
    assert db.capture(old, "x") and db.capture(old, "y")
    finish(db, old)
    assert old.reads == {"x": {"x": 0}, "y": {"y": 0}}
    db.install(writer, ("y",))
    db.decide(writer, True)
    complete = db.begin("complete", (30, "complete"))
    assert db.capture(complete, "x") and db.capture(complete, "y")
    finish(db, complete)
    assert writer.counters["installed_keys"] == 2
    assert writer.counters["duplicate_installed_keys"] == 1
    record("partial-install-never-publishes-fractured-report", db)

    db = store({"x": 0, "y": 0, "z": 0})
    blocker = db.begin("blocker", (10, "blocker"))
    blocker.writes["x"] = 1
    assert db.promise(blocker, ("x",))
    local = db.begin("local", (20, "local"))
    local.writes.update(x=2, y=2)
    assert not db.promise(local, ("x", "y")) and "y" not in db.claims
    db.decide(local, False)
    distributed = db.begin("distributed", (30, "distributed"))
    distributed.writes.update(x=3, y=3)
    assert db.promise(distributed, ("y",))
    assert not db.promise(distributed, ("x",))
    db.decide(distributed, False)
    assert db.claims["y"].owner == distributed.owner
    victim = db.begin("victim", (40, "victim"))
    victim.writes["y"] = 4
    assert not db.promise(victim, ("y",))
    db.decide(victim, False)
    unrelated = db.begin("unrelated", (50, "unrelated"))
    unrelated.writes["z"] = 5
    finish(db, unrelated)
    db.release(distributed, ("x", "y"))
    retry = db.begin("retry", (60, "retry"))
    retry.writes["y"] = 4
    finish(db, retry)
    db.decide(blocker, False)
    db.release(blocker, ("x",))
    record("local-all-or-none-and-real-distributed-bridge", db)

    return results


if __name__ == "__main__":
    print(json.dumps(probe(), indent=2))
