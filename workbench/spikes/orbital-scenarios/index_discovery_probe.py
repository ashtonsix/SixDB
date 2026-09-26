#!/usr/bin/env python3
"""Authored histories for UPDATE v=v+1 with a maintained secondary index.

The primary row target never changes. The index deletes entry(v) and inserts
entry(v+1), so a discovery pass does not establish stable complete output
coverage. Fresh execution under predicted gates must reject an uncovered entry.
All observations, returned values and complete committed effects are replayed.
Partial physical installation may temporarily differ from the committed index
invariant; a query spanning that state must wait on the unresolved promises.

Separate counter-probes retain write-only transaction gates after definitively
resolving an aborted attempt's promises, then try (never wait for) extra gates.
This is a bounded possible repair with additional state, not the default policy
or a general progress claim for dynamically changing outputs.

There is no scheduler, throughput estimate, full SQL engine or recovery model.
Index slots are a finite declared universe solely for these authored histories.
"""

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path

from fixed_execution import FixedStore
from write_admission import Admission


ROW = "eu/row/1"
INDEX = tuple(f"us/index/entry/{value}" for value in range(8))
ALL = "us/index/all"
DISJOINT = "eu/unrelated/1"


def outputs(value, indexed):
    return ({ROW, INDEX[value], INDEX[value + 1]} if indexed else {ROW})


def predicates(value):
    # Exact query membership changes at these equality predicates and the
    # threshold between the old and new values, independent of page layout.
    return {f"index:eq:{value}", f"index:eq:{value + 1}", f"index:ge:{value + 1}"}


def program(program, state, indexed):
    if program["op"] == "disjoint_increment":
        value = state[DISJOINT]
        return {DISJOINT: value + 1}, {"old_value": value, "new_value": value + 1}
    value = state[ROW]
    if program["op"] == "discover":
        return {}, {"value": value, "outputs": sorted(outputs(value, indexed)),
                    "affected_predicates": sorted(predicates(value)) if indexed else []}
    if program["op"] == "increment":
        writes = {ROW: value + 1}
        if indexed:
            writes.update({INDEX[value]: None, INDEX[value + 1]: 1})
        return writes, {"old_value": value, "new_value": value + 1}
    assert program["op"] == "query"
    result = {"row_value": value}
    if indexed:
        wanted = program["value"]
        result.update(equal_lookup=[1] if state[INDEX[wanted]] == 1 else [],
                      index_values=[number for number, key in enumerate(INDEX) if state[key] == 1])
    return {}, result


class Probe:
    def __init__(self, indexed, with_disjoint=False):
        self.indexed = indexed
        initial = {ROW: 0}
        if indexed:
            initial.update({key: 1 if number == 0 else None for number, key in enumerate(INDEX)})
        if with_disjoint:
            initial[DISJOINT] = 0
        self.initial = initial
        self.scopes = {key: (key,) for key in initial}
        if indexed:
            self.scopes[ALL] = INDEX
            self.scopes.update({f"us/index/eq/{number}": (key,) for number, key in enumerate(INDEX)})
            self.scopes.update({f"us/index/ge/{number}": INDEX[number:]
                                for number in range(len(INDEX))})
        self.store = FixedStore(initial, self.scopes)
        self.admission = Admission({})
        self.clock = 0
        self.identities = Counter()
        self.counts = Counter()
        self.events = []
        self.records = []
        self.retained = {}
        self.repair_events = []

    def identity(self, logical):
        self.identities[logical] += 1
        self.clock += 10
        return f"{logical}#{self.identities[logical]}"

    @staticmethod
    def groups(keys):
        groups = defaultdict(list)
        for key in keys:
            groups[key.split("/", 1)[0]].append(key)
        return [tuple(sorted(groups[owner])) for owner in sorted(groups)]

    def invariant(self, state):
        if self.indexed:
            assert [number for number, key in enumerate(INDEX) if state[key] == 1] == [state[ROW]]
            assert all(state[key] in (None, 1) for key in INDEX)

    def record(self, logical, attempt, description, writes, result):
        self.records.append({"logical": logical, "owner": attempt.owner, "position": attempt.c,
                             "program": description, "reads": dict(attempt.reads),
                             "writes": dict(writes), "result": result})

    def finish_read(self, logical, attempt, description):
        observed = {key: value for values in attempt.reads.values() for key, value in values.items()}
        writes, result = program(description, observed, self.indexed)
        assert not writes
        self.store.choose(attempt)
        self.store.decide(attempt, True)
        self.record(logical, attempt, description, writes, result)
        self.clock = max(self.clock, attempt.c[0])
        return result

    def discover(self, logical):
        owner = self.identity(logical)
        attempt = self.store.begin(owner, (self.clock, owner))
        assert self.store.capture(attempt, ROW, wait=True)
        result = self.finish_read(logical, attempt, {"op": "discover"})
        self.counts["discovery_passes"] += 1
        return result

    def begin_query(self, logical, value):
        owner = self.identity(logical)
        attempt = self.store.begin(owner, (self.clock, owner))
        assert self.store.capture(attempt, ROW, wait=True)
        return logical, attempt, {"op": "query", "value": value}

    def finish_query(self, query):
        logical, attempt, description = query
        if self.indexed:
            for scope in (f'us/index/eq/{description["value"]}', ALL):
                if not self.store.capture(attempt, scope, wait=True):
                    self.counts["query_waits"] += 1
                    return None
        result = self.finish_read(logical, attempt, description)
        if self.indexed:
            assert result["index_values"] == [result["row_value"]]
            assert result["equal_lookup"] == ([1] if description["value"] == result["row_value"] else [])
        return result

    def query(self, logical, value):
        result = self.finish_query(self.begin_query(logical, value))
        assert result is not None
        return result

    def update(self, logical, discovery, during_install=None, *, retain_on_miss=False):
        proposed = set(discovery["outputs"])
        assert ROW in proposed
        self.admission.priority.setdefault(logical, (len(self.admission.priority), logical))
        groups = self.groups(proposed)
        for keys in groups:
            assert self.admission.request(logical, keys), "this history acquires after the predecessor finishes"
        owner = self.identity(logical)
        attempt = self.store.begin_fixed(owner, (self.clock, owner), proposed,
                                         self.admission.grants, logical)
        for keys in groups:
            assert self.store.reserve_position(attempt, keys)
        self.store.fix_position(attempt)
        for keys in groups:
            assert self.store.publish_position(attempt, keys)
        self.clock = max(self.clock, attempt.c[0])
        assert self.store.capture(attempt, ROW, wait=True)
        value = attempt.reads[ROW][ROW]
        if self.indexed:
            assert self.store.capture(attempt, INDEX[value], wait=True)
            assert attempt.reads[INDEX[value]][INDEX[value]] == 1
        description = {"op": "increment"}
        writes, result = program(description, {ROW: value}, self.indexed)
        attempt.writes.update(writes)
        missing = set(writes) - proposed
        self.counts["fresh_evaluations"] += 1
        if missing:
            before = self.store.head()
            try:
                self.store.seal_values(attempt)
            except AssertionError as error:
                assert "new outputs require a restart" in str(error)
            else:
                raise AssertionError("unreserved index mutation passed the coverage guard")
            assert owner not in self.store.sealed
            self.store.decide(attempt, False)
            self.store.release(attempt, proposed)
            assert attempt.decision == "abort"
            assert not any(claim.owner == owner for claim in self.store.claims.values())
            assert all(not promise.remaining for promise in self.store.promises[owner])
            if retain_on_miss:
                assert all(self.admission.grants[key] == logical for key in proposed)
                self.retained[logical] = {"old_owner": owner, "old_position": attempt.c,
                                          "value": value, "outputs": proposed,
                                          "needed": set(writes)}
                self.counts["retained_gate_cycles"] += 1
                self.counts["retained_gate_keys"] += len(proposed)
                self.repair_events.append({"event": "abort_resolved_gates_retained", "logical": logical,
                                           "aborted_owner": owner, "aborted_position": attempt.c,
                                           "held_outputs": sorted(proposed), "remaining_promises": 0})
            else:
                self.admission.release_all(logical)
            assert self.store.head() == before
            self.invariant(before)
            self.counts["manifest_misses"] += 1
            self.events.append({"event": "manifest_miss", "logical": logical,
                                "primary_target": ROW, "discovered_value": discovery["value"],
                                "fresh_value": value, "predicted_outputs": sorted(proposed),
                                "actual_outputs": sorted(writes), "missing_outputs": sorted(missing),
                                "missing_predicates": sorted(predicates(value) -
                                                             set(discovery["affected_predicates"]))})
            return None
        self.store.seal_values(attempt)
        self.store.decide(attempt, True)
        for number, keys in enumerate(groups):
            self.store.install(attempt, keys)
            if number == 0 and during_install is not None:
                during_install()
        self.admission.release_all(logical)
        self.record(logical, attempt, description, writes, result)
        self.counts["committed_updates"] += 1
        self.invariant(self.store.head())
        return result

    def expand_after_miss(self, logical):
        """One no-wait expansion attempt, after every old promise is resolved.

        Admission.request retains a denied request for its normal waiting API;
        this wrapper immediately cancels it and releases ALL transaction gates.
        It never retries a denied additional group while retaining other gates.
        """
        held = self.retained.pop(logical)
        assert self.store.attempts[held["old_owner"]].decision == "abort"
        assert not any(claim.owner == held["old_owner"] for claim in self.store.claims.values())
        assert all(self.admission.grants[key] == logical for key in held["outputs"])
        additional = held["needed"] - held["outputs"]
        assert additional
        self.counts["expansion_attempts"] += 1
        for keys in self.groups(additional):
            if not self.admission.request(logical, keys):
                self.admission.release_all(logical)
                assert logical not in self.admission.waiting
                assert logical not in self.admission.grants.values()
                self.counts["denied_expansions"] += 1
                self.counts["release_all_restarts"] += 1
                self.repair_events.append({"event": "expansion_denied_release_all", "logical": logical,
                                           "denied_group": list(keys), "remaining_gates": 0,
                                           "queued_extra_request": False})
                return None
        expanded = held["outputs"] | additional
        self.counts["expanded_output_keys"] += len(additional)
        self.counts["repair_reruns"] += 1
        self.counts["extra_retained_coverage_keys"] += len(expanded - held["needed"])
        self.repair_events.append({"event": "expanded_before_fresh_attempt", "logical": logical,
                                   "held_outputs": sorted(expanded), "new_outputs": sorted(additional),
                                   "unused_old_outputs": sorted(expanded - held["needed"])})
        # Coverage is a conservative union, while reads and values are executed
        # afresh at a NEW position. No old observation or old position is reused.
        result = self.update(logical, {"value": held["value"], "outputs": sorted(expanded),
                                       "affected_predicates": sorted(predicates(held["value"]))},
                             retain_on_miss=True)
        if result is not None:
            committed = self.records[-1]
            assert committed["owner"] != held["old_owner"]
            assert committed["position"] > held["old_position"]
            self.repair_events.append({"event": "fresh_attempt_committed", "logical": logical,
                                       "owner": committed["owner"], "position": committed["position"],
                                       "fresh_result": result})
        return result

    def disjoint_update(self, logical):
        self.admission.priority.setdefault(logical, (len(self.admission.priority), logical))
        assert self.admission.request(logical, (DISJOINT,))
        owner = self.identity(logical)
        attempt = self.store.begin_fixed(owner, (self.clock, owner), {DISJOINT},
                                         self.admission.grants, logical)
        assert self.store.reserve_position(attempt, (DISJOINT,))
        self.store.fix_position(attempt)
        assert self.store.publish_position(attempt, (DISJOINT,))
        assert self.store.capture(attempt, DISJOINT, wait=True)
        description = {"op": "disjoint_increment"}
        writes, result = program(description, attempt.reads[DISJOINT], self.indexed)
        attempt.writes.update(writes)
        self.store.seal_values(attempt)
        self.store.decide(attempt, True)
        self.store.install(attempt, (DISJOINT,))
        self.admission.release_all(logical)
        self.record(logical, attempt, description, writes, result)
        self.clock = max(self.clock, attempt.c[0])
        self.counts["disjoint_committed_updates"] += 1
        return result

    def fresh_update(self, logical):
        discovery = self.discover("discover-" + logical)
        result = self.update(logical, discovery)
        assert result is not None
        return result

    def summary(self, name, full=False, **detail):
        state = dict(self.initial)
        ordered = sorted(self.records, key=lambda record: record["position"])
        for record in ordered:
            for scope, observed in record["reads"].items():
                assert observed == {key: state[key] for key in self.scopes[scope]}
            writes, result = program(record["program"], state, self.indexed)
            assert record["writes"] == writes and record["result"] == result
            state.update(writes)
            self.invariant(state)
        assert state == self.store.head() == self.store.check_serial()
        assert not self.store.claims and not self.admission.grants and not self.admission.waiting
        assert not self.retained
        updates = [record for record in ordered if record["program"]["op"] == "increment"]
        assert state[ROW] == len(updates)
        result = {"name": name, "indexed": self.indexed, "serial_check": True,
                  "returned_results_check": True, "index_invariant_check": self.indexed,
                  "counts": dict(self.counts), "manifest_misses": self.events,
                  "update_results": [{"logical": record["logical"], **record["result"]} for record in updates],
                  "query_results": [record["result"] for record in ordered if record["program"]["op"] == "query"],
                  "final": state, **detail}
        if full:
            result["committed_history"] = ordered
        if self.repair_events:
            result["repair_events"] = self.repair_events
        return result


def stale_prediction(indexed, full=False):
    probe = Probe(indexed)
    predicted = probe.discover("discover-T")
    assert predicted["value"] == 0
    assert probe.fresh_update("preceding-U") == {"old_value": 0, "new_value": 1}
    result = probe.update("T", predicted)
    if indexed:
        assert result is None
        assert probe.query("after-miss-old", 0)["equal_lookup"] == []
        assert probe.query("after-miss-current", 1)["equal_lookup"] == [1]
        assert probe.query("after-miss-unwritten", 2)["equal_lookup"] == []
        result = probe.update("T", probe.discover("rediscover-T"))
    assert result == {"old_value": 1, "new_value": 2}
    assert probe.query("final-query", 2)["row_value"] == 2
    return probe.summary("indexed_stale_prediction" if indexed else "primary_only_stale_prediction", full)


def queued_discoveries(full=False):
    probe = Probe(True)
    predicted = {logical: probe.discover("discover-" + logical) for logical in ("A", "B", "C")}
    assert all(discovery["value"] == 0 for discovery in predicted.values())
    assert probe.update("A", predicted["A"]) == {"old_value": 0, "new_value": 1}
    assert probe.update("B", predicted["B"]) is None
    assert probe.update("C", predicted["C"]) is None
    for logical in ("B", "C"):
        assert probe.update(logical, probe.discover("rediscover-" + logical)) is not None
    for value in range(4):
        assert probe.query("query-" + str(value), value)["equal_lookup"] == ([1] if value == 3 else [])
    return probe.summary("several_discoveries_of_v0", full)


def repeated_misses(full=False):
    probe = Probe(True)
    for number in range(4):
        predicted = probe.discover(f"discover-T-{number}")
        assert predicted["value"] == number
        probe.fresh_update(f"preceding-U-{number}")
        assert probe.update("T", predicted) is None
        assert probe.query(f"query-current-{number}", number + 1)["equal_lookup"] == [1]
    prefix = {"T_commits": sum(record["logical"] == "T" for record in probe.records),
              "manifest_misses": probe.counts["manifest_misses"], "row_value": probe.store.head()[ROW]}
    assert prefix == {"T_commits": 0, "manifest_misses": 4, "row_value": 4}
    # Conditional success after interference stops; not a progress guarantee.
    assert probe.update("T", probe.discover("discover-T-quiet")) == {"old_value": 4, "new_value": 5}
    assert probe.query("query-final", 5)["equal_lookup"] == [1]
    return probe.summary("repeated_pre_admission_changes", full, before_quiet_round=prefix,
                         four_attempt_budget_outcome="failure")


def partial_install(full=False):
    probe = Probe(True)
    waiting = []
    def between_participants():
        # The row is installed while both index-entry promises remain pending.
        assert probe.store.head()[ROW] == 1 and probe.store.head()[INDEX[0]] == 1
        query = probe.begin_query("concurrent-point-index-query", 1)
        assert probe.finish_query(query) is None
        waiting.append(query)
    assert probe.update("T", probe.discover("discover-T"), between_participants) is not None
    assert probe.finish_query(waiting[0]) == {"row_value": 1, "equal_lookup": [1], "index_values": [1]}
    return probe.summary("query_cannot_observe_partial_index_install", full)


def retained_gate_expansion(full=False):
    probe = Probe(True, with_disjoint=True)
    predicted = probe.discover("discover-T")
    probe.fresh_update("preceding-U")
    assert probe.update("T", predicted, retain_on_miss=True) is None
    assert set(probe.admission.grants) == outputs(0, True)
    assert set(probe.admission.grants.values()) == {"T"}
    assert not probe.store.claims

    # Gates alone do not block a coherent report. No replacement fixed-position
    # promise exists yet; the failed attempt has definitively aborted everywhere.
    gap = probe.query("report-between-attempts", 1)
    assert gap == {"row_value": 1, "equal_lookup": [1], "index_values": [1]}
    assert probe.query("unwritten-index-between-attempts", 2)["equal_lookup"] == []
    assert probe.disjoint_update("disjoint-during-retention") == {"old_value": 0, "new_value": 1}
    assert probe.admission.grants[ROW] == "T" and not probe.store.claims
    probe.repair_events.append({"event": "gap_read_and_disjoint_writer_completed",
                                "report": gap, "disjoint_value": probe.store.head()[DISJOINT],
                                "retained_primary_gate": True, "read_blocking_promises": 0})

    # The primary gate excludes another writer changing v. A new fixed attempt
    # therefore re-evaluates v=1 and settles after expanding by exactly entry(2).
    assert probe.expand_after_miss("T") == {"old_value": 1, "new_value": 2}
    assert probe.counts["repair_reruns"] == probe.counts["expanded_output_keys"] == 1
    assert probe.counts["extra_retained_coverage_keys"] == 1  # old entry(0)
    assert probe.query("repair-final", 2) == {
        "row_value": 2, "equal_lookup": [1], "index_values": [2]}
    return probe.summary("retained_gates_no_wait_expansion_settles", full,
                         strategy="retain transaction gates; resolve old promises; try extra gates; rerun",
                         logical_T_fixed_evaluations=2, logical_T_discoveries=1,
                         fresh_output_key_count=3, conservative_coverage_key_count=4,
                         gap_report_completed=True, disjoint_writer_completed=True)


def denied_retained_expansion(full=False):
    probe = Probe(True)
    predicted = probe.discover("discover-T")
    probe.fresh_update("preceding-U")
    assert probe.update("T", predicted, retain_on_miss=True) is None

    # A rival has only write-only preparation gates, with no fixed position or
    # data effects. This is a resource-level counter-probe, not an extra SQL
    # increment with an invented output manifest. If both preparations waited
    # for late outputs, T -> entry(2) and rival -> ROW could form a wait cycle.
    rival = "rival-preparation"
    probe.admission.priority[rival] = (len(probe.admission.priority), rival)
    assert probe.admission.request(rival, (INDEX[2],))
    assert probe.admission.grants[ROW] == "T"
    assert probe.expand_after_miss("T") is None
    assert probe.admission.grants == {INDEX[2]: rival}
    assert not probe.admission.waiting and not probe.store.claims

    # The rival's additional lower-order key succeeds immediately after T's
    # release; neither preparation ever waits while growing outside the initial
    # canonical order. Cancel this authored preparation without data effects.
    assert probe.admission.request(rival, (ROW,))
    probe.admission.release_all(rival)
    probe.repair_events.append({"event": "rival_acquired_released_primary_then_cancelled",
                                "rival_extra_acquisition_waited": False,
                                "potential_wait_cycle": ["T -> rival entry(2)", "rival -> T primary"],
                                "rival_data_effects": {}})
    assert probe.query("after-denied-expansion", 1) == {
        "row_value": 1, "equal_lookup": [1], "index_values": [1]}
    assert probe.update("T", probe.discover("rediscover-T")) == {"old_value": 1, "new_value": 2}
    assert probe.query("denied-then-restarted-final", 2)["equal_lookup"] == [1]
    assert probe.counts["denied_expansions"] == probe.counts["release_all_restarts"] == 1
    return probe.summary("denied_retained_expansion_releases_all", full,
                         strategy="retain transaction gates; resolve old promises; deny extra gates; release all",
                         logical_T_fixed_evaluations=2, logical_T_discoveries=2,
                         denied_extra_request_left_waiting=False,
                         rival_acquired_released_primary=True)


def run_probes(full=False):
    root = Path(__file__).resolve().parent
    physical = {ROW, "us/index/packed-page"}
    predicted, actual = predicates(0), predicates(1)
    assert actual - predicted == {"index:eq:2", "index:ge:2"}
    return {
        "source_sha256": {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
                          for name in ("index_discovery_probe.py", "fixed_execution.py",
                                       "certification.py", "write_admission.py")},
        "application": "Increment one fixed primary row; delete its old secondary-index entry and insert its new entry; return old/new values.",
        "assumptions": "Authored sequential histories with immediate failure-free gates/control delivery; a separate partial-install query probe; finite index-key universe; no timing model.",
        "scenarios": [stale_prediction(False, full), stale_prediction(True, full),
                      queued_discoveries(full), repeated_misses(full), partial_install(full),
                      retained_gate_expansion(full), denied_retained_expansion(full)],
        "packed_storage_scope_observation": {
            "physical_outputs_before_and_after": sorted(physical),
            "predicted_predicate_effects": sorted(predicted), "actual_predicate_effects": sorted(actual),
            "missing_predicate_effects": sorted(actual - predicted),
            "meaning": "Packing entries into one stable physical page does not make exact equality/range read-evidence coverage value-independent. A safe implementation must conservatively cover possible affected scopes before fixing the position, or detect/restart on changed scope coverage; page layout alone is not that proof.",
        },
        "limits": "A stable primary key is not a stable complete SQL output footprint. The core rejects unreserved entry mutation. Quiet-round success does not establish progress while preceding writers keep changing the discovered index keys. This does not model a full index, uniqueness constraints, new index pages, recovery or epoch confluence.",
        "retained_gate_repair_boundary": {
            "status": "Distinct bounded counter-probe, not a recommendation or change to the strict-release policy.",
            "mechanism": "Definitively abort and resolve every old fixed-position promise while retaining transaction-level write-only gates. Try additional groups without waiting; on any denial cancel the request and release every gate. On success use conservative expanded coverage in a distinct fresh fixed-position attempt and rerun all reads and computation.",
            "conditional_success": "For this same-primary indexed increment, the retained primary gate stabilizes the only value that determines outputs. One successful expansion settles. A report and disjoint writer complete while old promises are resolved and replacement promises do not yet exist.",
            "added_cost": "Transaction-level gate ownership outlives attempt ownership. Expanded coverage keeps unused old outputs; the example holds four keys for three actual writes. Definitive abort acknowledgements, cancellation of in-flight extra acquisition, cleanup of all retained gates, and recovery across the gap are required by a distributed implementation but are not modeled here. The program executes again at a new position.",
            "no_general_progress_claim": "Ungated source values or a moving winner can keep changing output and affected read-evidence scopes. Expansion can repeatedly fail or grow conservatively; the retained primary key does not stabilize arbitrary SQL source data. No bounded completion guarantee follows.",
        },
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--full", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = run_probes(args.full)
    encoded = json.dumps(result, sort_keys=True, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded)
        print(f"Wrote {len(result['scenarios'])} index discovery probes to {args.output}")
    else:
        print(encoded, end="")
