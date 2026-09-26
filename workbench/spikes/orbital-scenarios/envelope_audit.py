#!/usr/bin/env python3
"""Finite adversarial histories for conservative logical mutation envelopes.

This independent model separates row gates from derived-index evidence. It has
no network, recovery, epoch scheduler, costs or dynamically created authorities.
Negative controls deliberately omit an obligation. Predicate-safety controls
must fail serial replay; constraint and gate controls use separate oracles.
Positions are distinct authored integers, not clock times.
"""

import argparse
from dataclasses import dataclass
import hashlib
from itertools import permutations, product
import json
from pathlib import Path

from write_admission import Admission


class CurrentGroupFIFO(Admission):
    """Finite adapter: each authority orders only its current requested groups.

    Insertion order denotes agreed local request order, never physical arrival.
    Different authorities' queued groups are disjoint, so their interleaving in
    this dictionary is immaterial. No global transaction age is required here.
    """

    def request(self, owner, keys):
        self.priority = {other: (index, other)
                         for index, other in enumerate(self.waiting)}
        self.priority.setdefault(owner, (len(self.waiting), owner))
        return super().request(owner, keys)


@dataclass
class Pending:
    owner: str
    rows: frozenset[str]
    position: int
    authorities: frozenset[str]
    # None means any index value. Exact alternatives are a comparison control.
    values: frozenset[int] | None = None


class LogicalIndex:
    """Row ownership plus a finite, independently replayed secondary index.

    Every possible index destination needs advance metadata coverage. Read
    evidence is deliberately authority-wide: precise summaries are not modeled.
    Unknown row values conservatively affect every predicate in covered domains.
    """

    def __init__(self, initial, authorities=("index-even", "index-odd")):
        self.initial = dict(initial)
        self.authorities = tuple(authorities)
        self.read_bounds = dict.fromkeys(self.authorities, 0)
        self.pending = {}
        self.commits = []
        self.observations = []
        self.events = []

    @staticmethod
    def destination(value):
        return "index-odd" if value % 2 else "index-even"

    def begin(self, owner, rows, floor, *, authorities=None, honor_bounds=True,
              values=None):
        rows = frozenset(rows)
        assert rows and owner not in self.pending
        assert all(not rows.intersection(p.rows) for p in self.pending.values())
        covered = frozenset(self.authorities if authorities is None else authorities)
        position = max([floor, *(self.read_bounds[a] + 1 for a in covered)]) \
            if honor_bounds else floor
        assert position not in {p.position for p in self.pending.values()}
        assert position not in {p for p, _, _ in self.commits}
        self.pending[owner] = Pending(owner, rows, position, covered,
                                      None if values is None else frozenset(values))
        self.events.append({"event": "announce", "owner": owner,
                            "position": position, "rows": sorted(rows),
                            "authorities": sorted(covered),
                            "exclusive_row_count": len(rows),
                            "predicate_authority_count": len(covered)})
        return position

    def state_at(self, position, exclude_owner=None):
        state = dict(self.initial)
        for commit_position, owner, writes in sorted(self.commits):
            if commit_position <= position and owner != exclude_owner:
                state.update(writes)
        return state

    def lookup(self, owner, position, value, overlay=None):
        authority = self.destination(value)
        self.read_bounds[authority] = max(self.read_bounds[authority], position)
        blockers = sorted(p.owner for p in self.pending.values()
                          if p.owner != owner and p.position <= position
                          and authority in p.authorities
                          and (p.values is None or value in p.values))
        if blockers:
            self.events.append({"event": "wait", "owner": owner,
                                "position": position, "value": value,
                                "blockers": blockers})
            return None
        state = self.state_at(position, owner)
        state.update(overlay or {})
        rows = sorted(row for row, stored in state.items()
                      if stored == value)
        self.observations.append({"owner": owner, "position": position,
                                  "value": value, "rows": rows,
                                  "overlay": dict(overlay or {})})
        self.events.append({"event": "lookup", **self.observations[-1]})
        return rows

    def finish(self, owner, writes):
        pending = self.pending[owner]
        assert writes.keys() <= pending.rows, "mutation escaped the declared envelope"
        del self.pending[owner]
        self.commits.append((pending.position, owner, dict(writes)))
        self.events.append({"event": "resolve", "owner": owner,
                            "position": pending.position, "writes": dict(writes)})

    def replay_errors(self):
        errors = []
        for observation in self.observations:
            state = self.state_at(observation["position"], observation["owner"])
            state.update(observation["overlay"])
            expected = sorted(row for row, value in state.items()
                              if value == observation["value"])
            if expected != observation["rows"]:
                errors.append({"observation": observation, "expected": expected})
        return errors

    def result(self, name, *, expect_valid=True, **detail):
        assert not self.pending, "history must resolve every attempt"
        errors = self.replay_errors()
        assert bool(errors) != expect_valid, (name, errors)
        return {"name": name, "serial_valid": not errors,
                "expected_serial_valid": expect_valid, **detail,
                "replay_errors": errors, "events": self.events}


def index_histories():
    results = []
    # A row's new value is discovered only after the querying shard has replied.
    for covered in (False, True):
        db = LogicalIndex({"row/a": 0})
        db.begin("T", ["row/a"], 10,
                 authorities=None if covered else ["index-even"])
        first = db.lookup("Q", 20, 7)
        assert first == (None if covered else [])
        db.finish("T", {"row/a": 7})
        if covered:
            assert db.lookup("Q", 20, 7) == ["row/a"]
        results.append(db.result("unknown_destination_" + ("covered" if covered else "omitted"),
                                 expect_valid=covered))

    # A pending blocker cannot repair observations already completed before it.
    for honored in (False, True):
        db = LogicalIndex({"row/a": 0})
        assert db.lookup("Q", 20, 7) == []
        position = db.begin("T", ["row/a"], 10, honor_bounds=honored)
        assert position == (21 if honored else 10)
        db.finish("T", {"row/a": 7})
        results.append(db.result("predicate_read_bound_" + ("honored" if honored else "omitted"),
                                 expect_valid=honored, writer_position=position))

    # Same final writes: an unknown future index value delays a disjoint query.
    for unknown in (False, True):
        db = LogicalIndex({"row/a": 0, "row/b": 2})
        db.begin("WAN", ["row/a"], 10, values=None if unknown else [0, 2],
                 authorities=None if unknown else ["index-even"])
        query = db.lookup("local_query", 20, 7)
        assert query == (None if unknown else [])
        # A disjoint primary writer is admitted despite the index-wide promise.
        db.begin("local_writer", ["row/b"], 30)
        db.finish("local_writer", {"row/b": 4})
        db.finish("WAN", {"row/a": 2})
        if unknown:
            assert db.lookup("local_query", 20, 7) == []
        results.append(db.result("predicate_locality_" + ("unknown" if unknown else "exact"),
                                 unnecessary_query_wait=unknown,
                                 exclusive_row_count=1,
                                 predicate_authority_count=2 if unknown else 1,
                                 disjoint_primary_writer_completed_first=True))

    # Uniqueness depends on predicate evidence, despite disjoint row ownership.
    for first_owner in ("A", "B"):
        for visit in permutations(("A", "B")):
            db = LogicalIndex({"row/a": 0, "row/b": 2})
            second_owner = "B" if first_owner == "A" else "A"
            positions = {first_owner: 10, second_owner: 20}
            for owner in ("A", "B"):
                db.begin(owner, ["row/" + owner.lower()], positions[owner])
            captures = {}
            for owner in visit:
                captures[owner] = db.lookup(owner, positions[owner], 7)
            assert captures[first_owner] == []
            assert captures[second_owner] is None
            db.finish(first_owner, {"row/" + first_owner.lower(): 7})
            assert db.lookup(second_owner, positions[second_owner], 7) == [
                "row/" + first_owner.lower()]
            db.finish(second_owner, {})
            assert list(db.state_at(100).values()).count(7) == 1
            results.append(db.result(f"unique_{first_owner}_first_visit_{''.join(visit)}",
                                     accepted=first_owner, business_rejected=second_owner))
    return results


def own_constraint_histories():
    results = []
    for overlay in (False, True):
        db = LogicalIndex({"row/a": 0, "row/b": 2})
        position = db.begin("T", ["row/a", "row/b"], 10)
        assert db.lookup("T", position, 7) == []
        tentative = {"row/a": 7}
        matches = db.lookup("T", position, 7, overlay=tentative if overlay else None)
        assert matches == (["row/a"] if overlay else [])
        accepted = not matches
        db.finish("T", {"row/a": 7, "row/b": 7} if accepted else {})
        unique = len(set(db.state_at(100).values())) == len(db.initial)
        assert unique == overlay
        results.append(db.result("own_unique_effects_" + ("visible" if overlay else "omitted"),
                                 constraint_valid=unique, expected_constraint_valid=overlay,
                                 accepted=accepted))
    return results


def gate_histories():
    results = []
    # Finite logical slots include absent IDs; row/range descriptions normalize
    # to the same conflict domain. This is no interval-tree implementation.
    tenant_range = tuple(f"eu/tenant/{i}" for i in range(4))
    gates = Admission({"wide": (0, "wide"), "insert": (1, "insert"),
                       "outside": (2, "outside")})
    assert gates.request("wide", tenant_range)
    assert not gates.request("insert", ["eu/tenant/2"])
    assert gates.request("outside", ["eu/other/2"])
    gates.release_all("wide")
    assert gates.request("insert", ["eu/tenant/2"])
    results.append({"name": "range_owns_absent_identifier", "insert_waited": True,
                    "outside_writer_passed": True})

    for future_claim in (False, True):
        gates = Admission({"old": (0, "old"), "young": (1, "young")})
        assert gates.request("young", ["eu/a"])
        assert not gates.request("old", ["eu/a"])
        if future_claim:
            # Deliberately invalid policy: advertise an unrequested future group
            # for fairness. A canonical caller order alone cannot repair it.
            gates.waiting["old"] = frozenset(["eu/a", "us/b"])
        granted = gates.request("young", ["us/b"])
        assert granted != future_claim
        if granted:
            gates.release_all("young")
            assert gates.request("old", ["eu/a"])
            assert gates.request("old", ["us/b"])
        results.append({"name": "future_fairness_claim_" + ("unsafe" if future_claim else "absent"),
                        "deadlocked_before_execution": future_claim})

    gates = Admission({"WAN": (0, "WAN"), "wide": (1, "wide"),
                       "point": (2, "point")})
    assert gates.request("WAN", ["eu/x"])
    assert not gates.request("wide", ["eu/x", "eu/y"])
    assert "eu/y" not in gates.grants
    assert not gates.request("point", ["eu/y"])
    gates.release_all("WAN")
    assert gates.request("wide", ["eu/x", "eu/y"])
    gates.release_all("wide")
    assert gates.request("point", ["eu/y"])
    results.append({"name": "fair_waiter_spreads_WAN_delay", "point_key_was_unheld": True,
                    "wait_chain": ["point(y)", "wide(x,y)", "WAN(x)"]})
    return results


def admission_state_search():
    """Explore all enabled actor steps in finite overlapping-range fixtures.

    Completed admission always enables release: this check isolates gate order,
    never establishes progress of data programs, failures or infinite arrivals.
    Waiting sets matter for stable-age fairness and are part of state identity.
    """
    fixtures = {
        "overlapping_ranges": ((("eu/0", "eu/1"), ("us/0",)),
                               (("eu/1", "eu/2"), ("us/1",)),
                               (("eu/2",), ("us/0", "us/1"))),
        "cross_participant_bridge": ((("eu/0",), ("us/0", "us/1")),
                                     (("eu/0", "eu/1"), ("us/1",)),
                                     (("eu/1",), ("us/0",))),
        "broad_and_absent_slots": ((("eu/0", "eu/1", "eu/2"),),
                                   (("eu/1",), ("us/0",)),
                                   (("eu/2",), ("us/1",))),
    }
    results = []
    for name, groups in fixtures.items():
        count = 0
        transitions = 0
        for queue_policy, age_order in [("stable_age", order) for order in permutations(range(3))] + [
                ("local_fifo", (0, 1, 2))]:
            rank = {str(actor): (age_order.index(actor), str(actor)) for actor in range(3)}
            start = ((0, 0, 0), (), ())
            pending, visited = [start], set()
            while pending:
                state = pending.pop()
                if state in visited:
                    continue
                visited.add(state)
                steps, grants, waiters = state
                successors = set()
                for actor in range(3):
                    index = steps[actor]
                    if index > len(groups[actor]):
                        continue
                    gate = Admission(rank) if queue_policy == "stable_age" else CurrentGroupFIFO({})
                    gate.grants = dict(grants)
                    gate.waiting = {owner: frozenset(keys) for owner, keys in waiters}
                    next_steps = list(steps)
                    if index == len(groups[actor]):
                        gate.release_all(str(actor))
                        next_steps[actor] += 1
                    elif gate.request(str(actor), groups[actor][index]):
                        next_steps[actor] += 1
                    queued = tuple((owner, tuple(sorted(keys))) for owner, keys in gate.waiting.items())
                    successor = (tuple(next_steps), tuple(sorted(gate.grants.items())),
                                 tuple(sorted(queued)) if queue_policy == "stable_age" else queued)
                    if successor != state:
                        successors.add(successor)
                if not successors:
                    assert all(steps[a] > len(groups[a]) for a in range(3)), (name, state)
                transitions += len(successors)
                pending.extend(successors - visited)
            count += len(visited)
        results.append({"name": name, "age_orders": 6, "local_fifo_searches": 1, "states": count,
                        "transitions": transitions, "incomplete_terminal_states": 0})
    return results


def fixed_source_histories():
    # Reuse the actual candidate core for late source discovery. The source's
    # promise position, not when its shard is discovered, determines waiting.
    from fixed_execution import FixedStore

    results = []
    for source_position in (5, 15):
        initial = {"eu/target": 0, "us/source": 2}
        db = FixedStore(initial, {key: (key,) for key in initial})

        def fixed(owner, key, position):
            attempt = db.begin_fixed(owner, (position, owner), [key], {key: owner}, owner)
            assert db.reserve_position(attempt, [key])
            db.fix_position(attempt)
            assert db.publish_position(attempt, [key])
            return attempt

        def finish(attempt, key, value):
            attempt.writes[key] = value
            db.seal_values(attempt)
            db.decide(attempt, True)
            db.install(attempt, [key])

        source = fixed("source", "us/source", source_position)
        target = fixed("target", "eu/target", 10)
        captured = db.capture(target, "us/source", wait=True)
        assert captured == (source_position > 10)
        finish(source, "us/source", 9)
        if not captured:
            assert db.capture(target, "us/source", wait=True)
        value = target.reads["us/source"]["us/source"]
        assert value == (9 if source_position < 10 else 2)
        finish(target, "eu/target", value)
        assert db.check_serial()["eu/target"] == value
        results.append({"name": "late_source_" + ("earlier" if source_position < 10 else "later"),
                        "target_waited": not captured, "target_result": value,
                        "serial_valid": True})
    return results


def multiple_bound_histories():
    from fixed_execution import FixedStore

    results = []
    keys = ("a/row", "b/row", "c/row")
    for order in permutations(keys):
        for read_after in range(4):
            initial = dict.fromkeys(keys, 0)
            db = FixedStore(initial, {**{key: (key,) for key in keys}, "all": keys})
            writer = db.begin_fixed("T", (1, "T"), keys, dict.fromkeys(keys, "T"), "T")
            reader = db.begin("Q", (50, "Q"))
            captured = None
            for index in range(4):
                if index == read_after:
                    captured = db.capture(reader, "all", wait=True)
                if index < 3:
                    assert db.reserve_position(writer, [order[index]])
            db.fix_position(writer)
            for key in order:
                assert db.publish_position(writer, [key])
            if not captured:
                captured = db.capture(reader, "all", wait=True)
            before = writer.c > reader.s
            assert captured == before
            writer.writes.update(dict.fromkeys(keys, 1))
            db.seal_values(writer)
            db.decide(writer, True)
            for key in reversed(order):
                db.install(writer, [key])
            if not captured:
                assert db.capture(reader, "all", wait=True)
            assert set(reader.reads["all"].values()) == {0 if before else 1}
            db.choose(reader)
            db.decide(reader, True)
            assert db.check_serial() == dict.fromkeys(keys, 1)
            results.append({"name": "bound_order_" + "".join(key[0] for key in order)
                            + f"_read_after_{read_after}", "serial_valid": True,
                            "reader_before_writer": before,
                            "writer_position": writer.c, "reader_position": reader.s})
    return results


def maximum_owner(scores):
    return max(scores, key=lambda row: (scores[row], row))


def max_predicate_comparison():
    """Finite check of one indexed MAX protection rule, not a lock manager.

    The maximum includes a stable tie ID. Protection covers the selected row
    plus [maximum_index_key, infinity); writes check both old and new keys.
    """
    allowed, blocked = 0, 0
    for values in product(range(4), repeat=3):
        scores = dict(zip(("a", "b", "c"), values))
        winner = maximum_owner(scores)
        bound = (scores[winner], winner)
        for row in scores:
            for replacement in range(4):
                if replacement == scores[row]:
                    continue
                permitted = row != winner and max((scores[row], row), (replacement, row)) < bound
                if permitted:
                    changed = {**scores, row: replacement}
                    assert maximum_owner(changed) == winner
                    allowed += 1
                else:
                    blocked += 1

    def serial_valid(order, observed, initial):
        scores, marked = dict(initial), False
        for actor in order:
            if actor == "T":
                if maximum_owner(scores) != observed["T"]:
                    return False
                marked = True
            elif actor == "U":
                if marked != observed["U"]:
                    return False
                scores["b"] = 12
        return True

    # Gather unprotected shard maxima, then protect only the global winner.
    # U reads the old marker and introduces a new maximum before T marks a.
    valid_orders = [list(order) for order in permutations(("T", "U"))
                    if serial_valid(order, {"T": "a", "U": False}, {"a": 10, "b": 9})]
    assert not valid_orders

    scores = {"a": 100, "b": 50}
    winner = maximum_owner(scores)
    bound = (scores[winner], winner)
    assert (60, "b") < bound < (101, "b")
    scores["b"] = 60
    assert maximum_owner(scores) == "a"
    # T marks a here. V's proposed 101 was excluded until this point.
    scores["b"] = 101
    assert maximum_owner(scores) == "b"

    # Opposite bargain, executed through the actual fixed-position core: T
    # observes MAX but writes a separate marker. V overtakes the maximum and
    # physically commits while T is still pending. Replay must order T before V.
    from fixed_execution import FixedStore

    initial = {"eu/a": 100, "eu/b": 50, "eu/marker": 0}
    db = FixedStore(initial, {**{key: (key,) for key in initial}, "scores": ("eu/a", "eu/b")})

    def fixed(owner, key, floor):
        attempt = db.begin_fixed(owner, (floor, owner), [key], {key: owner}, owner)
        assert db.reserve_position(attempt, [key])
        db.fix_position(attempt)
        assert db.publish_position(attempt, [key])
        return attempt

    def finish(attempt, key, value):
        attempt.writes[key] = value
        db.seal_values(attempt)
        db.decide(attempt, True)
        db.install(attempt, [key])

    marker = fixed("T", "eu/marker", 10)
    assert db.capture(marker, "scores", wait=True)
    assert maximum_owner(marker.reads["scores"]) == "eu/a"
    challenger = fixed("V", "eu/b", 20)
    assert db.capture(challenger, "eu/b", wait=True)
    finish(challenger, "eu/b", 101)
    assert marker.decision is None
    finish(marker, "eu/marker", 1)  # The marker encodes selected row a.
    assert db.check_serial() == {"eu/a": 100, "eu/b": 101, "eu/marker": 1}
    assert marker.c < challenger.c
    predicate_blocks_challenger = (101, "b") >= bound
    assert predicate_blocks_challenger
    return {"name": "indexed_max_predicate_competitor", "finite_states": 64,
            "row_transitions": allowed + blocked, "permitted_preserving_max": allowed,
            "blocked_transitions": blocked,
            "unknown_winner": {"predicate_locks": {"loser_50_to_60": "passes",
                                                       "challenger_60_to_101": "waits"},
                               "target_collection_envelope": {"loser_50_to_60": "waits",
                                                               "challenger_60_to_101": "waits"},
                               "serial_witness": ["loser_50_to_60", "mark_max_a", "challenger_60_to_101"]},
            "known_marker": {"fixed_position_physical_commit_order": ["V", "T"],
                             "fixed_position_serial_order": ["T", "V"],
                             "retained_MAX_predicate_blocks_challenger": predicate_blocks_challenger,
                             "serial_valid": True},
            "unprotected_distributed_gather_valid_serial_orders": valid_orders}


def retention_histories():
    history = [(0, 2), (12, 9)]
    position = 10
    expected = max((version for version in history if version[0] <= position))[1]
    retained = history[1:]
    assert not any(version[0] <= position for version in retained)
    # Reading latest would silently change a fixed-position program's input.
    unsafe_latest = retained[-1][1]
    assert unsafe_latest != expected
    return {"name": "late_source_history_expired", "fixed_position": position,
            "historical_value": expected, "incorrect_latest_fallback": unsafe_latest,
            "safe_expiration_result": "history unavailable; refuse the read",
            "completion_requires": "retention coverage or an explicit abort/expiration contract"}


def run():
    histories = (index_histories() + own_constraint_histories() + gate_histories()
                 + fixed_source_histories() + multiple_bound_histories())
    return {"scope": "Finite semantic and gate-admission audit; no measured performance or recovery",
            "histories": histories, "gate_state_search": admission_state_search(),
            "predicate_competitor": max_predicate_comparison(),
            "source_retention": retention_histories(),
            "sources": {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
                        for name in ("envelope_audit.py", "write_admission.py",
                                     "fixed_execution.py", "certification.py")}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = run()
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"histories": len(result["histories"]),
                      "intentional_invalid_serial_histories": sum(
                          h.get("expected_serial_valid") is False for h in result["histories"]),
                      "gate_state_search": result["gate_state_search"]}, indent=2))


if __name__ == "__main__":
    main()
