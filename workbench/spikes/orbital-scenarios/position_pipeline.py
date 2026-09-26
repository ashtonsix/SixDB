#!/usr/bin/env python3
"""Experimental admission release after exact-position publication.

Multiple unresolved versions may coexist for one logical identity. Temporary
writer admission lasts only through complete exact-position publication. Reads
wait for unresolved versions that can still affect their chosen cut. A later
committed full-identity overwrite can hide an earlier unresolved version; this
does not make partial writes, deltas or constrained updates blind.

This core preserves FixedStore's interface for an external scheduler. It has no
scheduler, epoch confluence, durable recovery, timeout or reclamation protocol.
`claims` is a latest-promise diagnostic view; protocol logic uses pending_by_key.
The caller still fences original requests after participant abort delivery.
"""

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path

from certification import Promise, Version, next_owned
from fixed_execution import FixedStore
from write_admission import Admission


class PipelineStore(FixedStore):
    def __init__(self, initial, scopes):
        super().__init__(initial, scopes)
        self.pending_by_key = defaultdict(list)
        self.allocation_heads = dict.fromkeys(initial, (0, ""))

    def _refresh_claims(self, keys):
        for key in keys:
            pending = [claim for claim in self.pending_by_key[key] if key in claim.remaining]
            self.pending_by_key[key] = pending
            if pending:
                self.claims[key] = max(pending, key=lambda claim: claim.minimum)
            else:
                self.claims.pop(key, None)
        self.counters["peak_pending_versions"] = max(
            self.counters["peak_pending_versions"], sum(map(len, self.pending_by_key.values())))
        self.counters["peak_versions_per_identity"] = max(
            self.counters["peak_versions_per_identity"],
            max((len(values) for values in self.pending_by_key.values()), default=0))

    def _own_claim(self, attempt, key):
        return next((claim for claim in self.pending_by_key[key]
                     if claim.owner == attempt.owner and key in claim.remaining), None)

    def admission_releasable(self, attempt):
        return (attempt.owner in self.coverage and attempt.c is not None
                and self.published[attempt.owner] == self.coverage[attempt.owner])

    def _prior_positions(self, attempt, keys):
        prior = set()
        for key in keys:
            for claim in self.pending_by_key[key]:
                if claim.owner == attempt.owner or key not in claim.remaining:
                    continue
                predecessor = self.attempts[claim.owner]
                assert (self.admission_releasable(predecessor)
                        or (predecessor.owner not in self.coverage and predecessor.c is not None)), (
                    "earlier position must be exact at every authority before admission release")
                prior.add(predecessor.c)
        return list(prior)

    def _add_claim(self, attempt, keys, minimum):
        claim = Promise(attempt.owner, keys, minimum, set(keys))
        self.promises[attempt.owner].append(claim)
        for key in keys:
            self.pending_by_key[key].append(claim)
        self._refresh_claims(keys)

    def reserve_position(self, attempt, keys, *, in_flight=False):
        keys = tuple(sorted(set(keys)))
        assert keys and set(keys) <= self.coverage[attempt.owner]
        if attempt.decision == "commit":
            assert in_flight and keys in self.reservation_bounds[attempt.owner]
            self._count(attempt, "duplicate_position_reservations")
            return True
        if not self._active(attempt, in_flight):
            return False
        if keys in self.reservation_bounds[attempt.owner]:
            self._count(attempt, "duplicate_position_reservations")
            return True
        assert not attempt.observations and not attempt.writes and attempt.c is None
        assert not any(self._own_claim(attempt, key) for key in keys)
        scopes = {scope for key in keys for scope in self.key_scopes[key]}
        strict = [self.versions[key][-1].position for key in keys]
        strict += [self.allocation_heads[key] for key in keys]
        strict += [self.R[scope] for scope in scopes]
        strict += self._prior_positions(attempt, keys)
        minimum = next_owned(attempt.s[0], attempt.owner, strict_after=strict)
        self._add_claim(attempt, keys, minimum)
        self.reservation_bounds[attempt.owner][keys] = minimum
        self._count(attempt, "position_groups_reserved")
        self._event(attempt, "reserve_position", keys=keys, minimum=minimum)
        return True

    def publish_position(self, attempt, keys, *, in_flight=False):
        result = super().publish_position(attempt, keys, in_flight=in_flight)
        if result:
            for key in keys:
                self.allocation_heads[key] = max(self.allocation_heads[key], attempt.c)
        self._refresh_claims(keys)
        return result

    def promise(self, attempt, keys, *, in_flight=False):
        """Compatibility path for the scheduler's atomic local shortcut."""
        keys = tuple(sorted(set(keys)))
        assert keys and set(keys) <= attempt.writes.keys()
        if attempt.decision == "commit":
            assert in_flight and any(claim.keys == keys for claim in self.promises[attempt.owner])
            self._count(attempt, "duplicate_promises")
            return True
        if not self._active(attempt, in_flight):
            return False
        self._manifest(attempt)
        self._count(attempt, "promise_calls")
        if any(claim.keys == keys for claim in self.promises[attempt.owner]):
            self._count(attempt, "duplicate_promises")
            return True
        assert attempt.c is None and not any(self._own_claim(attempt, key) for key in keys)
        scopes = {scope for key in keys for scope in self.key_scopes[key]}
        strict = [self.versions[key][-1].position for key in keys]
        strict += [self.allocation_heads[key] for key in keys]
        strict += [self.R[scope] for scope in scopes if self.R[scope][1] != attempt.owner]
        strict += self._prior_positions(attempt, keys)
        minimum = next_owned(attempt.s[0], attempt.owner, strict_after=strict)
        self._add_claim(attempt, keys, minimum)
        self._count(attempt, "promised_groups")
        self._count(attempt, "promised_keys", len(keys))
        self._event(attempt, "promise", keys=keys, minimum=minimum)
        return True

    def choose(self, attempt, minimum=None):
        position = super().choose(attempt, minimum)
        for key in attempt.writes:
            self.allocation_heads[key] = max(self.allocation_heads[key], position)
        return position

    def _blocking(self, attempt, scope, position):
        blockers = set()
        for key in self.scopes[scope]:
            visible = max(version.position for version in self.versions[key] if version.position <= position)
            for claim in self.pending_by_key[key]:
                if claim.owner == attempt.owner or key not in claim.remaining:
                    continue
                predecessor = self.attempts[claim.owner]
                exact = (key in self.published.get(claim.owner, set())
                         or (claim.owner not in self.coverage and predecessor.c is not None))
                bound = predecessor.c if exact else claim.minimum
                # A temporary floor can rise above the visible version. It
                # cannot be hidden as though it were an exact old position.
                if bound <= position and (not exact or bound > visible):
                    blockers.add(claim.owner)
        return sorted(blockers)

    def decide(self, attempt, commit):
        assert self.attempts[attempt.owner] is attempt
        decision = "commit" if commit else "abort"
        if attempt.decision is not None:
            assert attempt.decision == decision
            self._count(attempt, "duplicate_decisions")
            return
        if commit:
            assert not attempt.rejected
            if attempt.owner in self.coverage:
                assert attempt.owner in self.sealed and self.admission_releasable(attempt)
            self._manifest(attempt)
            if not attempt.writes and attempt.c is None:
                self.choose(attempt)
            assert attempt.c is not None
            assert all(self._own_claim(attempt, key) for key in attempt.writes)
            assert attempt.c == attempt.s or attempt.reads.keys() <= attempt.renewed
        attempt.decision = decision
        self._count(attempt, "commit_decisions" if commit else "abort_decisions")
        self._event(attempt, "decide", decision=decision, c=attempt.c)

    def install(self, attempt, keys):
        assert attempt.decision == "commit" and attempt.c is not None
        self._manifest(attempt)
        keys = tuple(sorted(set(keys)))
        coverage = self.coverage.get(attempt.owner, set(attempt.writes))
        assert set(keys) <= coverage
        actual = set(keys).intersection(attempt.writes)
        fresh = sorted(actual - attempt.installed)
        resolved = self.resolved.get(attempt.owner, attempt.installed)
        unused = set(keys) - set(attempt.writes) - resolved
        for key in set(fresh) | unused:
            claim = self._own_claim(attempt, key)
            assert claim is not None and claim.minimum <= attempt.c
        for key in fresh:
            assert not any(version.position == attempt.c for version in self.versions[key])
            self.versions[key].append(Version(attempt.c, attempt.writes[key]))
            self.versions[key].sort(key=lambda version: version.position)
            for scope in self.key_scopes[key]:
                self.W[scope] = max(self.W[scope], attempt.c)
            attempt.installed.add(key)
        for key in set(fresh) | unused:
            self._own_claim(attempt, key).remaining.remove(key)
        if attempt.owner in self.resolved:
            self.resolved[attempt.owner].update(keys)
        self._refresh_claims(keys)
        self._count(attempt, "installed_keys", len(fresh))
        self._count(attempt, "duplicate_installed_keys", len(actual) - len(fresh))
        self._count(attempt, "unused_outputs_resolved", len(unused))
        self._event(attempt, "install", keys=keys, fresh=fresh, unused=sorted(unused))

    def release(self, attempt, keys):
        assert attempt.decision == "abort"
        keys = tuple(sorted(set(keys)))
        assert set(keys) <= self.coverage.get(attempt.owner, set(attempt.writes))
        released = []
        for key in keys:
            claim = self._own_claim(attempt, key)
            if claim is not None:
                claim.remaining.remove(key)
                released.append(key)
        self._refresh_claims(keys)
        self._count(attempt, "released_keys", len(released))
        self._event(attempt, "release", keys=released)


def database(initial, **collections):
    return PipelineStore(initial, {**{key: (key,) for key in initial}, **collections}), Admission({})


def prepared(db, admission, owner, coverage, floor=1):
    coverage = tuple(coverage)
    admission.priority.setdefault(owner, (len(admission.priority), owner))
    groups = defaultdict(list)
    for key in sorted(coverage):
        groups[key.split("/", 1)[0]].append(key)
    for node in sorted(groups):
        assert admission.request(owner, groups[node])
    attempt = db.begin_fixed(owner, (floor, owner), coverage, admission.grants, owner)
    for node in sorted(groups):
        assert db.reserve_position(attempt, groups[node])
    db.fix_position(attempt)
    for node in sorted(groups):
        assert db.publish_position(attempt, groups[node])
    assert db.admission_releasable(attempt)
    admission.release_all(owner)
    return attempt


def finish(db, attempt, writes):
    attempt.writes.update(writes)
    db.seal_values(attempt)
    db.decide(attempt, True)
    db.install(attempt, db.coverage[attempt.owner])


def finish_read(db, attempt):
    db.choose(attempt)
    db.decide(attempt, True)


def record(name, db, **detail):
    final = db.check_serial()
    assert not db.claims and not any(db.pending_by_key.values())
    return {"name": name, "serial_check": True, "final": final,
            "counters": dict(db.counters), **detail}


def probes():
    result = []
    db, gate = database({"eu/x": 0})
    first = prepared(db, gate, "first", ["eu/x"])
    second = prepared(db, gate, "second", ["eu/x"])
    assert first.c < second.c and len(db.pending_by_key["eu/x"]) == 2 and not gate.grants
    assert not db.capture(second, "eu/x", wait=True)
    assert db.capture(first, "eu/x", wait=True)
    finish(db, first, {"eu/x": first.reads["eu/x"]["eu/x"] + 1})
    assert db.capture(second, "eu/x", wait=True)
    finish(db, second, {"eu/x": second.reads["eu/x"]["eu/x"] + 1})
    assert db.head()["eu/x"] == 2
    result.append(record("same_row_two_increments", db, second_metadata_finished_before_first_computation=True))

    db, gate = database({"eu/x": 0})
    first = prepared(db, gate, "first", ["eu/x"], 10)
    second = prepared(db, gate, "second", ["eu/x"], 20)
    finish(db, second, {"eu/x": 9})  # Full-row blind overwrite, with no prior-value result.
    newer = db.begin("newer-reader", (30, "newer-reader"))
    assert db.capture(newer, "eu/x", wait=True)
    assert newer.reads["eu/x"]["eu/x"] == 9
    finish_read(db, newer)
    older = db.begin("older-reader", (15, "older-reader"))
    assert not db.capture(older, "eu/x", wait=True)
    assert db.capture(first, "eu/x", wait=True)
    finish(db, first, {"eu/x": first.reads["eu/x"]["eu/x"] + 1})
    assert db.head()["eu/x"] == 9
    assert db.capture(older, "eu/x", wait=True) and older.reads["eu/x"]["eu/x"] == 1
    finish_read(db, older)
    db.install(first, ["eu/x"])
    result.append(record("blind_overwrite_hides_earlier_only_at_newer_cuts", db,
                         old_reader_waited=True, late_older_install_preserved_newer_head=True))

    db, gate = database({"eu/x": 0})
    first = prepared(db, gate, "no-write", ["eu/x"])
    second = prepared(db, gate, "increment", ["eu/x"])
    assert not db.capture(second, "eu/x", wait=True)
    finish(db, first, {})
    assert db.capture(second, "eu/x", wait=True)
    finish(db, second, {"eu/x": second.reads["eu/x"]["eu/x"] + 1})
    assert len(db.versions["eu/x"]) == 2
    result.append(record("earlier_no_write_resolves_without_fake_version", db))

    db, gate = database({"eu/a": 1, "us/b": 1}, both=("eu/a", "us/b"))
    first = prepared(db, gate, "A", ["eu/a"], 1)
    second = prepared(db, gate, "B", ["us/b"], 2)
    assert db.capture(first, "both", wait=True)
    assert not db.capture(second, "both", wait=True)
    value = first.reads["both"]
    finish(db, first, {"eu/a": 0} if sum(value.values()) > 1 else {})
    assert db.capture(second, "both", wait=True)
    value = second.reads["both"]
    finish(db, second, {"us/b": 0} if sum(value.values()) > 1 else {})
    assert sum(db.head().values()) == 1 and not second.writes
    result.append(record("cross_output_write_skew_becomes_business_no_write", db))

    db, gate = database({"eu/a": 1, "us/b": 1, "eu/outside": 0}, both=("eu/a", "us/b"))
    broad = prepared(db, gate, "broad", ["eu/a", "us/b"], 1)
    narrow = prepared(db, gate, "narrow", ["eu/a"], 2)
    outside = prepared(db, gate, "outside", ["eu/outside"], 3)
    finish(db, outside, {"eu/outside": 1})
    assert not db.capture(narrow, "eu/a", wait=True)
    reader = db.begin("report", (10, "report"))
    assert not db.capture(reader, "both", wait=True)
    assert db.capture(broad, "both", wait=True)
    finish(db, broad, {key: value + 1 for key, value in broad.reads["both"].items()})
    assert db.capture(narrow, "eu/a", wait=True)
    finish(db, narrow, {"eu/a": narrow.reads["eu/a"]["eu/a"] + 1})
    assert db.capture(reader, "both", wait=True)
    finish_read(db, reader)
    result.append(record("broad_envelope_pipelines_narrow_metadata_not_dependent_reads", db,
                         outside_completed_before_broad=True))

    db, gate = database({"eu/x": 0, "us/y": 0})
    first = prepared(db, gate, "old", ["eu/x", "us/y"], 1)
    second = prepared(db, gate, "new", ["eu/x"], 2)
    db.decide(first, False)
    db.release(first, ["eu/x"])
    assert db.claims["eu/x"].owner == second.owner
    finish(db, second, {"eu/x": 5})
    db.release(first, ["us/y"])
    db.release(first, ["eu/x", "us/y"])
    result.append(record("abort_releases_only_its_pending_versions", db))

    # An early-release implementation using another writer's provisional
    # minimum as a final position can allow a newer read to skip a write that
    # subsequently moves into the read's visible interval.
    minimum_A, exact_B, final_A, read_Q = (10, "A"), (11, "B"), (21, "A"), (30, "Q")
    assert minimum_A < exact_B < final_A < read_Q
    observed_Q = 9  # B's blind value, returned while A was incorrectly shadowed.
    serial_state = 0
    for _, value in sorted(((exact_B, 9), (final_A, 1))):
        serial_state = value
    assert observed_Q != serial_state
    result.append({"name": "provisional_floor_is_not_an_exact_shadowable_version",
                   "unsafe_variant_rejected": True, "serial_expected": serial_state,
                   "incorrect_observation": observed_Q,
                   "positions": {"minimum_A": minimum_A, "B": exact_B, "final_A": final_A, "Q": read_Q}})
    return result


def run_probes():
    root = Path(__file__).resolve().parent
    return {"source_sha256": {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
                              for name in ("position_pipeline.py", "check_position_pipeline.py",
                                           "fixed_execution.py", "certification.py", "write_admission.py")},
            "scenarios": probes(),
            "limits": ["Pure semantic core; admission release delivery and scheduling are caller-owned.",
                       "Exact-c publication at every effect authority precedes any admission release.",
                       "Only complete logical-identity versions may shadow older pending writes.",
                       "All pending versions and their needed predecessor history are retained; no memory bound or GC protocol.",
                       "A bounded implementation must reserve pending-version and continuation capacity before publishing any provisional read blockers.",
                       "No throughput, finite-latency, recovery or arbitrary-index-authority proof."]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = run_probes()
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    for scenario in result["scenarios"]:
        print("PASS", scenario["name"])
