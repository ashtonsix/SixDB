#!/usr/bin/env python3
"""Committed-only MV timestamp-order rule with atomic point-key certification.

Explicit tombstones model known absent keys. General predicates, distributed
certification, failures, resource costs and epoch scheduling are not implemented.
Every step is an authored agreed event, NOT a replica-local permitted schedule.
"""

import argparse
from copy import deepcopy
from dataclasses import dataclass, field
import hashlib
import json
from pathlib import Path


@dataclass
class Version:
    wts: int
    value: object
    rts: int = 0


@dataclass
class Transaction:
    name: str
    ts: int
    program: list
    cursor: int = 0
    observed: list = field(default_factory=list)
    bindings: dict = field(default_factory=dict)
    writes: dict = field(default_factory=dict)
    status: str = "pending"
    blockers: list = field(default_factory=list)
    prepared: set = field(default_factory=set)


def evaluate(expression, bindings):
    return expression(bindings) if callable(expression) else expression


class Model:
    def __init__(self, initial, owners=None):
        self.initial = deepcopy(initial)
        self.versions = {key: [Version(0, value)] for key, value in initial.items()}
        self.transactions = []
        self.trace = []
        self.claims = {}
        self.owners = owners or dict.fromkeys(initial, "local")
        assert self.owners.keys() == initial.keys()

    def transaction(self, name, ts, program):
        assert ts > 0 and all(tx.ts != ts for tx in self.transactions)
        tx = Transaction(name, ts, program)
        self.transactions.append(tx)
        return tx

    def predecessor(self, key, ts):
        # Missing keys must be declared tombstones, never silently ignored.
        return max((v for v in self.versions[key] if v.wts <= ts), key=lambda v: v.wts)

    def conflicts(self, tx, keys):
        conflicts = []
        for key in keys:
            predecessor = self.predecessor(key, tx.ts)
            if key in self.claims and self.claims[key] is not tx:
                conflicts.append({"key": key, "claim": self.claims[key].name})
            elif predecessor.rts > tx.ts:
                conflicts.append({"key": key, "predecessor": predecessor.wts,
                                  "reader": predecessor.rts})
        return conflicts

    def release(self, tx):
        for key in list(self.claims):
            if self.claims[key] is tx:
                del self.claims[key]

    def prepare(self, tx, keys):
        # One participant's explicit promise. Rejection fails the whole attempt.
        # Release is atomic here: delayed messages/recovery are not simulated.
        assert tx.status == "pending" and tx.cursor == len(tx.program)
        assert set(keys) <= tx.writes.keys()
        assert len({self.owners[key] for key in keys}) == 1
        owner = self.owners[keys[0]]
        assert set(keys) == {key for key in tx.writes if self.owners[key] == owner}
        tx.blockers = self.conflicts(tx, keys)
        if tx.blockers:
            tx.status = "aborted"
            self.release(tx)
            self.trace.append({"tx": tx.name, "op": "prepare_rejected", "blockers": tx.blockers})
            return False
        for key in keys:
            self.claims[key] = tx
            tx.prepared.add(key)
        self.trace.append({"tx": tx.name, "op": "prepared", "keys": list(keys)})
        return True

    def step(self, tx):
        assert tx.status == "pending"
        if tx.cursor == len(tx.program):
            assert not tx.prepared or tx.prepared == tx.writes.keys()
            tx.blockers = self.conflicts(tx, tx.writes)
            if tx.blockers:
                tx.status = "aborted"
            else:
                # One indivisible all-write check/install. Not distributed 2PC.
                for key, value in tx.writes.items():
                    self.versions[key].append(Version(tx.ts, value, tx.ts))
                tx.status = "committed"
            self.release(tx)
            self.trace.append({"tx": tx.name, "op": tx.status, "blockers": tx.blockers})
            return True
        action, key, *args = tx.program[tx.cursor]
        assert key in self.versions
        if action == "read":
            if key in tx.writes:
                value = tx.writes[key]
            else:
                version = self.predecessor(key, tx.ts)
                holder = self.claims.get(key)
                if holder and version.wts < holder.ts <= tx.ts:
                    self.trace.append({"tx": tx.name, "op": "read_wait", "key": key, "for": holder.name})
                    return False
                value = version.value
                version.rts = max(version.rts, tx.ts)
            tx.observed.append([key, value])
            tx.bindings[key] = value
            self.trace.append({"tx": tx.name, "op": "read", "key": key, "value": value})
        else:
            assert action == "write"
            tx.writes[key] = evaluate(args[0], tx.bindings)
            self.trace.append({"tx": tx.name, "op": "buffer", "key": key})
        tx.cursor += 1
        return True

    def run(self, tx):
        while tx.status == "pending":
            assert self.step(tx), f"authored schedule tried to run blocked {tx.name}"

    def check_serial(self):
        state = deepcopy(self.initial)
        for tx in sorted(self.transactions, key=lambda tx: tx.ts):
            if tx.status != "committed":
                continue
            bindings, observed = {}, []
            for action, key, *args in tx.program:
                if action == "read":
                    bindings[key] = state[key]
                    observed.append([key, state[key]])
                else:
                    state[key] = evaluate(args[0], bindings)
            assert observed == tx.observed, (tx.name, observed, tx.observed)
        head = {key: max(vs, key=lambda v: v.wts).value for key, vs in self.versions.items()}
        assert state == head, (state, head)
        return head

    def result(self, name, expected):
        actual = {tx.name: tx.status for tx in self.transactions}
        assert actual == expected, (name, actual, expected)
        return {"name": name, "outcomes": actual, "final": self.check_serial(),
                "transactions": [{"name": tx.name, "ts": tx.ts,
                                  "observed": tx.observed, "blockers": tx.blockers}
                                 for tx in self.transactions], "trace": self.trace}


def read(key):
    return ("read", key)


def write(key, value):
    return ("write", key, value)


def increment(key):
    return [read(key), write(key, lambda values: values[key] + 1)]


def cases():
    db = Model({"a": 100, "b": None, "out": None})
    t = db.transaction("scan", 10, [read("a"), read("b"), write("out", lambda d: max(v for v in (d['a'], d['b']) if v is not None))])
    db.step(t); db.step(t)
    w = db.transaction("new_max", 20, [write("b", 101)])
    db.run(w); db.run(t)
    yield db.result("old_max_and_new_insert_both_commit", {"scan": "committed", "new_max": "committed"})

    db = Model({"a": 100, "b": None, "out": None})
    t = db.transaction("scan", 10, [read("a"), read("b"), write("out", lambda d: d['a'])])
    db.step(t); db.step(t)
    w = db.transaction("marker_then_insert", 20, [read("out"), write("b", 101)])
    db.run(w); db.run(t)
    yield db.result("back_edge_rejects_old_output", {"scan": "aborted", "marker_then_insert": "committed"})

    db = Model({"x": 0, "y": 0, "z": 0})
    t = db.transaction("broad_update", 10, [read(k) for k in ('x', 'y', 'z')] +
                       [write(k, lambda d, key=k: d[key] + 10) for k in ('x', 'y', 'z')])
    for _ in range(3): db.step(t)
    w = db.transaction("point_update", 20, increment("y"))
    db.run(w); db.run(t)
    yield db.result("one_conflict_rejects_whole_broad_update", {"broad_update": "aborted", "point_update": "committed"})

    db = Model({"x": 0, "y": 0})
    t = db.transaction("old_blind_batch", 10, [write("x", 4), write("y", 4)])
    w = db.transaction("new_blind_point", 20, [write("x", 9)])
    db.run(w); db.run(t)
    yield db.result("blind_batch_can_complete_out_of_timestamp_order", {"old_blind_batch": "committed", "new_blind_point": "committed"})

    db = Model({"x": 0})
    r = db.transaction("new_reader", 30, [read("x")]); db.run(r)
    for ts in (10, 20): db.run(db.transaction(f"old_writer_{ts}", ts, [write("x", ts)]))
    yield db.result("intermediate_version_cannot_hide_reader", {"new_reader": "committed", "old_writer_10": "aborted", "old_writer_20": "aborted"})

    db = Model({"x": 0})
    db.run(db.transaction("writer25", 25, [write("x", 25)]))
    db.run(db.transaction("reader30", 30, [read("x")]))
    db.run(db.transaction("writer20", 20, [write("x", 20)]))
    yield db.result("successor_reader_does_not_block_older_insertion", {"writer25": "committed", "reader30": "committed", "writer20": "committed"})

    db = Model({"absent": None})
    db.run(db.transaction("absence_reader", 30, [read("absent")]))
    db.run(db.transaction("old_insert", 20, [write("absent", 1)]))
    yield db.result("explicit_absence_rejects_backdated_insert", {"absence_reader": "committed", "old_insert": "aborted"})

    db = Model({"x": 0, "y": 0})
    db.run(db.transaction("self", 10, [write("x", 4), read("x"), write("y", lambda d: d['x'] + 1)]))
    yield db.result("read_own_buffered_write", {"self": "committed"})

    db = Model({"x": 0, "y": 0})
    db.run(db.transaction("reader40", 40, [read("y")]))
    db.run(db.transaction("aborted_reader30", 30, [read("x"), write("y", 1)]))
    db.run(db.transaction("writer20", 20, [write("x", 1)]))
    yield db.result("aborted_reader_evidence_can_cause_false_abort", {"reader40": "committed", "aborted_reader30": "aborted", "writer20": "aborted"})

    db = Model({"x": 0, "y": 0})
    expected = {}
    for attempt in range(3):
        t = db.transaction(f"broad_attempt{attempt}", 10 + attempt * 20,
                           [read("x"), read("y"), write("x", lambda d: d['x'] + 10), write("y", lambda d: d['y'] + 10)])
        db.step(t); db.step(t)
        w = db.transaction(f"point{attempt}", 20 + attempt * 20, increment("x"))
        db.run(w); db.run(t)
        expected[t.name] = "aborted"; expected[w.name] = "committed"
    yield db.result("fresh_attempts_can_keep_losing", expected)

    db = Model({"remote_input": 7, "x": 0, "unrelated": 0})
    t = db.transaction("wan", 10, [read("remote_input"), write("x", lambda d: d['remote_input'])])
    db.step(t)
    expected = {"wan": "committed"}
    for index in range(100):
        local = db.transaction(f"local{index}", 20 + index, increment("unrelated"))
        db.run(local); expected[local.name] = "committed"
    assert t.status == "pending"
    db.run(t)
    yield db.result("100_unrelated_completions_while_wan_pending", expected)

    db = Model({"x": 0, "remote_target": 0, "y": 0},
               {"x": "local", "remote_target": "remote", "y": "local"})
    t = db.transaction("wan_writer", 10, [write("x", 10), write("remote_target", 10)])
    db.step(t); db.step(t)
    assert db.prepare(t, ["x"])
    u = db.transaction("bridge", 20, [write("x", 20), write("y", 20)])
    db.step(u); db.step(u)
    assert not db.prepare(u, ["y", "x"])
    assert "y" not in db.claims
    v = db.transaction("local_y", 30, increment("y")); db.run(v)
    assert t.status == "pending" and v.status == "committed"
    assert db.prepare(t, ["remote_target"])
    db.run(t)
    yield db.result("rejected_bridge_does_not_reserve_y", {"wan_writer": "committed", "bridge": "aborted", "local_y": "committed"})

    db = Model({"x": 0, "y": 0})
    t = db.transaction("prepared_writer", 10, [write("x", 10)])
    db.step(t); assert db.prepare(t, ["x"])
    newer = db.transaction("new_reader", 20, [read("x")])
    assert not db.step(newer)
    older = db.transaction("old_reader", 5, [read("x")]); db.run(older)
    unrelated = db.transaction("unrelated", 30, increment("y")); db.run(unrelated)
    db.run(t); db.run(newer)
    yield db.result("prepared_write_wait_is_key_and_timestamp_scoped", {"prepared_writer": "committed", "new_reader": "committed", "old_reader": "committed", "unrelated": "committed"})

    db = Model({"x": 0, "y": 0, "z": 0}, {"x": "remote", "y": "local", "z": "local"})
    t = db.transaction("holder", 10, [write("x", 10)])
    db.step(t); assert db.prepare(t, ["x"])
    u = db.transaction("cross_shard_bridge", 20, [write("x", 20), write("y", 20)])
    db.step(u); db.step(u); assert db.prepare(u, ["y"])
    v = db.transaction("local_y", 30, increment("y"))
    assert not db.step(v)
    z = db.transaction("unrelated_z", 40, increment("z")); db.run(z)
    assert not db.prepare(u, ["x"])
    db.run(v); db.run(t)
    yield db.result("partial_write_promises_still_delay_their_keys", {"holder": "committed", "cross_shard_bridge": "aborted", "local_y": "committed", "unrelated_z": "committed"})

    for read_first in (False, True):
        db = Model({"x": 0})
        w = db.transaction("writer10", 10, [write("x", 1)])
        r = db.transaction("reader20", 20, [read("x")])
        for tx in ((r, w) if read_first else (w, r)): db.run(tx)
        yield db.result(f"schedule_counterexample_read_first_{read_first}",
                        {"writer10": "aborted" if read_first else "committed", "reader20": "committed"})


def interleavings():
    initial = Model({"x": 0, "y": 0, "z": 0})
    initial.transaction("a", 10, [read("x"), write("y", lambda d: d['x'] + 1)])
    initial.transaction("b", 20, [read("y"), write("z", lambda d: d['y'] + 1)])
    initial.transaction("c", 30, [read("z"), write("x", lambda d: d['z'] + 1)])
    counts = {}

    def visit(db):
        active = [index for index, tx in enumerate(db.transactions) if tx.status == 'pending']
        if not active:
            db.check_serial()
            mask = ','.join(tx.status for tx in db.transactions)
            counts[mask] = counts.get(mask, 0) + 1
            return
        for index in active:
            candidate = deepcopy(db)
            assert candidate.step(candidate.transactions[index])
            visit(candidate)

    visit(initial)
    assert sum(counts.values()) == 1680
    return {"authored_event_interleavings": sum(counts.values()), "outcome_counts": dict(sorted(counts.items())),
            "scope": "Three point-key programs forming x->y->z->x; check committed observations and final state against timestamp serial order. Not a confluence test or distributed proof."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--full", action="store_true", help="Include every event and transaction.")
    args = parser.parse_args()
    results = list(cases())
    assert results[-1]['outcomes'] != results[-2]['outcomes']
    result = {"model": __doc__, "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), "cases": results, "interleavings": interleavings()}
    if not args.full:
        result['cases'] = [{"name": case['name'], "final": case['final'],
                            "committed": sum(outcome == 'committed' for outcome in case['outcomes'].values()),
                            "aborted": sum(outcome == 'aborted' for outcome in case['outcomes'].values()),
                            "sample_transactions": case['transactions'][:3],
                            "wait_events": [event for event in case['trace'] if event['op'] == 'read_wait']}
                           for case in results]
    encoded = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded)
        print(f"Checked {len(results)} traces and 1,680 point-key interleavings against timestamp serial order; wrote {args.output}")
    else:
        print(encoded, end='')


if __name__ == '__main__':
    main()
