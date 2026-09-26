#!/usr/bin/env python3
"""Semantic checks for fixed-position execution, not a network/progress proof.

These tests use real output admission, explicit application results, and the
independent invariant-program replay. In-flight messages after abort delivery
remain a caller-fencing obligation; the tests do not grant them new authority.
"""

from itertools import permutations
import unittest

from certification import ZERO
from fixed_execution import FixedStore
from invariant_probe import evaluate, replay, workload
from write_admission import Admission


def database(initial, extra_scopes=None):
    scopes = {key: (key,) for key in initial}
    scopes.update(extra_scopes or {})
    return FixedStore(initial, scopes), Admission({})


def groups(keys):
    owners = sorted({key.split("/", 1)[0] for key in keys})
    return [tuple(sorted(key for key in keys if key.split("/", 1)[0] == owner))
            for owner in owners]


def admit(gates, owner, keys):
    if owner not in gates.priority:
        gates.priority[owner] = (len(gates.priority), owner)
    return gates.request(owner, keys)


def frozen(db, gates, owner, keys, floor=1, publish=True):
    for part in groups(keys):
        assert admit(gates, owner, part)
    attempt = db.begin_fixed(owner, (floor, owner), keys, gates.grants, owner)
    for part in groups(keys):
        assert db.reserve_position(attempt, part)
    db.fix_position(attempt)
    if publish:
        for part in groups(keys):
            assert db.publish_position(attempt, part)
    return attempt


def finish(db, gates, attempt, writes):
    attempt.writes.update(writes)
    db.seal_values(attempt)
    db.decide(attempt, True)
    for part in groups(db.coverage[attempt.owner]):
        db.install(attempt, part)
        gates.release(attempt.owner, part)


def finish_reader(db, attempt):
    db.choose(attempt)
    db.decide(attempt, True)


def application_record(attempt, actor, program, writes, result):
    return {"owner": attempt.owner, "actor": actor, "position": attempt.c,
            "program": dict(program), "writes": dict(writes), "result": dict(result),
            "reads": {key: dict(values) for key, values in attempt.reads.items()}}


class FixedExecutionTests(unittest.TestCase):
    def test_conditional_disjoint_outputs_in_sixteen_schedules(self):
        """Metadata/capture visit order cannot turn either invariant into skew."""
        schedules = 0
        for kind in ("on_call", "quota"):
            for lower in ("A", "B"):
                for registration in permutations(("A", "B")):
                    for visit in permutations(("A", "B")):
                        with self.subTest(kind=kind, lower=lower,
                                          registration=registration, visit=visit):
                            initial, programs = workload(kind)
                            db, gates = database(initial)
                            attempts = {}
                            for actor in registration:
                                attempts[actor] = frozen(
                                    db, gates, actor, [programs[actor]["target"]],
                                    floor=1 if actor == lower else 2)
                            higher = "B" if lower == "A" else "A"
                            self.assertLess(attempts[lower].c, attempts[higher].c)
                            for actor in visit:
                                for key in programs[actor]["read_keys"]:
                                    if not db.capture(attempts[actor], key, wait=True):
                                        self.assertEqual(actor, higher)
                                        break
                            self.assertEqual(set(attempts[lower].reads), set(initial))
                            self.assertLess(len(attempts[higher].reads), len(initial))
                            records = []
                            for actor in (lower, higher):
                                attempt = attempts[actor]
                                for key in programs[actor]["read_keys"]:
                                    if key not in attempt.reads:
                                        self.assertTrue(db.capture(attempt, key, wait=True))
                                values = {key: value for observed in attempt.reads.values()
                                          for key, value in observed.items()}
                                writes, result = evaluate(programs[actor], values)
                                self.assertEqual(result["accepted"], actor == lower)
                                finish(db, gates, attempt, writes)
                                records.append(application_record(
                                    attempt, actor, programs[actor], writes, result))
                            self.assertEqual(replay(kind, initial, records), db.head())
                            self.assertEqual(db.check_serial(), db.head())
                            rejected_output = programs[higher]["target"]
                            self.assertEqual(len(db.versions[rejected_output]), 1)
                            self.assertEqual(db.W[rejected_output], ZERO)
                            self.assertFalse(db.counters["renew_calls"])
                            self.assertFalse(db.counters["abort_decisions"])
                            self.assertFalse(db.claims)
                            self.assertFalse(gates.grants)
                            schedules += 1
        self.assertEqual(schedules, 16)

    def test_partial_gate_owner_does_not_block_holder_reads(self):
        db, gates = database({"eu/x": 1, "us/y": 0})
        holder = frozen(db, gates, "H", ["us/y"], floor=10)
        self.assertTrue(admit(gates, "P", ["eu/x"]))
        self.assertFalse(admit(gates, "P", ["us/y"]))
        with self.assertRaisesRegex(AssertionError, "all output gates"):
            db.begin_fixed("P", (1, "P"), ["eu/x", "us/y"], gates.grants, "P")
        self.assertNotIn("P", db.attempts)
        self.assertNotIn("eu/x", db.claims)
        self.assertTrue(db.capture(holder, "eu/x", wait=True))
        finish(db, gates, holder, {"us/y": holder.reads["eu/x"]["eu/x"] + 1})
        self.assertTrue(admit(gates, "P", ["us/y"]))
        later = db.begin_fixed("P", (1, "P"), ["eu/x", "us/y"], gates.grants, "P")
        for part in groups(db.coverage["P"]):
            self.assertTrue(db.reserve_position(later, part))
        db.fix_position(later)
        self.assertGreater(later.c, holder.c)
        for part in groups(db.coverage["P"]):
            self.assertTrue(db.publish_position(later, part))
        for key in ("eu/x", "us/y"):
            self.assertTrue(db.capture(later, key, wait=True))
        finish(db, gates, later, {key: later.reads[key][key] + 1 for key in later.reads})
        self.assertEqual(db.check_serial(), {"eu/x": 2, "us/y": 3})
        self.assertFalse(db.counters["renew_calls"])

    def test_waiting_reader_can_raise_a_not_yet_reserved_participant_bound(self):
        initial = {"eu/x": 0, "us/y": 0}
        db, gates = database(initial, {"both": tuple(initial)})
        for part in groups(initial):
            self.assertTrue(admit(gates, "T", part))
        attempt = db.begin_fixed("T", (1, "T"), initial, gates.grants, "T")
        self.assertTrue(db.reserve_position(attempt, ["eu/x"]))
        original_bound = db.claims["eu/x"].minimum
        reader = db.begin("R", (20, "R"))
        self.assertFalse(db.capture(reader, "both", wait=True))
        self.assertEqual(db.R["both"], reader.s)
        self.assertTrue(db.reserve_position(attempt, ["us/y"]))
        position = db.fix_position(attempt)
        self.assertLess(original_bound, position)
        self.assertGreater(position, reader.s)
        with self.assertRaises(AssertionError):
            db.capture(attempt, "eu/x", wait=True)
        self.assertTrue(db.publish_position(attempt, ["eu/x"]))
        self.assertEqual(db.claims["eu/x"].minimum, position)
        self.assertTrue(db.capture(reader, "both", wait=True))
        self.assertEqual(reader.reads["both"], initial)
        finish_reader(db, reader)
        # Data execution still needs every publication, even though this reader
        # could finish through the relaxed bound at the first participant.
        with self.assertRaises(AssertionError):
            db.capture(attempt, "eu/x", wait=True)
        self.assertTrue(db.publish_position(attempt, ["us/y"]))
        self.assertTrue(db.publish_position(attempt, ["eu/x"]))
        self.assertEqual(db.fix_position(attempt), position)
        self.assertTrue(db.capture(attempt, "both", wait=True))
        finish(db, gates, attempt, {"eu/x": 1, "us/y": 1})
        self.assertEqual(db.check_serial(), {"eu/x": 1, "us/y": 1})
        self.assertFalse(db.counters["renew_calls"])

    def test_metadata_publication_can_remove_an_apparent_data_wait_cycle(self):
        initial = {"eu/a": 0, "us/aux": 0, "us/b": 0}
        db, gates = database(initial)
        observer = db.begin("observer", (40, "observer"))
        self.assertTrue(db.capture(observer, "us/aux"))
        finish_reader(db, observer)
        high = frozen(db, gates, "A", ["eu/a", "us/aux"], publish=False)
        middle = frozen(db, gates, "B", ["us/b"], floor=20)
        self.assertLess(db.claims["eu/a"].minimum, middle.c)
        self.assertGreater(high.c, middle.c)
        self.assertFalse(db.capture(middle, "eu/a", wait=True))
        with self.assertRaises(AssertionError):
            db.capture(high, "us/b", wait=True)
        # This publication must remain eligible while data programs are waiting.
        self.assertTrue(db.publish_position(high, ["eu/a"]))
        self.assertTrue(db.capture(middle, "eu/a", wait=True))
        finish(db, gates, middle, {"us/b": middle.reads["eu/a"]["eu/a"] + 1})
        self.assertTrue(db.publish_position(high, ["us/aux"]))
        self.assertTrue(db.capture(high, "us/b", wait=True))
        finish(db, gates, high, {"eu/a": high.reads["us/b"]["us/b"], "us/aux": 9})
        self.assertEqual(db.check_serial(), {"eu/a": 1, "us/aux": 9, "us/b": 1})

    def test_waiting_broad_reader_excludes_future_low_position_writers(self):
        initial = {"eu/a": 0, "us/b": 0}
        db, gates = database(initial, {"both": tuple(initial)})
        earlier = frozen(db, gates, "earlier", ["eu/a"])
        reader = db.begin("reader", (20, "reader"))
        self.assertFalse(db.capture(reader, "both", wait=True))
        self.assertEqual(db.R["both"], reader.s)
        future = frozen(db, gates, "future", ["us/b"], floor=1)
        self.assertGreater(future.c, reader.s)
        finish(db, gates, future, {"us/b": 9})
        finish(db, gates, earlier, {"eu/a": 1})
        self.assertTrue(db.capture(reader, "both", wait=True))
        self.assertEqual(reader.reads["both"], {"eu/a": 1, "us/b": 0})
        finish_reader(db, reader)
        self.assertEqual(db.check_serial(), {"eu/a": 1, "us/b": 9})

    def test_future_writer_respects_successful_read_while_reader_computes(self):
        initial = {"eu/source": 1, "us/result": 0}
        db, gates = database(initial)
        reader = frozen(db, gates, "reader", ["us/result"], floor=20)
        # The future writer owns its gate but has registered no pending version.
        self.assertTrue(admit(gates, "writer", ["eu/source"]))
        writer = db.begin_fixed("writer", (1, "writer"), ["eu/source"], gates.grants, "writer")
        self.assertTrue(db.capture(reader, "eu/source", wait=True))
        self.assertTrue(db.reserve_position(writer, ["eu/source"]))
        db.fix_position(writer)
        self.assertGreater(writer.c, reader.c)
        self.assertTrue(db.publish_position(writer, ["eu/source"]))
        finish(db, gates, writer, {"eu/source": 9})
        finish(db, gates, reader, {"us/result": reader.reads["eu/source"]["eu/source"]})
        self.assertEqual(db.check_serial(), {"eu/source": 9, "us/result": 1})
        self.assertFalse(db.counters["renew_calls"])

    def test_partial_installation_preserves_common_cuts_and_disjoint_progress(self):
        initial = {"eu/x": 0, "us/y": 0, "eu/unrelated": 0}
        db, gates = database(initial, {"pair": ("eu/x", "us/y")})
        writer = frozen(db, gates, "T", ["eu/x", "us/y"], floor=10)
        writer.writes.update({"eu/x": 1, "us/y": 1})
        db.seal_values(writer)
        db.decide(writer, True)
        db.install(writer, ["eu/x"])
        gates.release("T", ["eu/x"])

        historical = db.begin("historical", (5, "historical"))
        self.assertTrue(db.capture(historical, "pair", wait=True))
        self.assertEqual(historical.reads["pair"], {"eu/x": 0, "us/y": 0})
        finish_reader(db, historical)

        report = db.begin("report", (20, "report"))
        self.assertTrue(db.capture(report, "eu/x", wait=True))
        self.assertEqual(report.reads["eu/x"], {"eu/x": 1})
        self.assertFalse(db.capture(report, "us/y", wait=True))
        self.assertNotIn("us/y", report.reads)
        self.assertIsNone(report.decision)
        unrelated = frozen(db, gates, "unrelated", ["eu/unrelated"], floor=30)
        finish(db, gates, unrelated, {"eu/unrelated": 7})
        self.assertEqual(db.head()["eu/unrelated"], 7)
        self.assertEqual(db.claims["us/y"].owner, "T")

        db.install(writer, ["us/y"])
        gates.release("T", ["us/y"])
        self.assertTrue(db.capture(report, "us/y", wait=True))
        self.assertEqual(report.reads, {"eu/x": {"eu/x": 1}, "us/y": {"us/y": 1}})
        finish_reader(db, report)
        self.assertEqual(db.check_serial(), {"eu/x": 1, "us/y": 1, "eu/unrelated": 7})

    def test_unused_coverage_resolves_without_fake_versions_or_deleting_new_claims(self):
        initial = {"eu/x": 0, "us/y": 0}
        db, gates = database(initial)
        attempt = frozen(db, gates, "T", initial)
        attempt.writes["eu/x"] = 1
        db.seal_values(attempt)
        db.decide(attempt, True)
        db.install(attempt, ["eu/x"])
        gates.release("T", ["eu/x"])
        reader = db.begin("R", (10, "R"))
        self.assertFalse(db.capture(reader, "us/y", wait=True))
        with self.assertRaisesRegex(AssertionError, "unused output promises"):
            db.check_serial()
        db.install(attempt, ["us/y"])
        gates.release("T", ["us/y"])
        self.assertEqual(len(db.versions["us/y"]), 1)
        self.assertEqual(db.W["us/y"], ZERO)
        self.assertTrue(db.capture(reader, "us/y", wait=True))
        finish_reader(db, reader)
        later = frozen(db, gates, "U", ["us/y"], floor=1)
        db.install(attempt, ["us/y"])  # A repeated old resolution cannot release U.
        self.assertEqual(db.claims["us/y"].owner, "U")
        finish(db, gates, later, {"us/y": 2})
        self.assertEqual(db.check_serial(), {"eu/x": 1, "us/y": 2})

    def test_abort_before_values_and_late_publication_do_not_resurrect_data(self):
        initial = {"eu/x": 0, "us/y": 0}
        db, gates = database(initial)
        attempt = frozen(db, gates, "T", initial, floor=5, publish=False)
        self.assertTrue(db.publish_position(attempt, ["eu/x"]))
        db.decide(attempt, False)
        db.release(attempt, ["eu/x"])
        gates.release("T", ["eu/x"])
        self.assertTrue(db.publish_position(attempt, ["us/y"], in_flight=True))
        self.assertEqual(attempt.decision, "abort")
        with self.assertRaises(AssertionError):
            db.seal_values(attempt)
        with self.assertRaises(AssertionError):
            db.decide(attempt, True)
        db.release(attempt, ["us/y"])
        gates.release("T", ["us/y"])
        self.assertFalse(db.claims)
        later = frozen(db, gates, "U", ["eu/x"])
        # Even if this redundant publication reaches the core, it only updates
        # T's retained publication record, never the live claim now owned by U.
        self.assertTrue(db.publish_position(attempt, ["eu/x"], in_flight=True))
        db.release(attempt, ["eu/x"])
        self.assertEqual(db.claims["eu/x"].owner, "U")
        self.assertEqual(db.head(), initial)
        self.assertEqual(db.W, dict.fromkeys(db.scopes, ZERO))
        finish(db, gates, later, {"eu/x": 2})
        self.assertEqual(db.check_serial(), {"eu/x": 2, "us/y": 0})

    def test_caller_fences_an_unreserved_group_after_abort_delivery(self):
        db, gates = database({"eu/x": 0})
        self.assertTrue(admit(gates, "T", ["eu/x"]))
        attempt = db.begin_fixed("T", (1, "T"), ["eu/x"], gates.grants, "T")
        db.decide(attempt, False)
        db.release(attempt, ["eu/x"])
        gates.release_all("T")
        aborted_participants = {"eu"}
        # A caller must not invoke reserve_position(in_flight=True) after this
        # participant has received abort: the core has no delivery tombstone.
        delivered = False
        if "eu" not in aborted_participants:
            delivered = db.reserve_position(attempt, ["eu/x"], in_flight=True)
        self.assertFalse(delivered)
        self.assertFalse(db.claims)
        self.assertEqual(db.check_serial(), {"eu/x": 0})

    def test_committed_duplicates_preserve_original_bound_and_exact_publication(self):
        db, gates = database({"eu/x": 0, "us/y": 0})
        observer = db.begin("observer", (20, "observer"))
        self.assertTrue(db.capture(observer, "us/y"))
        finish_reader(db, observer)
        attempt = frozen(db, gates, "T", ["eu/x", "us/y"], publish=False)
        original = db.reservation_bounds["T"][("eu/x",)]
        self.assertLess(original, attempt.c)
        for part in groups(db.coverage["T"]):
            self.assertTrue(db.publish_position(attempt, part))
        self.assertTrue(db.reserve_position(attempt, ["eu/x"]))
        self.assertEqual(db.reservation_bounds["T"][("eu/x",)], original)
        self.assertEqual(db.claims["eu/x"].minimum, attempt.c)
        finish(db, gates, attempt, {"eu/x": 1})
        before = db.head(), dict(db.W), tuple(map(len, db.versions.values()))
        for part in groups(db.coverage["T"]):
            self.assertTrue(db.reserve_position(attempt, part, in_flight=True))
            self.assertTrue(db.publish_position(attempt, part, in_flight=True))
        self.assertEqual(db.reservation_bounds["T"][("eu/x",)], original)
        self.assertFalse(db.claims)
        self.assertEqual((db.head(), dict(db.W), tuple(map(len, db.versions.values()))), before)
        self.assertEqual(db.check_serial(), {"eu/x": 1, "us/y": 0})


if __name__ == "__main__":
    unittest.main(verbosity=2)
