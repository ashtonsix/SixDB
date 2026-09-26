#!/usr/bin/env python3
"""Finite audit of releasing output allocation latches after exact publication.

This independent model collapses metadata exchanges into one publication step.
It has finite logical slots, including absent IDs; no transport, retention or
recovery implementation. Authored positions are unique logical integers.
Programs and completed reads are replayed independently in position order.
Unsafe controls must fail their semantic oracle, not pass as protocol results.
"""

import argparse
from dataclasses import dataclass, field
import hashlib
import json
from pathlib import Path

from write_admission import Admission


WAIT = object()


def evaluate(program, state):
    op, key = program["op"], program.get("key")
    if op == "put_many":
        return dict(program["values"]), {"ok": True}
    if op == "put":
        return {key: program["value"]}, {"ok": True}
    if op == "increment":
        assert state[key] is not None
        value = state[key] + program["delta"]
        return {key: value}, {"old": state[key], "new": value}
    if op == "update":
        exists = state[key] is not None
        return ({key: program["value"]} if exists else {}), {"affected": int(exists)}
    if op == "noop":
        return {}, {"ok": True}
    if op == "unique_put":
        occupied = any(state[row] == program["value"] for row in program["domain"] if row != key)
        return ({} if occupied else {key: program["value"]}), {"accepted": not occupied}
    assert op == "cascade"
    child, value = program["child"], program["value"]
    writes = {key: value}
    if state[key] != value and state[child] == state[key]:
        writes[child] = value
    return writes, {"old": state[key], "new": value}


def inputs(program):
    op = program["op"]
    if op in ("put", "put_many", "noop"):
        return ()
    if op == "unique_put":
        return tuple(row for row in program["domain"] if row != program["key"])
    if op == "cascade":
        return (program["key"], program["child"])
    return (program["key"],)


@dataclass
class Attempt:
    owner: str
    coverage: frozenset[str]
    position: int
    decision: str | None = None
    writes: dict = field(default_factory=dict)
    program: dict | None = None
    result: dict | None = None
    resolved: set = field(default_factory=set)


class Versions:
    def __init__(self, initial, release_early=True):
        self.initial = dict(initial)
        self.release_early = release_early
        self.gates = Admission({})
        self.attempts = {}
        self.slots = {key: [] for key in initial}
        self.frontier = dict.fromkeys(initial, 0)
        self.read_bounds = dict.fromkeys(initial, 0)
        self.installed = {key: [(0, "initial", value)] for key, value in initial.items()}
        self.observations = []
        self.events = []

    def allocate(self, owner, coverage, floor):
        coverage = frozenset(coverage)
        assert coverage and coverage <= self.initial.keys()
        if owner in self.attempts:
            assert self.attempts[owner].coverage == coverage
            return self.attempts[owner]
        self.gates.priority.setdefault(owner, (len(self.gates.priority), owner))
        shards = sorted({key.split("/", 1)[0] for key in coverage})
        for shard in shards:
            group = sorted(key for key in coverage if key.split("/", 1)[0] == shard)
            if not self.gates.request(owner, group):
                self.events.append({"event": "allocation_wait", "owner": owner, "shard": shard})
                return None
        position = max([floor, *(self.frontier[key] + 1 for key in coverage),
                        *(self.read_bounds[key] + 1 for key in coverage)])
        assert all(position != attempt.position for attempt in self.attempts.values()), "authored positions are unique"
        attempt = Attempt(owner, coverage, position)
        self.attempts[owner] = attempt
        for key in coverage:
            self.frontier[key] = position
            self.slots[key].append(owner)
        self.events.append({"event": "exact_position_everywhere", "owner": owner, "position": position})
        if self.release_early:
            self.gates.release_all(owner)
        return attempt

    def read(self, reader, key, position, *, unsafe_installed_only=False):
        self.read_bounds[key] = max(position, self.read_bounds[key])
        candidates = sorted((self.attempts[owner] for owner in self.slots[key]
                             if owner != reader and self.attempts[owner].position <= position),
                            key=lambda attempt: attempt.position, reverse=True)
        value = self.initial[key]
        for attempt in candidates:
            if key not in attempt.resolved:
                if unsafe_installed_only:
                    continue
                self.events.append({"event": "read_wait", "reader": reader, "key": key,
                                    "position": position, "blocker": attempt.owner})
                return WAIT
            if attempt.decision == "commit" and key in attempt.writes:
                value = attempt.writes[key]  # None is a tombstone, not no-write.
                break
        self.observations.append({"reader": reader, "key": key, "position": position, "value": value})
        return value

    def prepare(self, owner, program, *, unsafe_installed_only=False):
        attempt = self.attempts[owner]
        assert attempt.decision is None
        state = {}
        for key in inputs(program):
            value = self.read(owner, key, attempt.position, unsafe_installed_only=unsafe_installed_only)
            if value is WAIT:
                return None
            state[key] = value
        writes, result = evaluate(program, state)
        assert writes.keys() <= attempt.coverage
        attempt.program, attempt.writes, attempt.result = dict(program), writes, result
        return result

    def lookup_index(self, reader, value, position):
        # A deliberately conservative row-owned completeness ledger. Query all
        # row possibilities, then filter immutable index candidates by the
        # canonical row version. No physical index/routing cost is modeled.
        rows = {}
        for key in sorted(self.initial):
            row_value = self.read(reader, key, position)
            if row_value is WAIT:
                return WAIT
            rows[key] = row_value
        candidates = {key for key, versions in self.installed.items()
                      if any(p <= position and stored == value for p, _, stored in versions)}
        result = sorted(key for key in candidates if rows[key] == value)
        assert result == sorted(key for key, stored in rows.items() if stored == value)
        return result

    def decide(self, owner, commit):
        attempt = self.attempts[owner]
        if attempt.decision is not None:
            return attempt.decision == ("commit" if commit else "abort")
        if commit:
            assert attempt.program is not None
        attempt.decision = "commit" if commit else "abort"
        self.events.append({"event": "decision", "owner": owner, "decision": attempt.decision})
        return True

    def resolve(self, owner, keys=None):
        attempt = self.attempts[owner]
        assert attempt.decision is not None
        keys = attempt.coverage if keys is None else frozenset(keys)
        assert keys <= attempt.coverage
        for key in sorted(keys - attempt.resolved):
            if attempt.decision == "commit" and key in attempt.writes:
                self.installed[key].append((attempt.position, owner, attempt.writes[key]))
            attempt.resolved.add(key)
            self.events.append({"event": "resolve", "owner": owner, "key": key,
                                "kind": "value" if attempt.decision == "commit" and key in attempt.writes else "no_value"})
        if attempt.resolved == attempt.coverage:
            self.gates.release_all(owner)

    def finish(self, owner, program, **kwargs):
        result = self.prepare(owner, program, **kwargs)
        if result is None:
            return None
        assert self.decide(owner, True)
        self.resolve(owner)
        return result

    def state_at(self, position, exclude_owner=None):
        state = dict(self.initial)
        for attempt in sorted(self.attempts.values(), key=lambda attempt: attempt.position):
            if attempt.position <= position and attempt.owner != exclude_owner and attempt.decision == "commit":
                state.update(attempt.writes)
        return state

    def replay_errors(self):
        state, errors = dict(self.initial), []
        for attempt in sorted(self.attempts.values(), key=lambda attempt: attempt.position):
            if attempt.decision != "commit":
                continue
            expected_writes, expected_result = evaluate(attempt.program, state)
            if (expected_writes, expected_result) != (attempt.writes, attempt.result):
                errors.append({"kind": "program", "owner": attempt.owner,
                               "expected_writes": expected_writes, "actual_writes": attempt.writes,
                               "expected_result": expected_result, "actual_result": attempt.result})
            state.update(attempt.writes)
        for observed in self.observations:
            expected = self.state_at(observed["position"], observed["reader"])[observed["key"]]
            if expected != observed["value"]:
                errors.append({"kind": "read", "observed": observed, "expected": expected})
        return errors

    def report(self, name, valid=True, **detail):
        assert all(a.decision is not None and a.resolved == a.coverage for a in self.attempts.values())
        errors = self.replay_errors()
        assert bool(errors) != valid, (name, errors)
        return {"name": name, "expected_valid": valid, "serial_valid": not errors,
                **detail, "errors": errors, "events": self.events}


def put(key, value):
    return {"op": "put", "key": key, "value": value}


def overwrite_and_rmw():
    results = []
    for release_early in (False, True):
        for rmw in (False, True):
            db = Versions({"eu/x": 0}, release_early)
            first = db.allocate("A", ["eu/x"], 10)
            later = db.allocate("B", ["eu/x"], 20)
            program = {"op": "increment", "key": "eu/x", "delta": 1} if rmw else put("eu/x", 9)
            completed_early = False
            if release_early:
                assert later.position > first.position
                completed_early = db.finish("B", program) is not None
                assert completed_early != rmw
                if completed_early:
                    assert db.read("Q-new", "eu/x", 25) == 9
            else:
                assert later is None
            assert db.finish("A", put("eu/x", 7)) == {"ok": True}
            if not completed_early:
                db.allocate("B", ["eu/x"], 20)
                assert db.finish("B", program) is not None
            assert db.read("Q-old", "eu/x", 15) == 7
            assert db.read("Q-final", "eu/x", 30) == (8 if rmw else 9)
            results.append(db.report(f"{'release' if release_early else 'retain'}_{'rmw' if rmw else 'blind'}",
                                     metadata_pipelined=release_early, completed_before_predecessor=completed_early))
    return results


def no_write_abort_delete():
    results = []
    for resolution in ("noop", "abort", "delete"):
        db = Versions({"eu/x": 0})
        db.allocate("A", ["eu/x"], 10)
        db.allocate("B", ["eu/x"], 20)
        if resolution == "abort":
            assert db.decide("B", False)
            db.resolve("B")
            assert not db.decide("B", True), "late commit cannot revive abort"
        else:
            db.finish("B", {"op": "noop"} if resolution == "noop" else put("eu/x", None))
        value = db.read("Q", "eu/x", 25)
        assert value is (None if resolution == "delete" else WAIT)
        db.finish("A", put("eu/x", 7))
        assert db.read("Q", "eu/x", 25) == (None if resolution == "delete" else 7)
        next_writer = db.allocate("C", ["eu/x"], 1)
        assert next_writer.position > 25, "resolved slots do not rewind allocation/read frontiers"
        db.finish("C", put("eu/x", 8))
        results.append(db.report("later_" + resolution, tombstone=resolution == "delete"))
    return results


def partial_and_late_installation():
    results = []
    db = Versions({"eu/x": 0, "us/y": 0})
    db.allocate("A", db.initial, 10)
    db.allocate("B", db.initial, 20)
    # These are unconditional complete effects, with no affected-row or OLD result.
    program_a = {"op": "put_many", "values": {"eu/x": 1, "us/y": 1}}
    program_b = {"op": "put_many", "values": {"eu/x": 2, "us/y": 2}}
    db.prepare("B", program_b)
    db.decide("B", True)
    db.resolve("B", ["eu/x"])
    assert db.read("Q-x", "eu/x", 25) == 2
    assert db.read("Q-y", "us/y", 25) is WAIT
    db.resolve("B", ["us/y"])
    assert db.read("Q-y", "us/y", 25) == 2
    db.finish("A", program_a)
    assert db.read("Q-old", "eu/x", 15) == 1
    assert db.read("Q-new", "eu/x", 25) == 2
    wrong_arrival_head = db.installed["eu/x"][-1][2]
    assert wrong_arrival_head == 1 and wrong_arrival_head != db.state_at(25)["eu/x"]
    results.append(db.report("partial_install_and_late_older_version", unsafe_arrival_head=wrong_arrival_head,
                             correct_new_snapshot=2, correct_old_snapshot=1))

    db = Versions({"eu/0": None, "eu/1": None, "eu/2": None})
    broad = db.allocate("range", db.initial, 10)
    point = db.allocate("point", ["eu/1"], 1)
    assert point.position > broad.position
    db.finish("point", put("eu/1", 9))
    db.finish("range", put("eu/1", 7))
    assert db.read("Q", "eu/1", 30) == 9
    results.append(db.report("earlier_range_late_insert_is_shadowed", finite_slot_overlap_only=True))
    return results


def application_dependencies():
    results = []
    for unsafe in (False, True):
        db = Versions({"eu/x": 0})
        db.allocate("delete", ["eu/x"], 10)
        db.allocate("update", ["eu/x"], 20)
        program = {"op": "update", "key": "eu/x", "value": 9}
        early = db.finish("update", program, unsafe_installed_only=unsafe)
        assert (early is not None) == unsafe
        db.finish("delete", put("eu/x", None))
        if not unsafe:
            assert db.finish("update", program) == {"affected": 0}
        results.append(db.report("update_after_delete_" + ("unsafe" if unsafe else "wait"), valid=not unsafe))

    for unsafe in (False, True):
        db = Versions({"eu/a": 0, "us/b": 1})
        db.allocate("older", ["us/b"], 10)
        db.allocate("newer", ["eu/a"], 20)
        old_program = {"op": "unique_put", "key": "us/b", "value": 9, "domain": list(db.initial)}
        new_program = {"op": "unique_put", "key": "eu/a", "value": 9, "domain": list(db.initial)}
        early = db.finish("newer", new_program, unsafe_installed_only=unsafe)
        assert (early is not None) == unsafe
        assert db.finish("older", old_program) == {"accepted": True}
        if not unsafe:
            assert db.finish("newer", new_program) == {"accepted": False}
        unique = len(set(db.state_at(30).values())) == 2
        assert unique != unsafe
        results.append(db.report("unique_full_row_" + ("unsafe" if unsafe else "wait"),
                                 valid=not unsafe, unique_constraint_valid=unique))

    for unsafe in (False, True):
        db = Versions({"eu/parent": 1, "us/child": 1})
        db.allocate("older", db.initial, 10)
        db.allocate("newer", db.initial, 20)
        old_program = {"op": "cascade", "key": "eu/parent", "child": "us/child", "value": 2}
        new_program = {**old_program, "value": 1}
        early = db.finish("newer", new_program, unsafe_installed_only=unsafe)
        assert (early is not None) == unsafe
        db.finish("older", old_program)
        if not unsafe:
            db.finish("newer", new_program)
        state = db.state_at(30)
        valid_fk = state["eu/parent"] == state["us/child"]
        assert valid_fk != unsafe
        results.append(db.report("cascade_full_row_" + ("unsafe" if unsafe else "wait"),
                                 valid=not unsafe, foreign_key_valid=valid_fk))
    return results


def index_shadowing():
    results = []
    db = Versions({"eu/x": 0})
    db.allocate("A", ["eu/x"], 10)
    db.allocate("B", ["eu/x"], 20)
    db.finish("B", put("eu/x", 9))
    assert db.lookup_index("Q-new-before", 9, 25) == ["eu/x"]
    assert db.lookup_index("Q-old-before", 7, 25) == []
    db.finish("A", put("eu/x", 7))
    assert any(value == 7 for _, _, value in db.installed["eu/x"])
    assert db.lookup_index("Q-new-after", 9, 25) == ["eu/x"]
    assert db.lookup_index("Q-old-after", 7, 25) == []
    assert db.lookup_index("Q-historical", 7, 15) == ["eu/x"]
    results.append(db.report("row_version_filters_late_index_membership", stale_index_candidate_present=True))

    db = Versions({"eu/x": 0, "us/y": 0})
    db.allocate("A", db.initial, 10)
    db.allocate("B", ["eu/x"], 20)
    db.finish("B", put("eu/x", 9))
    assert db.lookup_index("Q", 9, 25) is WAIT, "x shadowing does not resolve y's possible index effect"
    db.finish("A", {"op": "put_many", "values": {"eu/x": 7, "us/y": 7}})
    assert db.lookup_index("Q", 9, 25) == ["eu/x"]
    results.append(db.report("shadowing_one_row_does_not_resolve_transaction", unshadowed_row_waited=True))
    return results


def capacity_cycle():
    def cyclic(edges):
        visiting, visited = set(), set()
        def visit(node):
            if node in visiting:
                return True
            if node in visited:
                return False
            visiting.add(node)
            if any(visit(other) for other in edges.get(node, ())):
                return True
            visiting.remove(node)
            visited.add(node)
            return False
        return any(visit(node) for node in edges)

    unsafe = {"A-data": ["B-metadata"], "B-metadata": ["A-data"]}
    preflight = {"B-capacity": ["A-data"], "A-data": []}
    assert cyclic(unsafe) and not cyclic(preflight)
    return {"name": "capacity_wait_after_provisional_announcement", "unsafe_cycle": unsafe,
            "preflight_dependencies": preflight,
            "obligation": "reserve capacity before read blockers, or abort; metadata cannot await blocked data"}


def run():
    histories = (overwrite_and_rmw() + no_write_abort_delete() + partial_and_late_installation()
                 + application_dependencies() + index_shadowing())
    return {"scope": "Finite semantic audit, not a transport or performance model",
            "histories": histories, "capacity": capacity_cycle(),
            "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "admission_sha256": hashlib.sha256(Path(__file__).with_name("write_admission.py").read_bytes()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = run()
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"histories": len(result["histories"]),
                      "expected_invalid": sum(not history["expected_valid"] for history in result["histories"]),
                      "capacity_cycle_and_preflight_control": "checked"}, indent=2))


if __name__ == "__main__":
    main()
