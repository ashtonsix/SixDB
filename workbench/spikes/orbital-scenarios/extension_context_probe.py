#!/usr/bin/env python3
"""Bounded local extension-context histories; no VM or verification protocol.

Uses the retained fixed-position semantic core unchanged. The small adapter
captures private observations after a declared shard-wide read floor. A negative
control deliberately omits that floor and is rejected by the serial oracle.
"""

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path

from fixed_execution import FixedStore


def store():
    initial = dict.fromkeys(("x", "y", "z", "w"), 0)
    return FixedStore(initial, {**{key: (key,) for key in initial},
                                "shard": tuple(initial)})


def prepare(db, owner, output, floor):
    attempt = db.begin_fixed(owner, (floor, owner), (output,),
                             {output: owner}, owner)
    db.reserve_position(attempt, (output,))
    db.fix_position(attempt)
    db.publish_position(attempt, (output,))
    return attempt


def private_read(db, attempt, scope, *, omit_floor_control=False):
    """Proposed adapter: no shared per-query registration after context setup.

    Attempt observations are private candidate evidence until final acceptance.
    Relevant earlier promises still govern which versions are available.
    """
    assert set(db.scopes[scope]) <= set(db.scopes["shard"])
    if not omit_floor_control:
        assert db.R["shard"] >= attempt.c
    if db._blocking(attempt, scope, attempt.c):
        return None
    values = db.snapshot(scope, attempt.c)
    overlay = {key: attempt.writes[key] for key in values if key in attempt.writes}
    values.update(overlay)
    attempt.reads[scope] = dict(values)
    attempt.observations.append({"scope": scope, "position": attempt.c,
                                 "values": dict(values), "overlay": overlay})
    return values


def finish(db, attempt, values):
    # Assumes a complete, accepted outcome; does not establish hash agreement.
    attempt.writes.update(values)
    db.seal_values(attempt)
    db.decide(attempt, True)
    db.install(attempt, tuple(db.coverage[attempt.owner]))


def shared_metadata(db):
    # Excludes each execution's private observations/bytes, not shared bounds.
    return {"R": dict(db.R), "W": dict(db.W),
            "versions": deepcopy(db.versions),
            "promises": deepcopy(db.promises),
            "coverage": deepcopy(db.coverage),
            "published": deepcopy(db.published)}


def run():
    cases = []
    for protected in (False, True):
        db = store()
        t = prepare(db, "T", "y", 20)
        if protected:
            db.register_read(t, "shard")
        assert private_read(db, t, "x", omit_floor_control=not protected) == {"x": 0}
        writer = prepare(db, "W", "x", 10)
        finish(db, writer, {"x": 1})  # Completes while T is still pending.
        assert t.decision is None
        finish(db, t, {"y": 1})
        serial = True
        try:
            db.check_serial()
        except AssertionError:
            serial = False
        assert serial == protected
        assert (writer.c > t.c) == protected
        cases.append({"name": "floor_preserves_private_snapshot" if protected
                      else "negative_control_without_floor_breaks_snapshot",
                      "T_position": t.c, "W_position": writer.c,
                      "serial_oracle_accepts": serial,
                      "writer_completed_before_T": True})

    db = store()
    earlier = prepare(db, "Earlier", "z", 10)
    t = prepare(db, "T", "y", 20)
    db.register_read(t, "shard")  # Does not capture or wait on the whole shard.
    assert db._blocking(t, "shard", t.c) == [earlier.owner]
    assert private_read(db, t, "x") == {"x": 0}
    assert private_read(db, t, "z") is None
    writer = prepare(db, "W", "w", 1)
    finish(db, writer, {"w": 1})
    assert earlier.decision is None and t.decision is None
    db.check_serial()
    cases.append({"name": "registering_floor_is_not_reading_whole_shard",
                  "x_read": "ready", "z_read": "waits for Earlier",
                  "unrelated_writer": "committed while Earlier and T pending"})

    template = store()
    t = prepare(template, "T", "y", 20)
    template.register_read(t, "shard")
    executions = []
    transcripts = []
    for scope in ("x", "z"):
        db = deepcopy(template)
        local_t = db.attempts["T"]
        values = private_read(db, local_t, scope)
        transcripts.append({"request": scope, "response": values, "result": "OK"})
        executions.append(db)
    assert transcripts[0]["result"] == transcripts[1]["result"]
    assert transcripts[0] != transcripts[1]
    assert shared_metadata(executions[0]) == shared_metadata(executions[1])
    positions = []
    for db in executions:
        writer = prepare(db, "W", "x", 10)
        finish(db, writer, {"x": 1})
        positions.append(writer.c)
        local_t = db.attempts["T"]
        # Mismatch and agreed abort are supplied facts, not a tested protocol.
        db.decide(local_t, False)
        db.release(local_t, tuple(db.coverage[local_t.owner]))
        assert db.R["shard"] == local_t.c
        db.check_serial()
    assert positions[0] == positions[1]
    assert shared_metadata(executions[0]) == shared_metadata(executions[1])
    cases.append({"name": "different_private_queries_do_not_diverge_shared_bounds",
                  "final_bytes_equal": True, "complete_transcripts_equal": False,
                  "writer_positions": positions, "T_outcome": "assumed agreed abort",
                  "floor_retained_after_abort": True})

    root = Path(__file__).parent
    return {"cases": cases, "passed": True,
            "source_sha256": {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
                              for name in ("extension_context_probe.py", "fixed_execution.py",
                                           "certification.py")},
            "limits": ["Four authored semantic histories, not exhaustive schedules or a proof.",
                       "Fixed local key universe and assumed complete effect/observation coverage.",
                       "No VM, hash verifier, consumer-set protocol, epoch admission or recovery.",
                       "Comparison and final decisions are supplied facts.",
                       "No measurements, retention budget or resource scheduling."]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = run()
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"{len(result['cases'])} authored local-context histories passed; "
          "includes an expected serial-oracle rejection. No verification protocol or timing model.")


if __name__ == "__main__":
    main()
