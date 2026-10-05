#!/usr/bin/env python3
"""Bounded descriptor histories; logical counters, not a performance model.

Run with --output lifecycle-results.json to retain counts and shortest witnesses.
The coordinate is class | slot << 4.  Reduced exhaustive cases use two classes
and two homes/class (home indices 0..3 denote bytes 0x00, 0x01, 0x10, 0x11);
the directed pressure cases keep all sixteen homes/class.
Payload tokens stand for independently expected bytes/identity, not addresses.
"""

from __future__ import annotations

import argparse
from collections import defaultdict, deque
from dataclasses import dataclass, replace
import hashlib
import json
from pathlib import Path
from typing import Callable, Hashable, Iterable


@dataclass
class Model:
    name: str
    initial: Hashable
    successors: Callable[[Hashable], Iterable[tuple[str, Hashable]]]
    violation: Callable[[Hashable], str | None]
    terminal: Callable[[Hashable], bool]
    expected_error: str | None = None
    marker: Callable[[Hashable], str | None] = lambda _: None


def explore(model: Model, limit: int = 100_000) -> dict:
    """Enumerate every reachable non-violating state and its outgoing edges.

    No partial-order reduction.  An invalid edge is recorded and cut, rather
    than exploring arbitrary behavior after the first broken obligation.
    """
    queue = deque([model.initial])
    previous = {model.initial: None}
    depth = {model.initial: 0}
    transitions = 0
    invalid_edges = 0
    terminals = 0
    terminal_states = set()
    incoming = defaultdict(set)
    witnesses = {}
    markers = {}

    def trace(state: Hashable) -> list[str]:
        steps = []
        while previous[state] is not None:
            parent, label = previous[state]
            steps.append(label)
            state = parent
        return list(reversed(steps))

    while queue:
        state = queue.popleft()
        if model.terminal(state):
            terminals += 1
            terminal_states.add(state)
        marker = model.marker(state)
        if marker is not None and marker not in markers:
            markers[marker] = trace(state)
        for label, next_state in model.successors(state):
            transitions += 1
            error = model.violation(next_state)
            if error is not None:
                invalid_edges += 1
                if error not in witnesses:
                    witnesses[error] = trace(state) + [label]
                continue
            incoming[next_state].add(state)
            if next_state not in previous:
                if len(previous) >= limit:
                    raise RuntimeError(f"{model.name}: state limit {limit} reached")
                previous[next_state] = (state, label)
                depth[next_state] = depth[state] + 1
                queue.append(next_state)

    can_complete = set(terminal_states)
    queue.extend(terminal_states)
    while queue:
        for parent in incoming[queue.popleft()]:
            if parent not in can_complete:
                can_complete.add(parent)
                queue.append(parent)

    if model.expected_error is None:
        assert invalid_edges == 0, (model.name, witnesses)
        assert terminals > 0, f"{model.name}: no completed history"
        assert len(can_complete) == len(previous), f"{model.name}: stranded valid state"
    else:
        assert model.expected_error in witnesses, (model.name, witnesses)
    return {
        "case": model.name,
        "states": len(previous),
        "transitions": transitions,
        "invalid_transitions": invalid_edges,
        "completed_states": terminals,
        "states_without_completion_path": len(previous) - len(can_complete),
        "max_depth": max(depth.values()),
        "state_limit": limit,
        "witnesses": witnesses,
        "markers": markers,
    }


@dataclass(frozen=True)
class Reader:
    phase: str = "start"
    home: int = -1
    expected: int = -1
    pin: bool = False
    domain: bool = False
    grant: bool = False
    cancelled: bool = False


@dataclass(frozen=True)
class AccessState:
    homes: tuple[int | None, ...]
    current: int = 0
    readers: tuple[Reader, ...] = (Reader(),)
    writer: int = 0
    latch: int = -1
    gate: bool = False


def access_model(
    name: str,
    protocol: str,
    *,
    reader_count: int = 1,
    unrelated_last: bool = False,
    ignore_cancelled: bool = False,
    exclusive: str | None = None,
    expected_error: str | None = None,
) -> Model:
    """Move without changing bytes, or mutate under a drained acquisition gate.

    A grant covers a kernel/send and all pointers derived within that use.
    Cancel is merely a request: only completion releases the grant.  A short
    latch serializes resolve+retain with publication/reclaim, not payload use.
    Coarse protection enters a grid domain before reading a coordinate.
    """
    initial = AccessState(
        (101, None, 202 if unrelated_last else None, None),
        readers=tuple(Reader() for _ in range(reader_count)),
    )

    def update_reader(s: AccessState, i: int, r: Reader) -> AccessState:
        readers = list(s.readers)
        readers[i] = r
        return replace(s, readers=tuple(readers))

    def is_held(r: Reader) -> bool:
        return not (ignore_cancelled and r.cancelled)

    def retained(s: AccessState, home: int) -> bool:
        if protocol == "domain":
            return any(r.domain and is_held(r) for r in s.readers)
        return any(r.pin and r.home == home and is_held(r) for r in s.readers)

    def successors(s: AccessState):
        for i, r in enumerate(s.readers):
            p = f"reader {i}"
            if r.phase == "start" and not s.gate:
                if protocol == "latch" and s.latch == -1:
                    yield f"{p}: take acquisition latch", replace(
                        update_reader(s, i, replace(r, phase="protected")), latch=i
                    )
                elif protocol == "domain":
                    yield f"{p}: enter grid protection", update_reader(
                        s, i, replace(r, phase="protected", domain=True)
                    )
                elif protocol == "load_then_pin":
                    home = 2 if unrelated_last and i == reader_count - 1 else s.current
                    yield f"{p}: load coordinate and expected token", update_reader(
                        s, i, replace(r, phase="resolved", home=home, expected=s.homes[home])
                    )
            elif r.phase == "protected":
                home = 2 if unrelated_last and i == reader_count - 1 else s.current
                yield f"{p}: resolve protected coordinate", update_reader(
                    s, i, replace(r, phase="resolved", home=home, expected=s.homes[home])
                )
            elif r.phase == "resolved":
                next_state = update_reader(
                    s, i, replace(r, phase="granted", grant=True, pin=protocol != "domain")
                )
                if protocol == "latch":
                    next_state = replace(next_state, latch=-1)
                yield f"{p}: retain home and return raw grant", next_state
            elif r.phase == "granted":
                yield f"{p}: use raw reference", update_reader(s, i, replace(r, phase="observed"))
            elif r.phase == "observed":
                yield f"{p}: request cancellation (raw grant remains)", update_reader(
                    s, i, replace(r, phase="cancelled", cancelled=True)
                )
            elif r.phase == "cancelled":
                yield f"{p}: completion releases raw grant", update_reader(
                    s, i, replace(r, phase="done", grant=False, pin=False, domain=False)
                )

        if exclusive is not None:
            if exclusive == "gated":
                if s.writer == 0 and s.latch == -1:
                    yield "writer: close acquisition gate", replace(s, gate=True, writer=1)
                elif s.writer == 1 and not any(r.grant for r in s.readers):
                    yield "writer: prove all earlier grants drained", replace(s, writer=2)
                elif s.writer == 2:
                    yield "writer: mutate exclusively", replace(s, homes=(303, *s.homes[1:]), writer=3)
                elif s.writer == 3:
                    yield "writer: reopen acquisition gate", replace(s, gate=False, writer=4)
            elif exclusive == "ungated":
                if s.writer == 0 and not any(r.grant for r in s.readers):
                    yield "writer: observe zero grants without closing gate", replace(s, writer=1)
                elif s.writer == 1:
                    yield "writer: mutate after stale zero-grant observation", replace(
                        s, homes=(303, *s.homes[1:]), writer=4
                    )
            elif exclusive == "unique_head_only" and s.writer == 0:
                yield "writer: mutate on sole-head knowledge alone", replace(
                    s, homes=(303, *s.homes[1:]), writer=4
                )
            return

        if s.writer == 0:
            # Destination claim is serialized here; the separate claims case
            # explores the consequence of splitting scan and claim.
            assert s.homes[1] is None
            yield "writer: claim vacant destination home", replace(s, writer=1)
        elif s.writer == 1:
            yield "writer: finish copying payload token 101", replace(s, homes=(101, 101, *s.homes[2:]), writer=2)
        elif s.writer == 2 and s.latch == -1:
            yield "writer: publish initialized destination coordinate", replace(s, current=1, writer=3)
        elif s.writer == 3 and s.latch == -1 and not retained(s, 0):
            yield "writer: reclaim previous home", replace(s, homes=(None, *s.homes[1:]), writer=4)

    def violation(s: AccessState):
        if s.homes[s.current] is None:
            return "published binding names uninitialized storage"
        for r in s.readers:
            if r.grant and s.homes[r.home] != r.expected:
                return "live raw grant lost its expected payload"
        return None

    def marker(s: AccessState):
        if protocol == "domain" and unrelated_last and s.writer == 3:
            unrelated = any(r.domain and r.home == 2 for r in s.readers)
            old_used = any(r.grant and r.home == 0 for r in s.readers)
            if unrelated and not old_used:
                return "unrelated grid reader delays previous-home reclamation"
        return None

    return Model(
        name, initial, successors, violation,
        lambda s: s.writer == 4 and all(r.phase == "done" for r in s.readers),
        expected_error, marker,
    )


@dataclass(frozen=True)
class AbaState:
    current: int = 0
    homes: tuple[int | None, ...] = (101, None)
    reader: int = 0
    saved_coordinate: int = -1
    expected: int = -1
    observed: int = -1
    writer: int = 0
    passed: bool = False


def aba_model() -> Model:
    def successors(s: AbaState):
        if s.reader == 0:
            yield f"reader: save byte coordinate and payload token {s.homes[s.current]}", replace(
                s, reader=1, saved_coordinate=s.current, expected=s.homes[s.current]
            )
        elif s.reader == 1:
            same = s.current == s.saved_coordinate
            yield f"reader: byte comparison {'passes' if same else 'retries'}", replace(
                s, reader=2, passed=same, observed=s.homes[s.current] if same else -1
            )
        if s.writer == 0:
            yield "writer: publish version 202 at another coordinate", replace(s, homes=(101, 202), current=1, writer=1)
        elif s.writer == 1:
            yield "writer: reclaim original home", replace(s, homes=(None, 202), writer=2)
        elif s.writer == 2:
            yield "writer: initialize version 303 at original coordinate", replace(s, homes=(303, 202), writer=3)
        elif s.writer == 3:
            yield "writer: publish original byte coordinate again", replace(s, current=0, writer=4)

    error = "repeated coordinate falsely validates the selected logical version"
    return Model(
        "bad_coordinate_byte_is_version", AbaState(), successors,
        lambda s: error if s.passed and s.observed != s.expected else None,
        lambda s: s.reader == 2 and s.writer == 4, error,
    )


@dataclass(frozen=True)
class IdentityState:
    usable_old_head: bool = True
    owner_retired: bool = False
    incarnation: int = 1
    reader: int = 0
    bound_incarnation: int = 1
    reused: bool = False
    raw_grant: bool = False
    old_payload_present: bool = True


def identity_model(safe: bool) -> Model:
    def successors(s: IdentityState):
        if not s.owner_retired:
            yield "owner: retire allocation ownership", replace(s, owner_retired=True)
        if s.owner_retired and not s.reused and (not safe or not s.usable_old_head):
            yield "allocator: reuse trampoline for identity 2 with another payload", replace(s, incarnation=2, reused=True)
        if s.reader == 0 and s.usable_old_head:
            yield "reader: bind old head expecting identity 1", replace(
                s, reader=1, bound_incarnation=s.incarnation, raw_grant=True
            )
        elif s.reader == 1:
            yield "reader: retire final usable head while retaining raw payload grant", replace(
                s, reader=2, usable_old_head=False
            )
        elif s.reader == 2:
            yield "reader: complete raw payload use", replace(s, reader=3, raw_grant=False)
        if s.owner_retired and not s.usable_old_head and not s.raw_grant and s.old_payload_present:
            yield "allocator: reclaim old payload after raw completion", replace(s, old_payload_present=False)

    error = "old usable head resolved a different allocation identity"
    def violation(s: IdentityState):
        if s.reader >= 1 and s.bound_incarnation != 1:
            return error
        if s.raw_grant and not s.old_payload_present:
            return "trampoline retirement incorrectly reclaimed an old raw payload grant"
        return None

    return Model(
        "identity_reuse_after_head_retirement" if safe else "bad_owner_retirement_is_identity_retirement",
        IdentityState(), successors, violation,
        lambda s: s.reader == 3 and s.reused and not s.old_payload_present,
        None if safe else error,
        lambda s: "trampoline identity retired while old raw payload grant remains"
        if s.reused and s.raw_grant and not s.usable_old_head else None,
    )


@dataclass(frozen=True)
class VersionState:
    published: bool = False
    shared_binding: int = 0
    observed: tuple[int, int] = (-1, -1)


def version_model(view_specific: bool) -> Model:
    def successors(s: VersionState):
        if not s.published:
            yield "writer: publish logical version 2, retaining version 1 bytes", replace(
                s, published=True, shared_binding=1
            )
        if s.observed[0] == -1:
            location = 0 if view_specific else s.shared_binding
            yield "reader: bind selected historical view 1", replace(s, observed=(location + 1, s.observed[1]))
        if s.published and s.observed[1] == -1:
            location = 1 if view_specific else s.shared_binding
            yield "reader: bind selected current view 2", replace(s, observed=(s.observed[0], location + 1))

    error = "one current trampoline cannot locate both selected logical versions"
    return Model(
        "version_specific_bindings" if view_specific else "bad_shared_binding_is_mvcc_selection",
        VersionState(), successors,
        lambda s: error if s.observed[0] not in (-1, 1) or s.observed[1] not in (-1, 2) else None,
        lambda s: s.published and -1 not in s.observed,
        None if view_specific else error,
    )


@dataclass(frozen=True)
class ClaimsState:
    phases: tuple[int, int] = (0, 0)
    chosen: tuple[int, int] = (-1, -1)
    owners: tuple[int, int] = (-1, -1)


def claims_model(atomic: bool) -> Model:
    def successors(s: ClaimsState):
        for i in range(2):
            phases, chosen, owners = list(s.phases), list(s.chosen), list(s.owners)
            if phases[i] == 0:
                for slot, owner in enumerate(s.owners):
                    if owner == -1:
                        phases, chosen, owners = list(s.phases), list(s.chosen), list(s.owners)
                        chosen[i] = slot
                        phases[i] = 2 if atomic else 1
                        if atomic:
                            owners[slot] = i
                        yield f"mover {i}: {'claim' if atomic else 'observe vacant'} home {slot}", ClaimsState(
                            tuple(phases), tuple(chosen), tuple(owners)
                        )
            elif phases[i] == 1:
                owners[chosen[i]] = i
                phases[i] = 2
                yield f"mover {i}: claim earlier observed home {chosen[i]}", ClaimsState(
                    tuple(phases), tuple(chosen), tuple(owners)
                )

    error = "concurrent movers claimed the same destination home"
    return Model(
        "serialized_destination_claim" if atomic else "bad_split_scan_and_claim",
        ClaimsState(), successors,
        lambda s: error if s.phases == (2, 2) and s.chosen[0] == s.chosen[1] else None,
        lambda s: s.phases == (2, 2),
        None if atomic else error,
    )


def pressure_cases() -> list[dict]:
    """Directed geometry checks at the brief's actual sixteen-home bound."""
    encode = lambda cls, slot: cls | slot << 4
    homes = {encode(0, i): (i, 1) for i in range(16)}
    current = {i: encode(0, i) for i in range(16)}

    def vacant(cls):
        return [encode(cls, i) for i in range(16) if encode(cls, i) not in homes]

    assert not vacant(0)
    full_class = {
        "case": "sixteen_current_payloads_same_class_replacement",
        "current_payloads": 16,
        "retained_payloads": 0,
        "destination_class_free_homes": 0,
        "result": "would_block; explicit promotion/overflow or exclusive in-place semantics required",
    }
    # Each move retains its previous bytes for an unfinished raw grant.
    old_grants = dict(current)
    for i in range(16):
        home = vacant(1)[0]
        homes[home] = (i, 2)
        current[i] = home
    assert len(set(current.values())) == 16
    assert len(homes) == 32
    assert not vacant(0) and not vacant(1)
    retained = {
        "case": "sixteen_current_plus_sixteen_retained_payloads",
        "current_payloads": 16,
        "retained_payloads": 16,
        "occupied_homes": 32,
        "class_zero_free_homes": 0,
        "class_one_free_homes": 0,
        "result": "another replacement in either full class would_block",
    }
    before_cancel = dict(homes)
    # Cancellation did not establish completion, so no home is released.
    assert homes == before_cancel
    del homes[old_grants[0]]  # Completion of exactly one old grant.
    assert vacant(0) == [encode(0, 0)]
    home = vacant(0)[0]
    homes[home] = (0, 3)
    current[0] = home
    assert len(homes) == 32 and len(set(current.values())) == 16
    completion = {
        "case": "completion_releases_one_home",
        "cancel_releases_homes": 0,
        "one_completion_releases_homes": 1,
        "replacement_coordinate": home,
        "result": "replacement resumes without overwriting another live payload",
    }
    return [full_class, retained, completion]


def run() -> dict:
    models = [
        access_model("short_latch_two_readers", "latch", reader_count=2),
        access_model("grid_domain_two_readers", "domain", reader_count=2),
        access_model("per_home_with_unrelated_reader", "latch", reader_count=2, unrelated_last=True),
        access_model("grid_domain_with_unrelated_reader", "domain", reader_count=2, unrelated_last=True),
        access_model("bad_load_then_pin", "load_then_pin", expected_error="live raw grant lost its expected payload"),
        access_model("cancel_is_not_completion", "latch"),
        access_model("bad_cancel_releases_raw_grant", "latch", ignore_cancelled=True, expected_error="live raw grant lost its expected payload"),
        access_model("exclusive_gate_drains_grants", "latch", reader_count=2, exclusive="gated"),
        access_model("bad_zero_grants_without_gate", "latch", exclusive="ungated", expected_error="live raw grant lost its expected payload"),
        access_model("bad_unique_head_implies_exclusive_payload", "latch", exclusive="unique_head_only", expected_error="live raw grant lost its expected payload"),
        identity_model(True), identity_model(False),
        version_model(True), version_model(False),
        aba_model(), claims_model(True), claims_model(False),
    ]
    results = [explore(model) for model in models]
    return {
        "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "kind": "bounded interleaving model; no timing or probabilistic cost",
        "bounds": {
            "exhaustive_move_cases": "one mover, one/two readers, two classes, two homes/class",
            "claims_case": "two movers, two destination homes",
            "pressure_cases": "sixteen owners, sixteen homes/class, two populated classes",
            "exploration": "all interleavings of finite actions; invalid edges stop at first obligation failure",
        },
        "properties": [
            "published bindings name initialized payload",
            "every live raw grant keeps its independently expected payload token until completion",
            "identity reuse cannot make a usable old head name a different allocation",
            "selected logical views locate their own expected version",
            "simultaneous destination claims are distinct",
            "every reachable safe state has a path to completion in its finite history",
        ],
        "limitations": [
            "sequentially consistent abstract events; native ordering/atomicity must be validated separately",
            "small finite histories, serialized claim/publication as explicitly stated; not an unbounded proof",
            "tokens abstract complete bytes; no page faults, COW mappings, disk durability or recovery",
            "latch/domain accounting is an experimental protocol, not a prescribed runtime or per-pointer reference count",
            "same-version relocation is separated from MVCC view selection and in-place logical update",
            "pressure handling reports would_block; it does not choose virtual/overflow placement or scheduling policy",
        ],
        "cases": results,
        "pressure": pressure_cases(),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = run()
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(encoded)
    print(json.dumps({
        "cases": len(result["cases"]),
        "states": sum(r["states"] for r in result["cases"]),
        "transitions": sum(r["transitions"] for r in result["cases"]),
        "safe_cases": sum(not r["witnesses"] for r in result["cases"]),
        "counterexample_cases": sum(bool(r["witnesses"]) for r in result["cases"]),
        "output": str(args.output) if args.output else None,
    }, sort_keys=True))


if __name__ == "__main__":
    main()
