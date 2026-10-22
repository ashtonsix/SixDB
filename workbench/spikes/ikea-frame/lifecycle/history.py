"""Logical mutation histories for Frame research, not a physical lifecycle model."""

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import random
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "execution"))
import workload


def successor(basis, snapshot, changes):
    """Publish ordered field replacements; older logical images stay untouched."""
    workload.validate(basis)
    if not isinstance(snapshot, str) or not snapshot or snapshot == basis["snapshot"]:
        raise ValueError("a new logical snapshot label is required")
    result = deepcopy(basis)
    parts = {part["part_id"]: part for part in result["parts"]}
    rows = {name: {row["position"]: row for row in part["rows"]}
            for name, part in parts.items()}
    for change in changes:
        name, position = change["part_id"], change["position"]
        field, value = change["field"], change["value"]
        if name not in rows:
            raise ValueError("replacement names an unknown part")
        workload.unsigned(position, parts[name]["position_limit"] - 1, "position")
        if position not in rows[name]:
            raise ValueError("replacement must name an occupied original position")
        if field not in ("x", "category"):
            raise ValueError("unknown logical field")
        if value is not None:
            workload.unsigned(value, workload.U32 if field == "x" else workload.U64, field)
        rows[name][position][field] = value
        if field == "category" and value is not None and value not in parts[name]["dictionary"]:
            # Logical enumeration for the oracle/transport, not a prescription
            # to extend an encoded dictionary instead of using an override.
            parts[name]["dictionary"].append(value)
            parts[name]["dictionary_edition"] = f"{name}-{snapshot}"
    result["snapshot"] = snapshot
    for query in result["queries"]:
        query["snapshot"] = snapshot
    workload.validate(result)
    return result


def answer_digest(document):
    answers = workload.oracle(document)
    encoded = json.dumps(answers, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def generate(seed=713, rows=256, batches=4, updates=128):
    if rows < 1 or batches < 1 or updates < 1:
        raise ValueError("positive rows, batches and updates required")
    rng = random.Random(seed)
    basis = workload.generate(seed, parts=2, rows=rows, categories=12)
    for part in basis["parts"]:
        for row in part["rows"]:
            if row["x"] is not None:
                row["x"] = ((row["category"] or 0) & 255) * 16 + row["position"] % 16
    for query in basis["queries"]:
        if query["query_id"] == "scattered":
            query["min_x"] = 128
    versions = [basis]
    events = [{"action": "hold", "reader": "old", "snapshot": basis["snapshot"]}]
    current = basis
    for phase in ("repeated-hot", "dispersed", "width-novelty"):
        for batch in range(batches):
            changes = []
            for _ in range(updates):
                part = rng.choice(current["parts"])
                pool = part["rows"][:min(64, rows)] if phase == "repeated-hot" else part["rows"]
                row = rng.choice(pool)
                value = rng.getrandbits(32) if phase == "width-novelty" else rng.randrange(4096)
                changes.append({"part_id": part["part_id"], "position": row["position"],
                                "field": "x", "value": None if rng.randrange(17) == 0 else value})
            label = f"{phase}-{batch}"
            next_image = successor(current, label, changes)
            events.append({"action": "publish", "basis": current["snapshot"],
                           "snapshot": label, "phase": phase, "changes": changes})
            current = next_image
            versions.append(current)
            events.append({"action": "read", "snapshot": label,
                           "answer_sha256": answer_digest(current)})
        events.append({"action": "conversion", "snapshot": current["snapshot"],
                       "choices": ["fold-changed-blocks", "thaw", "re-encode-group"],
                       "answer_sha256": answer_digest(current)})
        events.append({"action": "read", "reader": "old", "snapshot": basis["snapshot"],
                       "answer_sha256": answer_digest(basis)})
    events.append({"action": "release", "reader": "old"})
    return versions, events


def check():
    # Literal expected answers are independent of dictionaries/code ranks.
    basis = {"format": workload.FORMAT, "kind": "authored-synthetic", "snapshot": "basis-0",
             "parts": [
                 {"part_id": "a", "dictionary_edition": "a-0", "position_limit": 65536,
                  "dictionary": [10, 20], "rows": [
                      {"position": 0, "category": 10, "x": 3},
                      {"position": 65535, "category": 20, "x": None}]},
                 {"part_id": "b", "dictionary_edition": "b-0", "position_limit": 4,
                  "dictionary": [20, 10], "rows": [
                      {"position": 0, "category": 10, "x": 7},
                      {"position": 2, "category": None, "x": 5}]}],
             "queries": [workload.query("all", {"a": None, "b": None})]}
    original = deepcopy(basis)
    old_groups = [{"key": None, "count": 1, "sum": 5}, {"key": 10, "count": 2, "sum": 10}]
    assert workload.oracle(basis)[0]["groups"] == old_groups
    next_image = successor(basis, "next", [
        {"part_id": "a", "position": 0, "field": "x", "value": 11},
        {"part_id": "b", "position": 0, "field": "category", "value": 42},
        {"part_id": "b", "position": 2, "field": "x", "value": None},
        {"part_id": "a", "position": 65535, "field": "x", "value": 0}])
    assert workload.oracle(next_image)[0]["groups"] == [
        {"key": 10, "count": 1, "sum": 11}, {"key": 20, "count": 1, "sum": 0},
        {"key": 42, "count": 1, "sum": 7}]
    assert basis == original and workload.oracle(basis)[0]["groups"] == old_groups
    assert next_image["parts"][0]["dictionary_edition"] == "a-0"
    assert next_image["parts"][1]["dictionary_edition"] != "b-0"
    repeated = successor(next_image, "repeat", [
        {"part_id": "a", "position": 0, "field": "x", "value": 0},
        {"part_id": "a", "position": 0, "field": "x", "value": 9}])
    assert workload.oracle(repeated)[0]["groups"][0] == {"key": 10, "count": 1, "sum": 9}
    for bad in (workload.U32 + 1, -1, True):
        try:
            successor(next_image, "refused", [
                {"part_id": "a", "position": 0, "field": "x", "value": 1},
                {"part_id": "a", "position": 65535, "field": "x", "value": bad}])
        except ValueError:
            pass
        else:
            raise AssertionError("invalid batch was accepted")
        assert workload.oracle(next_image)[0]["groups"][0]["sum"] == 11
    for seed in (713, 991, 1729):
        versions, events = generate(seed, rows=17, batches=2, updates=19)
        assert len(versions) == 7
        assert len({image["snapshot"] for image in versions}) == len(versions)
        by_name = {image["snapshot"]: image for image in versions}
        for event in events:
            if event["action"] == "publish":
                replayed = successor(by_name[event["basis"]], event["snapshot"], event["changes"])
                assert replayed == by_name[event["snapshot"]]
            if "answer_sha256" in event:
                assert answer_digest(by_name[event["snapshot"]]) == event["answer_sha256"]
    print("Logical histories passed: ordered replacements, NULL/zero, dictionary novelty, old images and refusal.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("check")
    gen = commands.add_parser("generate")
    gen.add_argument("--output", type=Path, required=True)
    for key, default in (("seed", 713), ("rows", 256), ("batches", 4), ("updates", 128)):
        gen.add_argument(f"--{key}", type=int, default=default)
    args = parser.parse_args()
    if args.command == "check":
        check()
        return
    versions, events = generate(args.seed, args.rows, args.batches, args.updates)
    args.output.mkdir(parents=True, exist_ok=True)
    for document in versions:
        target = args.output / (document["snapshot"] + ".json")
        target.write_text(json.dumps(document, indent=2) + "\n")
    manifest = {"kind": "logical-history-not-a-cost-model", "seed": args.seed,
                "snapshots": [image["snapshot"] + ".json" for image in versions], "events": events}
    (args.output / "history.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Wrote {len(versions)} logical snapshots and {len(events)} events; no physical policy or timing.")


if __name__ == "__main__":
    main()
