#!/usr/bin/env python3
"""Authored logical histories for data-dependent select-max-then-update.

The application selects the greatest (score, key), subtracts 100 from that
row's score, and returns its key and old score. Its output identity is computed
from data, not supplied to the transaction in advance. Narrow admission uses a
separate speculative discovery query, then acquires that proposed output and
reevaluates the complete application at a fresh snapshot. A different output
requires release and restart; it cannot expand a held admission in place.

These are deterministic semantic probes with logical counters, not a scheduler,
latency comparison, SQL implementation, or distributed progress proof.
"""

import argparse
from collections import Counter
import hashlib
from itertools import permutations
import json
from pathlib import Path

from certification import Store
from write_admission import Admission


ROWS = ("eu/a", "eu/b", "eu/c")
SCOPE = "eu/items"
INITIAL = {"eu/a": 10, "eu/b": 9, "eu/c": 0, "us/outside": 0}
SCOPES = {**{key: (key,) for key in INITIAL}, SCOPE: ROWS}
STRATEGIES = ("optimistic", "narrow-rediscovery", "broad-admission")


def required_scopes(program):
    return ((SCOPE,) if program["op"] in ("take", "discover", "report")
            else tuple(program.get("read_keys", ())))


def evaluate(program, values):
    if program["op"] in ("take", "discover"):
        winner = max(ROWS, key=lambda key: (values[key], key))
        result = {"selected": winner, "old_score": values[winner]}
        writes = {winner: values[winner] - 100} if program["op"] == "take" else {}
        return writes, result
    if program["op"] == "report":
        return {}, {key: values[key] for key in ROWS}
    assert program["op"] == "point"
    return ({program["key"]: program["value"]},
            {key: values[key] for key in program.get("read_keys", ())})


def replay(records):
    """Replay complete declared observations, computed writes and return values."""
    state = dict(INITIAL)
    for record in records:
        assert set(record["reads"]) == set(required_scopes(record["program"]))
        for scope, observed in record["reads"].items():
            assert observed == {key: state[key] for key in SCOPES[scope]}
        writes, result = evaluate(record["program"], state)
        assert record["writes"] == writes
        assert record["result"] == result
        state.update(writes)
    return state


def witnesses(records, before=()):
    found = []
    for order in permutations(records):
        ids = [record["owner"] for record in order]
        if any(ids.index(left) >= ids.index(right) for left, right in before):
            continue
        try:
            replay(order)
        except AssertionError:
            continue
        found.append(ids)
    return found


class Probe:
    def __init__(self, strategy):
        self.strategy = strategy
        self.store = Store(INITIAL, SCOPES)
        self.admission = Admission({"T": (0, "T")})
        self.clock = 0
        self.identities = Counter()
        self.counts = Counter()
        self.events = []
        self.committed = []

    def admit(self, logical, keys):
        if self.strategy == "optimistic":
            return True
        if logical not in self.admission.priority:
            self.admission.priority[logical] = (len(self.admission.priority), logical)
        granted = self.admission.request(logical, keys)
        self.events.append({"event": "admission", "logical": logical,
                            "keys": list(keys), "granted": granted})
        self.counts["peak_admitted_keys"] = max(
            self.counts["peak_admitted_keys"], len(self.admission.grants))
        self.counts["peak_application_admitted_keys"] = max(
            self.counts["peak_application_admitted_keys"],
            sum(holder == "T" for holder in self.admission.grants.values()))
        if not granted:
            self.counts["blocked_admission_requests"] += 1
        return granted

    def release(self, logical):
        if logical in self.admission.priority:
            self.admission.release_all(logical)

    def draft(self, logical, program):
        self.clock += 10
        self.identities[logical] += 1
        owner = f"{logical}#{self.identities[logical]}"
        attempt = self.store.begin(owner, (self.clock, owner))
        for scope in required_scopes(program):
            assert self.store.capture(attempt, scope)
        values = {key: value for observed in attempt.reads.values()
                  for key, value in observed.items()}
        writes, result = evaluate(program, values)
        if program["op"] == "discover":
            self.counts["discovery_passes"] += 1
        elif program["op"] == "take":
            self.counts["application_evaluations"] += 1
        self.events.append({"event": "evaluate", "owner": owner,
                            "op": program["op"], "result": result,
                            "actual_outputs": sorted(writes)})
        return {"logical": logical, "attempt": attempt, "program": program,
                "writes": writes, "result": result}

    @staticmethod
    def record(draft):
        attempt = draft["attempt"]
        return {"owner": attempt.owner, "logical": draft["logical"],
                "position": attempt.c or attempt.s,
                "program": dict(draft["program"]),
                "reads": {scope: dict(values) for scope, values in attempt.reads.items()},
                "writes": dict(draft["writes"]), "result": dict(draft["result"])}

    def abort(self, draft, reason):
        attempt = draft["attempt"]
        self.store.decide(attempt, False)
        self.store.release(attempt, tuple(attempt.writes))
        self.release(draft["logical"])
        self.counts["aborted_application_attempts"] += draft["program"]["op"] == "take"
        self.events.append({"event": "abort", "owner": attempt.owner, "reason": reason})

    def finish(self, draft):
        attempt = draft["attempt"]
        if self.strategy != "optimistic":
            assert all(self.admission.grants.get(key) == draft["logical"]
                       for key in draft["writes"])
        attempt.writes.update(draft["writes"])
        if attempt.writes and not self.store.promise(attempt, tuple(attempt.writes)):
            self.abort(draft, attempt.rejection_reason)
            return False
        self.store.choose(attempt)
        self.clock = max(self.clock, attempt.c[0])
        if attempt.c > attempt.s:
            for scope in attempt.reads:
                if not self.store.renew(attempt, scope):
                    self.abort(draft, attempt.rejection_reason)
                    return False
        self.store.decide(attempt, True)
        self.store.install(attempt, tuple(attempt.writes))
        self.release(draft["logical"])
        self.committed.append(self.record(draft))
        self.counts["committed_" + draft["program"]["op"]] += 1
        self.events.append({"event": "commit", "owner": attempt.owner,
                            "position": attempt.c})
        return True

    def discover(self, logical):
        draft = self.draft(logical, {"op": "discover"})
        assert self.finish(draft)
        return draft["result"]["selected"]

    def point(self, logical, key, value, read_keys=()):
        if not self.admit(logical, (key,)):
            return False
        draft = self.draft(logical, {"op": "point", "key": key,
                                    "value": value, "read_keys": tuple(read_keys)})
        assert self.finish(draft)
        return True

    def reevaluate(self, proposed):
        assert self.admission.grants.get(proposed) == "T"
        draft = self.draft("T", {"op": "take"})
        if set(draft["writes"]) != {proposed}:
            self.counts["manifest_restarts"] += 1
            self.events.append({"event": "manifest_changed", "proposed": proposed,
                                "actual": sorted(draft["writes"])})
            self.abort(draft, "output_manifest_changed")
            return None
        return draft

    def summary(self, name, full=False, **detail):
        ordered = sorted(self.committed, key=lambda record: record["position"])
        assert replay(ordered) == self.store.head()
        assert self.store.check_serial() == self.store.head()
        assert not self.store.claims
        assert not self.admission.grants and not self.admission.waiting
        result = {"name": name, "strategy": self.strategy, "serial_check": True,
                  "counts": dict(self.counts),
                  "captured_rows": self.store.counters["captured_rows"],
                  "application_results": [record["result"] for record in self.committed
                                          if record["program"]["op"] == "take"],
                  "physical_commit_order": [record["owner"] for record in self.committed],
                  "serial_order": [record["owner"] for record in ordered],
                  "final": self.store.head(), **detail}
        if full:
            result.update(events=self.events, committed_history=self.committed)
        return result


def moving_winner(strategy, backedge=False, full=False):
    probe = Probe(strategy)
    extra = {}
    read_keys = ("eu/a",) if backedge else ()
    if strategy == "optimistic":
        original = probe.draft("T", {"op": "take"})
        assert original["result"]["selected"] == "eu/a"
        assert probe.point("U", "eu/b", 11, read_keys)
        competitor = probe.committed[-1]
        proposed_history = [probe.record(original), competitor]
        possible = witnesses(proposed_history)
        extra["original_observations_serializable"] = bool(possible)
        if backedge:
            assert not possible
            assert not probe.finish(original)
            assert original["attempt"].rejection_reason == "source_changed"
            retry = probe.draft("T", {"op": "take"})
            assert retry["result"]["selected"] == "eu/b"
            assert probe.finish(retry)
        else:
            assert possible == [[original["attempt"].owner, competitor["owner"]]]
            assert not witnesses(proposed_history,
                                 before=((competitor["owner"], original["attempt"].owner),))
            extra["old_max_valid_if_competitor_must_precede_T"] = False
            assert probe.finish(original)
            assert original["attempt"].c == original["attempt"].s
    elif strategy == "narrow-rediscovery":
        proposed = probe.discover("D1")
        assert proposed == "eu/a"
        assert probe.point("U", "eu/b", 11, read_keys)
        assert probe.admit("T", (proposed,))
        assert probe.reevaluate(proposed) is None
        proposed = probe.discover("D2")
        assert proposed == "eu/b"
        assert probe.admit("T", (proposed,))
        retry = probe.reevaluate(proposed)
        assert retry is not None and probe.finish(retry)
    else:
        assert strategy == "broad-admission"
        assert probe.admit("T", ROWS)
        original = probe.draft("T", {"op": "take"})
        assert not probe.point("U", "eu/b", 11, read_keys)
        assert probe.finish(original)
        assert probe.point("U", "eu/b", 11, read_keys)
        if backedge:
            # The writer could not execute its old-value read before T, so it
            # observes the changed row rather than creating the dependency cycle.
            assert probe.committed[-1]["result"] == {"eu/a": -90}
    return probe.summary("moving_winner_backedge" if backedge else "moving_winner",
                         full, **extra)


def repeated_rediscovery(full=False):
    probe = Probe("narrow-rediscovery")
    for index in range(4):
        proposed = probe.discover(f"D{index + 1}")
        other = "eu/b" if proposed == "eu/a" else "eu/a"
        assert probe.point(f"U{index + 1}", other, probe.store.head()[proposed] + 1)
        assert probe.admit("T", (proposed,))
        assert probe.reevaluate(proposed) is None
    prefix = {"moves": 4, "manifest_restarts": probe.counts["manifest_restarts"],
              "committed_application_updates": probe.counts["committed_take"]}
    assert prefix == {"moves": 4, "manifest_restarts": 4,
                      "committed_application_updates": 0}
    # This additional quiet round is conditional success, not a progress promise
    # under continued motion. A four-round budget would have returned failure.
    proposed = probe.discover("D5")
    assert probe.admit("T", (proposed,))
    retry = probe.reevaluate(proposed)
    assert retry is not None and probe.finish(retry)
    return probe.summary("repeated_moves_then_quiet", full,
                         before_quiet_round=prefix,
                         four_round_budget_outcome="failure")


def admission_coverage(strategy, full=False):
    probe = Probe(strategy)
    proposed = None
    if strategy == "narrow-rediscovery":
        proposed = probe.discover("D1")
        assert probe.admit("T", (proposed,))
    elif strategy == "broad-admission":
        assert probe.admit("T", ROWS)
    # c is a possible output in the conservative domain, but this update keeps
    # it well below a. The actual application still changes only a.
    loser_progressed = probe.point("loser", "eu/c", 1)
    assert loser_progressed == (strategy != "broad-admission")
    assert probe.point("outside", "us/outside", 1)
    report = probe.draft("reader", {"op": "report"})
    assert probe.finish(report)  # Admission itself creates no read protection.
    application = (probe.reevaluate(proposed) if proposed is not None
                   else probe.draft("T", {"op": "take"}))
    assert application is not None and set(application["writes"]) == {"eu/a"}
    assert probe.finish(application)
    if not loser_progressed:
        assert probe.point("loser", "eu/c", 1)
    assert probe.store.head() == {"eu/a": -90, "eu/b": 9, "eu/c": 1, "us/outside": 1}
    return probe.summary("conservative_output_coverage", full,
                         loser_write_progressed_before_T=loser_progressed,
                         outside_write_progressed_before_T=True,
                         read_only_report_completed_while_T_admitted=(strategy != "optimistic"),
                         actual_application_write_keys=["eu/a"])


def run_probes(full=False):
    results = [moving_winner(strategy, backedge, full)
               for backedge in (False, True) for strategy in STRATEGIES]
    results.append(repeated_rediscovery(full))
    results.extend(admission_coverage(strategy, full) for strategy in STRATEGIES)
    root = Path(__file__).resolve().parent
    return {
        "source_sha256": {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
                          for name in ("discovery_probe.py", "certification.py", "write_admission.py")},
        "assumptions": "Authored logical histories; integer scores; one fixed collection; "
                       "whole-participant admission; failure-free immediate decisions/install; no time model.",
        "application": "Select max(score,key); subtract100 from that selected row; return key and old score.",
        "limitations": [
            "An old maximum is permitted only when the complete history can place T before the competing change.",
            "The explicit backedge makes the optimistic stale attempt unserializable and forces a restart.",
            "Narrow discovery is speculative extra work, not an earlier application read silently moved to a fresh snapshot.",
            "Narrow rediscovery has no guaranteed completion under continued winner changes.",
            "Broad admission excludes competing writes from a conservative domain larger than the actual output.",
            "Read admission is absent, but later certification write promises can still delay readers.",
            "This does not test dynamic indexes, absence, multishard discovery, recovery or epoch confluence.",
        ],
        "scenarios": results,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--full", action="store_true", help="include events and complete committed observations")
    arguments = parser.parse_args()
    print(json.dumps(run_probes(arguments.full), sort_keys=True, indent=2))
