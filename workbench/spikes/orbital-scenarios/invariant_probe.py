#!/usr/bin/env python3
"""Conditional SQL histories under write-only admission and certification.

Two actors can hold disjoint output admissions while their decisions depend on
both actors' data. These histories check write skew and a global quota, then
contrast fresh inventory evaluation after a shared-output gate with retaining
a stale pre-admission result. A business rejection is a committed application
result; an aborted speculative result must never be returned as acceptance.

The schedules are authored logical transitions with immediate delivery, not a
latency model, exhaustive interleaving proof, or implementation of SQL/epochs.
"""

import argparse
from collections import Counter
import hashlib
from itertools import permutations
import json
from pathlib import Path

from certification import Store
from write_admission import Admission


def workload(kind):
    if kind == "on_call":
        initial = {"eu/alice": 1, "us/bob": 1}
        targets = {"A": "eu/alice", "B": "us/bob"}
    elif kind == "quota":
        initial = {"eu/allocated": 4, "us/allocated": 4}
        targets = {"A": "eu/allocated", "B": "us/allocated"}
    else:
        assert kind == "inventory"
        initial = {"eu/stock": 3}
        targets = {"A": "eu/stock", "B": "eu/stock"}
    programs = {actor: {"kind": kind, "target": target,
                        "read_keys": tuple(initial), "amount": 2, "limit": 10}
                for actor, target in targets.items()}
    return initial, programs


def evaluate(program, values):
    target = program["target"]
    if program["kind"] == "on_call":
        total = sum(values[key] for key in program["read_keys"])
        accepted = values[target] == 1 and total > 1
        return ({target: 0} if accepted else {},
                {"accepted": accepted, "on_call_after": total - int(accepted),
                 "reason": "off_call" if accepted else "must_keep_one_on_call"})
    if program["kind"] == "quota":
        total = sum(values[key] for key in program["read_keys"])
        accepted = total + program["amount"] <= program["limit"]
        allocated = program["amount"] if accepted else 0
        return ({target: values[target] + allocated} if accepted else {},
                {"accepted": accepted, "allocated": allocated,
                 "total_after": total + allocated,
                 "reason": "allocated" if accepted else "quota_exceeded"})
    assert program["kind"] == "inventory"
    accepted = values[target] >= program["amount"]
    reserved = program["amount"] if accepted else 0
    remaining = values[target] - reserved
    return ({target: remaining} if accepted else {},
            {"accepted": accepted, "reserved": reserved, "remaining": remaining,
             "reason": "reserved" if accepted else "insufficient_stock"})


def assert_invariant(kind, initial, state, committed):
    """Include accepted business outcomes, not merely the stored numbers."""
    if kind == "on_call":
        assert all(value in (0, 1) for value in state.values())
        assert sum(state.values()) >= 1
        accepted = sum(record["result"]["accepted"] for record in committed)
        assert sum(initial.values()) - sum(state.values()) == accepted
    elif kind == "quota":
        assert all(value >= 0 for value in state.values())
        assert sum(state.values()) <= 10
        allocated = sum(record["result"]["allocated"] for record in committed)
        assert sum(state.values()) - sum(initial.values()) == allocated
    else:
        assert kind == "inventory" and state["eu/stock"] >= 0
        reserved = sum(record["result"]["reserved"] for record in committed)
        assert initial["eu/stock"] - state["eu/stock"] == reserved


def replay(kind, initial, records):
    """Independent application replay, including every observed and returned value."""
    state = dict(initial)
    prefix = []
    assert_invariant(kind, initial, state, prefix)
    for record in records:
        program = record["program"]
        assert set(record["reads"]) == set(program["read_keys"])
        for key, observed in record["reads"].items():
            assert observed == {key: state[key]}
        writes, result = evaluate(program, state)
        assert writes == record["writes"]
        assert result == record["result"]
        if not result["accepted"]:
            assert not writes
        state.update(writes)
        prefix.append(record)
        assert_invariant(kind, initial, state, prefix)
    return state


def serial_witnesses(kind, initial, records):
    found = []
    for order in permutations(records):
        try:
            replay(kind, initial, order)
        except AssertionError:
            continue
        found.append([record["owner"] for record in order])
    return found


class History:
    def __init__(self, kind):
        self.kind = kind
        self.initial, self.programs = workload(kind)
        self.scopes = {key: (key,) for key in self.initial}
        self.store = Store(self.initial, self.scopes)
        self.admission = Admission({"A": (0, "A"), "B": (1, "B")})
        self.clock = 0
        self.identities = Counter()
        self.counts = Counter()
        self.committed = []
        self.events = []

    def admit(self, actor):
        key = self.programs[actor]["target"]
        granted = self.admission.request(actor, (key,))
        self.counts["peak_admitted_keys"] = max(
            self.counts["peak_admitted_keys"], len(self.admission.grants))
        if not granted:
            self.counts["admission_waits"] += 1
        self.events.append({"event": "admission", "actor": actor,
                            "output": key, "granted": granted})
        return granted

    def draft(self, actor):
        # The stale-inventory negative control deliberately calls this before
        # admission. Correct ordered-write execution admits first, then calls it.
        self.identities[actor] += 1
        self.clock += 10
        owner = f"{actor}#{self.identities[actor]}"
        attempt = self.store.begin(owner, (self.clock, owner))
        program = self.programs[actor]
        for key in program["read_keys"]:
            assert self.store.capture(attempt, key)
        values = {key: value for observed in attempt.reads.values()
                  for key, value in observed.items()}
        writes, result = evaluate(program, values)
        self.counts["evaluations"] += 1
        self.events.append({"event": "private_result", "owner": owner,
                            "result": result, "returned_to_client": False})
        return {"actor": actor, "attempt": attempt, "program": program,
                "writes": writes, "result": result}

    @staticmethod
    def record(draft):
        attempt = draft["attempt"]
        return {"actor": draft["actor"], "owner": attempt.owner,
                "position": attempt.c or attempt.s,
                "program": dict(draft["program"]),
                "reads": {key: dict(values) for key, values in attempt.reads.items()},
                "writes": dict(draft["writes"]), "result": dict(draft["result"])}

    def finish(self, draft):
        actor, attempt = draft["actor"], draft["attempt"]
        assert self.admission.grants.get(draft["program"]["target"]) == actor
        attempt.writes.update(draft["writes"])
        if attempt.writes and not self.store.promise(attempt, tuple(attempt.writes)):
            return self.abort(draft)
        self.store.choose(attempt)
        self.clock = max(self.clock, attempt.c[0])
        if attempt.c > attempt.s:
            for key in attempt.reads:
                if not self.store.renew(attempt, key):
                    return self.abort(draft)
        self.store.decide(attempt, True)
        self.store.install(attempt, tuple(attempt.writes))
        self.admission.release_all(actor)
        self.committed.append(self.record(draft))
        self.counts["committed_transactions"] += 1
        self.counts["accepted" if draft["result"]["accepted"] else "business_rejections"] += 1
        self.events.append({"event": "commit", "owner": attempt.owner,
                            "position": attempt.c, "result": draft["result"]})
        return True

    def abort(self, draft):
        attempt = draft["attempt"]
        assert attempt.rejection_reason is not None
        self.store.decide(attempt, False)
        self.store.release(attempt, tuple(attempt.writes))
        self.admission.release_all(draft["actor"])
        self.counts["serialization_aborts"] += 1
        self.events.append({"event": "abort", "owner": attempt.owner,
                            "reason": attempt.rejection_reason,
                            "discarded_private_result": draft["result"]})
        return False

    def summary(self, name, full=False, **detail):
        records = sorted(self.committed, key=lambda record: record["position"])
        assert len(records) == 2 and {record["actor"] for record in records} == {"A", "B"}
        assert replay(self.kind, self.initial, records) == self.store.head()
        assert self.store.check_serial() == self.store.head()
        assert not self.admission.grants and not self.admission.waiting
        assert not self.store.claims
        assert self.counts["accepted"] == 1 and self.counts["business_rejections"] == 1
        result = {"name": name, "workload": self.kind, "serial_check": True,
                  "application_invariant_check": True,
                  "counts": dict(self.counts), "final": self.store.head(),
                  "committed_results": {record["actor"]: record["result"] for record in records},
                  "serial_order": [record["owner"] for record in records], **detail}
        if full:
            result.update(committed_history=self.committed, events=self.events)
        return result


def disjoint_outputs(kind, capture_order, finish_order, full=False):
    history = History(kind)
    assert history.admit("A") and history.admit("B")
    assert len(history.admission.grants) == 2
    drafts = {actor: history.draft(actor) for actor in capture_order}
    assert all(draft["result"]["accepted"] for draft in drafts.values())
    stale_records = [history.record(drafts[actor]) for actor in ("A", "B")]
    assert not serial_witnesses(kind, history.initial, stale_records)
    unsafe = dict(history.initial)
    for record in stale_records:
        unsafe.update(record["writes"])
    try:
        assert_invariant(kind, history.initial, unsafe, stale_records)
    except AssertionError:
        pass
    else:
        raise AssertionError("accepting both stale decisions must violate the application invariant")
    first, second = finish_order
    assert history.finish(drafts[first])
    assert not history.finish(drafts[second])
    assert drafts[second]["attempt"].rejection_reason == "source_changed"
    assert history.admit(second)
    retry = history.draft(second)
    assert not retry["result"]["accepted"] and not retry["writes"]
    assert history.finish(retry)
    assert history.counts["serialization_aborts"] == 1
    return history.summary("disjoint_outputs_share_a_read_invariant", full,
                           capture_order=list(capture_order), finish_order=list(finish_order),
                           both_stale_acceptances_serializable=False,
                           first_accepted_actor=first,
                           fresh_business_rejection_actor=second)


def fresh_inventory_gate(full=False):
    history = History("inventory")
    assert history.admit("A")
    first = history.draft("A")
    assert not history.admit("B")
    assert history.identities["B"] == 0  # No early snapshot or speculative acceptance.
    assert history.finish(first)
    assert history.admit("B")
    second = history.draft("B")
    assert second["result"] == {"accepted": False, "reserved": 0, "remaining": 1,
                                "reason": "insufficient_stock"}
    assert history.finish(second)
    assert history.counts["serialization_aborts"] == 0
    return history.summary("same_output_gate_then_fresh_inventory_evaluation", full,
                           business_rejection_is_committed=True)


def stale_inventory_result(full=False):
    history = History("inventory")
    # A deliberately retained proposal from before B owns the gate. Admission
    # acquired later cannot turn this old "accepted" result into a fresh one.
    stale = history.draft("B")
    assert history.admit("A")
    first = history.draft("A")
    assert not history.admit("B")
    stale_records = [history.record(first), history.record(stale)]
    assert not serial_witnesses("inventory", history.initial, stale_records)
    unsafe = dict(history.initial)
    for record in stale_records:
        unsafe.update(record["writes"])
    assert unsafe["eu/stock"] == 1  # Nonnegative stored stock hides the double acceptance.
    assert sum(record["result"]["reserved"] for record in stale_records) == 4 > 3
    assert history.finish(first)
    assert history.admit("B")
    assert not history.finish(stale)
    assert stale["attempt"].rejection_reason == "source_changed"
    assert history.admit("B")
    fresh = history.draft("B")
    assert not fresh["result"]["accepted"] and not fresh["writes"]
    assert history.finish(fresh)
    assert history.counts["serialization_aborts"] == 1
    return history.summary("same_output_gate_cannot_adopt_stale_inventory_acceptance", full,
                           both_stale_acceptances_serializable=False,
                           unsafe_stored_stock_would_still_be_nonnegative=True,
                           unsafe_accepted_units=4, initial_stock=3,
                           fresh_business_rejection_is_committed=True)


def run_probes(full=False):
    orders = tuple(permutations(("A", "B")))
    results = [disjoint_outputs(kind, captures, finishes, full)
               for kind in ("on_call", "quota")
               for captures in orders for finishes in orders]
    results.extend((fresh_inventory_gate(full), stale_inventory_result(full)))
    root = Path(__file__).resolve().parent
    return {
        "source_sha256": {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
                          for name in ("invariant_probe.py", "certification.py", "write_admission.py")},
        "assumptions": "Authored logical schedules; point scopes across two shards; complete known "
                       "potential outputs; failure-free immediate delivery; no latency model.",
        "schedule_coverage": "Two complete capture orders by two finish orders for each disjoint-output invariant; "
                             "two authored inventory histories. This is not exhaustive message interleaving.",
        "limitations": [
            "Output admission serializes conflicting output keys, not predicates over disjoint outputs.",
            "Certification still rejects invalidated read dependencies; a fresh retry can commit a business rejection.",
            "The stale inventory history is a negative control: correct ordered admission evaluates only after acquiring its gate.",
            "Accepted inventory results must agree with stock accounting; nonnegative stored stock alone is insufficient.",
            "These programs are conditional operations, not unconditional associative increments.",
            "No dynamic discovery, predicate indexing, recovery, fairness guarantee or epoch-confluence proof is provided.",
        ],
        "scenarios": results,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--full", action="store_true", help="include complete histories and private versus committed results")
    arguments = parser.parse_args()
    print(json.dumps(run_probes(arguments.full), sort_keys=True, indent=2))
