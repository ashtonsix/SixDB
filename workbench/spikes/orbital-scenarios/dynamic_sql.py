#!/usr/bin/env python3
"""Small SQL/ELT application histories with genuinely computed write sets.

These are logical histories, not a SQL engine, scheduler or recovery protocol.
The finite preallocated key universe includes absent keys. Programs compute
their writes and returned results from captured state; discovery is only a
prediction. FixedStore is unchanged. Conservative coverage and representation
changes are separate comparisons, with their exclusion/read costs explicit.
"""

import argparse
from collections import Counter, defaultdict
from copy import deepcopy
import hashlib
import json
from pathlib import Path

from fixed_execution import FixedStore
from write_admission import Admission


def evaluate(program, state, generations):
    """Small complete application programs; business rejection writes nothing."""
    op = program["op"]
    if op == "assign":
        return dict(program["values"]), {"status": "applied"}, {}
    if op == "merge":
        row, email, seq = program["row"], program["email"], program["seq"]
        old, old_seq = state[f"eu/customer/{row}/email"], state[f"eu/customer/{row}/seq"]
        if old_seq is not None and seq <= old_seq:
            return {}, {"status": "duplicate_or_old", "row": row, "seq": old_seq}, {}
        other = next((candidate for candidate in (1, 2)
                      if candidate != row and state[f"eu/customer/{candidate}/email"] == email), None)
        if other is not None:
            return {}, {"status": "unique_rejection", "email": email, "owner": other}, {}
        writes = {f"eu/customer/{row}/email": email, f"eu/customer/{row}/seq": seq}
        if program["indexed"]:
            if old is not None:
                writes[f"us/email/{old}"] = None
            writes[f"us/email/{email}"] = row
        return writes, {"status": "merged", "row": row, "old": old, "new": email}, {}
    if op == "delete_customer":
        row, seq = program["row"], program["seq"]
        old, old_seq = state[f"eu/customer/{row}/email"], state[f"eu/customer/{row}/seq"]
        if old_seq is not None and seq <= old_seq:
            return {}, {"status": "duplicate_or_old", "row": row, "seq": old_seq}, {}
        writes = {f"eu/customer/{row}/email": None, f"eu/customer/{row}/seq": seq}
        if program["indexed"] and old is not None:
            writes[f"us/email/{old}"] = None
        return writes, {"status": "deleted", "row": row, "old": old}, {}
    if op == "cascade":
        parents = sorted(key for key, value in state.items()
                         if key.startswith("eu/parent/") and value == 1)
        parent_ids = {int(key.rsplit("/", 1)[1]) for key in parents}
        children = sorted(key for key, value in state.items()
                          if key.startswith("us/child/") and value in parent_ids)
        writes = dict.fromkeys(parents + children, None)
        return writes, {"status": "deleted", "parents": parents, "children": children}, {}
    if op == "insert_child":
        parent, child = program["parent"], program["child"]
        if state[f"eu/parent/{parent}"] is None:
            return {}, {"status": "foreign_key_rejection", "parent": parent}, {}
        if state[child] is not None:
            return {}, {"status": "child_key_rejection", "child": child}, {}
        return {child: parent}, {"status": "inserted", "child": child}, {}
    if op == "insert_select_rows":
        selected = {key.replace("/source/", "/dest/"): value
                    for key, value in state.items()
                    if "/source/" in key and value is not None and value > 0}
        return selected, {"status": "copied", "keys": sorted(selected)}, {}
    if op == "object_correction":
        root = program["root"]
        image = dict(generations[state[root]])
        image[program["key"]] = program["value"]
        token = program["token"]
        return {root: token}, {"status": "corrected", "key": program["key"]}, {token: image}
    if op == "object_insert_select":
        writes, images = {}, {}
        for region in ("eu", "us"):
            root, token = f"{region}/dest_root", program["tokens"][region]
            image = dict(generations[state[root]])
            for key, value in state.items():
                if key.startswith(region + "/source/") and value is not None and value > 0:
                    image[key.rsplit("/", 1)[1]] = value
            writes[root], images[token] = token, image
        return writes, {"status": "merged_image", "roots": sorted(writes)}, images
    if op == "publish_job":
        # The input cut is explicit job data, not a read silently carried into
        # a later serial SQL transaction. Authoritative replacement is named.
        writes = {f"{region}/dest_root": program["tokens"][region] for region in ("eu", "us")}
        images = {program["tokens"][region]: dict(program["images"][region])
                  for region in ("eu", "us")}
        return writes, {"status": "authoritative_replacement", "input_cut": program["input_cut"]}, images
    if op == "activate_feed":
        if state["eu/feed_checkpoint"] >= program["through"]:
            return {}, {"status": "duplicate_activation", "through": state["eu/feed_checkpoint"]}, {}
        writes = {f"{region}/dest_root": program["tokens"][region] for region in ("eu", "us")}
        writes["eu/feed_checkpoint"] = program["through"]
        images = {program["tokens"][region]: dict(program["images"][region]) for region in ("eu", "us")}
        return writes, {"status": "feed_activated", "through": program["through"]}, images
    if op == "report":
        return {}, {"status": "report", "values": dict(state)}, {}
    raise AssertionError(op)


class History:
    def __init__(self, initial, scopes, generations=None):
        self.initial = dict(initial)
        self.scopes = {**{key: (key,) for key in initial}, **scopes}
        self.db = FixedStore(initial, self.scopes)
        self.gates = Admission({})
        self.generations = deepcopy(generations or {})
        self.initial_generations = deepcopy(self.generations)
        self.counter = 0
        self.counts = Counter()
        self.records, self.events = [], []

    def identity(self, logical):
        self.counter += 10
        return f"{logical}#{self.counter}"

    @staticmethod
    def groups(keys):
        grouped = defaultdict(list)
        for key in sorted(keys):
            grouped[key.split("/", 1)[0]].append(key)
        return [tuple(grouped[node]) for node in sorted(grouped)]

    def admission(self, logical, coverage):
        self.gates.priority.setdefault(logical, (len(self.gates.priority), logical))
        for group in self.groups(coverage):
            if not self.gates.request(logical, group):
                self.counts["blocked_admission_requests"] += 1
                return False
        self.counts["max_transaction_coverage"] = max(self.counts["max_transaction_coverage"], len(coverage))
        return True

    def begin(self, logical, coverage, program, scopes):
        coverage = set(coverage)
        assert coverage and self.admission(logical, coverage)
        owner = self.identity(logical)
        attempt = self.db.begin_fixed(owner, (self.counter, owner), coverage, self.gates.grants, logical)
        for group in self.groups(coverage):
            assert self.db.reserve_position(attempt, group)
        self.db.fix_position(attempt)
        for group in self.groups(coverage):
            assert self.db.publish_position(attempt, group)
        return {"logical": logical, "attempt": attempt, "program": deepcopy(program),
                "scopes": tuple(scopes), "coverage": coverage}

    def capture(self, draft):
        for scope in draft["scopes"]:
            if scope not in draft["attempt"].reads:
                if not self.db.capture(draft["attempt"], scope, wait=True):
                    self.counts["read_waits"] += 1
                    return False
        return True

    def compute(self, draft):
        assert self.capture(draft)
        attempt = draft["attempt"]
        observed = {key: value for values in attempt.reads.values() for key, value in values.items()}
        writes, result, images = evaluate(draft["program"], observed, self.generations)
        draft.update(writes=writes, result=result, images=images)
        self.counts["application_evaluations"] += 1
        self.counts["captured_cells"] += len(observed)
        attempt.writes.update(writes)
        missing = set(writes) - draft["coverage"]
        if missing:
            self.db.decide(attempt, False)
            self.db.release(attempt, draft["coverage"])
            self.gates.release_all(draft["logical"])
            self.counts["footprint_failures"] += 1
            self.events.append({"event": "footprint_failure", "logical": draft["logical"],
                                "missing": sorted(missing), "private_result": result})
            return False
        return True

    def finish(self, draft, between_participants=None, before_install=None):
        attempt = draft["attempt"]
        assert attempt.decision is None
        for token, image in draft["images"].items():
            assert token not in self.generations, "generation identifiers are immutable"
            self.generations[token] = dict(image)
            self.counts["private_generation_cells"] += len(image)
        self.db.seal_values(attempt)
        self.db.decide(attempt, True)
        self.records.append({"logical": draft["logical"], "position": attempt.c,
                             "program": deepcopy(draft["program"]), "scopes": draft["scopes"],
                             "reads": deepcopy(attempt.reads), "writes": dict(draft["writes"]),
                             "result": deepcopy(draft["result"]), "images": deepcopy(draft["images"])})
        self.counts["committed_transactions"] += 1
        self.counts["business_rejections"] += draft["result"]["status"] in (
            "unique_rejection", "foreign_key_rejection", "child_key_rejection")
        if before_install is not None:
            before_install()
        for index, group in enumerate(self.groups(draft["coverage"])):
            self.db.install(attempt, group)
            self.gates.release(draft["logical"], group)
            if index == 0 and between_participants is not None:
                between_participants()
        return draft["result"]

    def execute(self, logical, coverage, program, scopes, between_participants=None):
        draft = self.begin(logical, coverage, program, scopes)
        if not self.compute(draft):
            return None
        return self.finish(draft, between_participants)

    def discover(self, logical, program, scopes):
        owner = self.identity(logical)
        attempt = self.db.begin(owner, (self.counter, owner))
        for scope in scopes:
            assert self.db.capture(attempt, scope, wait=True)
        observed = {key: value for values in attempt.reads.values() for key, value in values.items()}
        writes, result, images = evaluate(program, observed, self.generations)
        assert not images, "generation construction is not this discovery comparator"
        self.db.choose(attempt)
        self.db.decide(attempt, True)
        self.counts["discovery_passes"] += 1
        self.records.append({"logical": logical, "position": attempt.c, "program": {"op": "report"},
                             "scopes": tuple(scopes), "reads": deepcopy(attempt.reads), "writes": {},
                             "result": {"status": "report", "values": observed}, "images": {}})
        return set(writes), result

    def summary(self, name, **detail):
        state, images = dict(self.initial), deepcopy(self.initial_generations)
        for record in sorted(self.records, key=lambda value: value["position"]):
            assert set(record["reads"]) == set(record["scopes"])
            observed = {}
            for scope, actual in record["reads"].items():
                assert actual == {key: state[key] for key in self.scopes[scope]}
                observed.update(actual)
            writes, result, created = evaluate(record["program"], observed, images)
            assert (writes, result, created) == (record["writes"], record["result"], record["images"])
            assert not set(created).intersection(images)
            images.update(created)
            state.update(writes)
        assert state == self.db.head() == self.db.check_serial()
        assert images == self.generations
        assert not self.db.claims and not self.gates.grants and not self.gates.waiting
        return {"name": name, "serial_check": True, "counts": dict(self.counts),
                "final": state, "generations": images, "committed": self.records,
                "events": self.events, **detail}


def customer_history(indexed=True):
    initial = {f"eu/customer/{row}/{field}": value
               for row, email in ((1, 1), (2, 4)) for field, value in (("email", email), ("seq", 0))}
    emails = tuple(key for key in initial if key.endswith("/email"))
    if indexed:
        initial.update({f"us/email/{email}": (1 if email == 1 else 2 if email == 4 else None)
                        for email in range(1, 9)})
    return History(initial, {"eu/customers": emails})


def merge_program(row=1, email=2, seq=10, indexed=True):
    return {"op": "merge", "row": row, "email": email, "seq": seq, "indexed": indexed}


def merge_scopes(row=1):
    return ("eu/customers", f"eu/customer/{row}/seq")


def assert_customer_index(history, indexed):
    state = history.db.head()
    actual = {state[f"eu/customer/{row}/email"]: row for row in (1, 2)}
    assert len(actual) == 2, "unique emails"
    if indexed:
        assert {int(key.rsplit("/", 1)[1]): value for key, value in state.items()
                if key.startswith("us/email/") and value is not None} == actual


def merge_moving_index(indexed):
    history = customer_history(indexed)
    program = merge_program(indexed=indexed)
    predicted, _ = history.discover("discover-T", program, merge_scopes())
    preceding = merge_program(email=3, seq=1, indexed=indexed)
    preceding_keys, _ = history.discover("discover-U", preceding, merge_scopes())
    assert history.execute("U", preceding_keys, preceding, merge_scopes())["status"] == "merged"
    result = history.execute("T", predicted, program, merge_scopes())
    if indexed:
        assert result is None
        assert history.events[-1]["missing"] == ["us/email/3"]
        predicted, _ = history.discover("rediscover-T", program, merge_scopes())
        result = history.execute("T-retry", predicted, program, merge_scopes())
    assert result == {"status": "merged", "row": 1, "old": 3, "new": 2}
    # Duplicate ingestion is a successful no-write business outcome.
    noop = history.begin("duplicate", {"eu/customer/1/email", "eu/customer/1/seq"}, program, merge_scopes())
    assert history.compute(noop) and not noop["writes"]
    assert history.finish(noop)["status"] == "duplicate_or_old"
    assert_customer_index(history, indexed)
    return history.summary("merge_materialized_index" if indexed else "merge_derived_index",
                           representation="maintained entries" if indexed else "derive lookup by scanning rows",
                           limit="The derived-index control removes stored index outputs and pays row-scan work; it is not a free maintained-index implementation.")


def concurrent_unique_rejection():
    history = customer_history(False)
    first = history.begin("A", {"eu/customer/1/email", "eu/customer/1/seq"},
                          merge_program(indexed=False), merge_scopes())
    second = history.begin("B", {"eu/customer/2/email", "eu/customer/2/seq"},
                           merge_program(row=2, indexed=False), merge_scopes(2))
    assert history.capture(first)
    assert not history.capture(second)
    assert history.compute(first)
    assert history.finish(first)["status"] == "merged"
    assert history.compute(second) and not second["writes"]
    assert history.finish(second)["status"] == "unique_rejection"
    assert_customer_index(history, False)
    return history.summary("unique_business_rejection_after_ordered_read",
                           result="One accepted merge, one committed constraint rejection; no serialization abort.")


def upsert_delete_reinsert():
    prototype = customer_history(True)
    initial = dict(prototype.initial)
    initial.update({"eu/customer/2/email": None, "eu/customer/2/seq": None, "us/email/4": None})
    history = History(initial, {"eu/customers": prototype.scopes["eu/customers"]})
    coverage = {"eu/customer/2/email", "eu/customer/2/seq"} | {
        key for key in initial if key.startswith("us/email/")}
    assert history.execute("insert", coverage, merge_program(row=2, email=2, seq=1),
                           merge_scopes(2)) == {"status": "merged", "row": 2, "old": None, "new": 2}
    assert history.execute("delete", coverage,
                           {"op": "delete_customer", "row": 2, "seq": 5, "indexed": True},
                           merge_scopes(2))["status"] == "deleted"
    assert history.db.head()["eu/customer/2/email"] is None
    assert history.execute("late-replay", coverage, merge_program(row=2, email=2, seq=4),
                           merge_scopes(2))["status"] == "duplicate_or_old"
    assert history.db.head()["eu/customer/2/email"] is None
    assert history.execute("reinsert", coverage, merge_program(row=2, email=3, seq=6),
                           merge_scopes(2))["status"] == "merged"
    conflict_coverage = {"eu/customer/1/email", "eu/customer/1/seq"} | {
        key for key in initial if key.startswith("us/email/")}
    assert history.execute("unique-conflict", conflict_coverage, merge_program(email=3),
                           merge_scopes())["status"] == "unique_rejection"
    assert_customer_index(history, True)
    return history.summary("upsert_delete_reinsert_and_late_feed_replay",
                           accepted_source_contract="One source supplies monotone per-key sequence numbers; deletion retains its sequence tombstone. This does not reconcile independent unordered sources.")


def merge_conservative_domain():
    history = customer_history(True)
    coverage = {"eu/customer/1/email", "eu/customer/1/seq"} | {
        key for key in history.initial if key.startswith("us/email/")}
    draft = history.begin("T", coverage, merge_program(), merge_scopes())
    # Different primary row and disjoint current/new emails still share this
    # deliberately coarse index namespace coverage.
    other = {"eu/customer/2/email", "eu/customer/2/seq", "us/email/4", "us/email/5"}
    assert not history.admission("unrelated-customer", other)
    history.gates.release_all("unrelated-customer")
    assert history.compute(draft)
    actual = len(draft["writes"])
    history.finish(draft)
    assert_customer_index(history, True)
    return history.summary("merge_full_index_domain",
                           covered_cells=len(coverage), actual_writes=actual,
                           excluded_disjoint_customer=True,
                           progress_rule="One admitted evaluation if finite predecessors finish; every index writer honors the declared namespace exclusion.")


def cascade_history():
    initial = {"eu/parent/1": 1, "eu/parent/2": 0, "us/child/1": 1, "us/child/2": 2,
               **{f"us/child/{number}": None for number in range(3, 8)}, "eu/other_tenant": 0}
    related = tuple(key for key in initial if key != "eu/other_tenant")
    return History(initial, {"eu/family": related})


def insert_child(history, number):
    key = f"us/child/{number}"
    program = {"op": "insert_child", "parent": 1, "child": key}
    return history.execute(f"insert-{number}", {key}, program, ("eu/parent/1", key))


def cascade_repeated_discovery():
    history = cascade_history()
    program, scope = {"op": "cascade"}, ("eu/family",)
    for number in range(3, 7):
        predicted, _ = history.discover(f"discover-{number}", program, scope)
        assert insert_child(history, number)["status"] == "inserted"
        assert history.execute(f"delete-{number}", predicted, program, scope) is None
    assert history.counts["footprint_failures"] == 4
    before_quiet = dict(history.db.head())
    assert before_quiet["eu/parent/1"] == 1
    predicted, _ = history.discover("quiet-discovery", program, scope)
    result = history.execute("quiet-delete", predicted, program, scope)
    assert result["children"] == [f"us/child/{number}" for number in (1, 3, 4, 5, 6)]
    assert history.db.head()["us/child/2"] == 2
    assert all(value != 1 for key, value in history.db.head().items() if key.startswith("us/child/"))
    return history.summary("cascade_growing_children", finite_four_attempt_outcome="failure",
                           before_quiet=before_quiet, quiet_round_is_progress_guarantee=False)


def cascade_conservative_domain():
    history = cascade_history()
    coverage = set(history.scopes["eu/family"])
    # The envelope was chosen before discovering current child membership.
    assert insert_child(history, 3)["status"] == "inserted"
    draft = history.begin("delete", coverage, {"op": "cascade"}, ("eu/family",))
    assert not history.admission("child-insert", {"us/child/4"})
    history.gates.release_all("child-insert")
    assert not history.admission("nonmatching-parent-edit", {"eu/parent/2"})
    history.gates.release_all("nonmatching-parent-edit")
    assert history.execute("other-tenant", {"eu/other_tenant"},
                           {"op": "assign", "values": {"eu/other_tenant": 1}}, ())["status"] == "applied"
    assert history.compute(draft)
    assert history.finish(draft)["children"] == ["us/child/1", "us/child/3"]
    assert insert_child(history, 4)["status"] == "foreign_key_rejection"
    return history.summary("cascade_declared_family_domain",
                           covered_cells=len(coverage), delete_writes=3,
                           other_tenant_completed_during_delete=True,
                           nonmatching_inside_target_writer_excluded=True,
                           progress_rule="Declared family domain excludes matching and nonmatching covered writes. Outside-domain work continues; later child insertion observes the deleted parent.")


def insert_select_discovery():
    initial = {"eu/source/a": 10, "us/source/b": None, "eu/dest/a": None, "us/dest/b": None}
    history = History(initial, {"eu/sources": ("eu/source/a", "us/source/b")})
    program = {"op": "insert_select_rows"}
    predicted, _ = history.discover("discover", program, ("eu/sources",))
    history.execute("new-source", {"us/source/b"}, {"op": "assign", "values": {"us/source/b": 20}}, ())
    assert history.execute("copy", predicted, program, ("eu/sources",)) is None
    predicted, _ = history.discover("rediscover", program, ("eu/sources",))
    assert history.execute("copy-retry", predicted, program, ("eu/sources",))["keys"] == ["eu/dest/a", "us/dest/b"]
    return history.summary("insert_select_new_destination_key",
                           limit="A later qualifying source row changes destination coverage even when every destination was initially absent.")


def insert_select_complete_envelope():
    initial = {"eu/source/a": 10, "us/source/b": None, "eu/dest/a": None,
               "us/dest/b": None, "eu/outside_dest": 0}
    history = History(initial, {"eu/sources": ("eu/source/a", "us/source/b")})
    # This represents a declared destination relation, including absent keys,
    # rather than the row identities found by a first execution.
    coverage = {"eu/dest/a", "us/dest/b"}
    history.execute("new-source-before-cut", {"us/source/b"},
                    {"op": "assign", "values": {"us/source/b": 20}}, ())
    draft = history.begin("insert-select", coverage, {"op": "insert_select_rows"}, ("eu/sources",))
    assert history.compute(draft)
    history.execute("source-continues", {"us/source/b"},
                    {"op": "assign", "values": {"us/source/b": 30}}, ())
    history.execute("outside-destination", {"eu/outside_dest"},
                    {"op": "assign", "values": {"eu/outside_dest": 1}}, ())
    assert not history.admission("inside-target-correction", {"eu/dest/a"})
    history.gates.release_all("inside-target-correction")
    result = history.finish(draft)
    assert result["keys"] == ["eu/dest/a", "us/dest/b"]
    assert history.db.head()["us/dest/b"] == 20 and history.db.head()["us/source/b"] == 30
    return history.summary("insert_select_declared_destination_envelope",
                           discovery_passes=0, source_writer_completed_during_computation=True,
                           outside_target_writer_completed=True, inside_target_writer_excluded=True,
                           restriction="A target relation/range covers new identities. This finite universe models coverage semantics, not a general range admission implementation.")


def row_owned_index_visibility():
    # The only logical mutation ownership is the primary row. Index entries
    # are physical projections of each row's versioned outcome, not additional
    # independently acquired logical outputs.
    initial = {"eu/row/a": 1, "eu/row/b": 10}
    history = History(initial, {"eu/index_predicates": tuple(initial)})
    index_versions = {key: [((0, ""), value)] for key, value in initial.items()}
    physical_rows, physical_index = dict(initial), {(key, value) for key, value in initial.items()}
    draft = history.begin("indexed-A", {"eu/row/a"},
                          {"op": "assign", "values": {"eu/row/a": 2}}, ("eu/row/a",))
    assert history.compute(draft)
    waiting, returned = [], []

    def finish_projection_before_logical_release():
        position = draft["attempt"].c
        physical_rows["eu/row/a"] = 2
        physical_index.remove(("eu/row/a", 1))
        assert ("eu/row/a", 2) not in physical_index
        assert history.db.claims["eu/row/a"].owner == draft["attempt"].owner
        # Both the matching and provably-at-the-end nonmatching query must
        # initially wait: unknown pending row effects cover the whole domain.
        for wanted in (2, 999):
            owner = history.identity(f"lookup-{wanted}")
            reader = history.db.begin(owner, (history.counter, owner))
            assert not history.db.capture(reader, "eu/index_predicates", wait=True)
            waiting.append((reader, wanted))
        # This primary writer neither needs the unfinished row nor an index
        # lookup. Registered predicate bounds place it above both read cuts.
        other = history.begin("indexed-B", {"eu/row/b"},
                              {"op": "assign", "values": {"eu/row/b": 11}}, ("eu/row/b",))
        assert history.compute(other)
        assert other["attempt"].c > max(reader.s for reader, _ in waiting)
        physical_rows["eu/row/b"] = 11
        physical_index.remove(("eu/row/b", 10))
        physical_index.add(("eu/row/b", 11))
        index_versions["eu/row/b"].append((other["attempt"].c, 11))
        history.finish(other)
        assert history.db.head()["eu/row/b"] == 11
        assert history.db.head()["eu/row/a"] == 1
        # Complete A's projection before resolving its one logical promise.
        physical_index.add(("eu/row/a", 2))
        index_versions["eu/row/a"].append((position, 2))

    history.finish(draft, before_install=finish_projection_before_logical_release)
    for reader, wanted in waiting:
        assert history.db.capture(reader, "eu/index_predicates", wait=True)
        image = reader.reads["eu/index_predicates"]
        index_image = {key: max((item for item in versions if item[0] <= reader.s),
                               key=lambda item: item[0])[1]
                       for key, versions in index_versions.items()}
        assert index_image == image == {"eu/row/a": 2, "eu/row/b": 10}
        matches = sorted(key for key, value in index_image.items() if value == wanted)
        assert matches == (["eu/row/a"] if wanted == 2 else [])
        history.db.choose(reader)
        history.db.decide(reader, True)
        returned.append({"wanted": wanted, "matches": matches, "position": reader.s,
                         "row_image": dict(image)})
    assert physical_index == {(key, value) for key, value in physical_rows.items()}
    return history.summary("row_owned_versioned_index_outcome",
                           admitted_logical_keys_for_A=["eu/row/a"], discovered_index_gate_requests=0,
                           matching_lookup_waited=True, nonmatching_lookup_waited=True,
                           unrelated_primary_writer_completed_during_partial_install=True,
                           returned=returned,
                           restriction="This probe uses one known row-partition authority. A collection-wide read bound and pending-row check cover predicate visibility. One row promise lasts through row plus index availability; old index versions are retained. Remotely routed index authorities must receive conservative unknown-effect promises and contribute bounds before c, or queries must visit the known row authorities. No late invisible index authority is permitted.")


def object_history():
    initial = {"eu/source/a": 10, "us/source/b": 20, "eu/dest_root": 1, "us/dest_root": 2}
    return History(initial, {"eu/sources": ("eu/source/a", "us/source/b"),
                             "eu/roots": ("eu/dest_root", "us/dest_root")},
                   {1: {"a": 0, "correction": 0}, 2: {"b": 0}})


def objects_preserve_correction():
    history = object_history()
    stale = {"a": 10, "correction": 0}
    correction = {"op": "object_correction", "root": "eu/dest_root", "key": "correction", "value": 7, "token": 3}
    history.execute("correction", {"eu/dest_root"}, correction, ("eu/dest_root",))
    assert history.generations[history.db.head()["eu/dest_root"]]["correction"] == 7
    assert stale["correction"] != 7  # A stale root swap is not the merge contract.
    program = {"op": "object_insert_select", "tokens": {"eu": 4, "us": 5}}
    draft = history.begin("refresh", {"eu/dest_root", "us/dest_root"}, program, ("eu/sources", "eu/roots"))
    blocked = history.admission("different-row-same-partition", {"eu/dest_root"})
    assert not blocked
    history.gates.release_all("different-row-same-partition")
    assert history.compute(draft)
    waiting = []
    def between():
        owner = history.identity("reader")
        reader = history.db.begin(owner, (history.counter, owner))
        assert not history.db.capture(reader, "eu/roots", wait=True)
        waiting.append(reader)
    history.finish(draft, between)
    reader = waiting[0]
    assert history.db.capture(reader, "eu/roots", wait=True)
    history.db.choose(reader)
    history.db.decide(reader, True)
    images = {region: history.generations[history.db.head()[f"{region}/dest_root"]] for region in ("eu", "us")}
    assert images == {"eu": {"a": 10, "correction": 7}, "us": {"b": 20}}
    return history.summary("generation_objects_preserve_correction_and_atomic_visibility",
                           stale_merge_would_lose_correction=True, partial_publication_reader_waited=True,
                           final_images=images, same_partition_point_writer_excluded=True,
                           restriction="Every destination mutation owns its partition object. This is coarse write exclusion; logical publication is small but construction and same-partition queueing remain.")


def authoritative_job_replacement():
    history = object_history()
    cut = {"eu": {"a": 10}, "us": {"b": 20}}
    history.execute("correction", {"eu/dest_root"},
                    {"op": "object_correction", "root": "eu/dest_root", "key": "correction", "value": 7, "token": 3},
                    ("eu/dest_root",))
    history.execute("source-moves", {"eu/source/a"}, {"op": "assign", "values": {"eu/source/a": 99}}, ())
    result = history.execute("publish-job", {"eu/dest_root", "us/dest_root"},
                             {"op": "publish_job", "input_cut": "explicit-source-cut-before-source-moves",
                              "tokens": {"eu": 4, "us": 5}, "images": cut}, ())
    assert result["status"] == "authoritative_replacement"
    assert history.generations[history.db.head()["eu/dest_root"]] == {"a": 10}
    return history.summary("explicit_snapshot_job_authoritative_replacement",
                           accepted_tradeoff="Published job image is stale to current source and intentionally discards destination corrections. It is not the preserving INSERT SELECT/merge contract.")


def ordered_feed_atomic_activation():
    snapshot_cut = 100
    image = {"eu": {"a": 10}, "us": {"b": 20}}
    # A trusted source snapshot/stream boundary is an input to this program.
    # Arrival order may differ; sequence numbers impose the source order.
    events = [(103, "us", "d", 4), (101, "eu", "a", 11),
              (102, "us", "b", None), (101, "eu", "a", 11),
              (99, "us", "b", 999), (104, "eu", "a", 12)]
    by_sequence = {}
    for sequence, region, key, value in events:
        event = (region, key, value)
        assert sequence not in by_sequence or by_sequence[sequence] == event
        by_sequence[sequence] = event
    through, ignored = snapshot_cut, 0
    for sequence, (region, key, value) in sorted(by_sequence.items()):
        if sequence <= snapshot_cut:
            ignored += 1
            continue
        if value is None:
            image[region].pop(key, None)
        else:
            image[region][key] = value
        through = sequence
    assert image == {"eu": {"a": 12}, "us": {"d": 4}}
    initial = {"eu/dest_root": 1, "us/dest_root": 2,
               "eu/feed_checkpoint": snapshot_cut, "eu/source_frontier": 105}
    history = History(initial, {"eu/activation": ("eu/dest_root", "us/dest_root", "eu/feed_checkpoint")},
                      {1: {"a": 10}, 2: {"b": 20}})
    program = {"op": "activate_feed", "through": through,
               "tokens": {"eu": 3, "us": 4}, "images": image}
    coverage = set(history.scopes["eu/activation"])
    waiting = []
    def after_first_participant():
        # The checkpoint and EU object have physically installed while US has
        # not. No reader may treat the new checkpoint plus old US as a cut.
        assert history.db.head()["eu/feed_checkpoint"] == 104
        assert history.db.head()["us/dest_root"] == 2
        owner = history.identity("activation-observer")
        reader = history.db.begin(owner, (history.counter, owner))
        assert not history.db.capture(reader, "eu/activation", wait=True)
        waiting.append(reader)
    result = history.execute("activate", coverage, program, ("eu/feed_checkpoint",), after_first_participant)
    assert result == {"status": "feed_activated", "through": 104}
    reader = waiting[0]
    assert history.db.capture(reader, "eu/activation", wait=True)
    observed = reader.reads["eu/activation"]
    assert observed["eu/feed_checkpoint"] == 104
    assert {region: history.generations[observed[f"{region}/dest_root"]] for region in ("eu", "us")} == image
    history.db.choose(reader)
    history.db.decide(reader, True)
    assert history.execute("duplicate-activation", coverage, program,
                           ("eu/feed_checkpoint",))["status"] == "duplicate_activation"
    assert history.records[-1]["writes"] == {}
    return history.summary("ordered_feed_idempotence_and_atomic_activation",
                           duplicate_events=len(events) - len(by_sequence), events_before_cut_ignored=ignored,
                           partial_activation_reader_waited=True, published_cut=104, source_already_at=105,
                           contract="Trusted source cut plus total ordered immutable event IDs; source-owned target image. Live target edits require ordinary reconciliation under their mutation envelope. This does not obtain the source cut, implement connector catch-up windows, or prove crash recovery.")


def run_probes():
    results = [merge_moving_index(True), merge_moving_index(False), concurrent_unique_rejection(),
               upsert_delete_reinsert(),
               merge_conservative_domain(), cascade_repeated_discovery(), cascade_conservative_domain(),
               insert_select_discovery(), insert_select_complete_envelope(), row_owned_index_visibility(),
               objects_preserve_correction(), authoritative_job_replacement(), ordered_feed_atomic_activation()]
    root = Path(__file__).resolve().parent
    return {"source_sha256": {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
                              for name in ("dynamic_sql.py", "check_dynamic_sql.py", "fixed_execution.py",
                                           "certification.py", "write_admission.py")},
            "assumptions": ["Authored logical histories with immediate failure-free admission/control delivery.",
                            "Finite predeclared key universe, including absent rows; no claim about general predicate/range implementation.",
                            "Generation payloads are retained privately and immutably before publication; no durability, byte transport or reclamation model.",
                            "Every writer honors the compared representation's admission domain; sources are captured at the chosen serial position.",
                            "No timings, throughput, arbitrary triggers, full SQL errors or recovery proof."],
            "scenarios": results}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = run_probes()
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    for scenario in result["scenarios"]:
        print("PASS", scenario["name"], scenario["counts"])
