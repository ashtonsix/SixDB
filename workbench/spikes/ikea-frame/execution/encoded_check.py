"""Check/time complete encoded consumers against the independent logical oracle."""
import argparse
import copy
import json
from pathlib import Path
import random
import subprocess

import workload as w
from check import coarse_example

PROGRAMS = {"opaque-complete", "rich-prepared", "decode-scratch"}


def evaluate(binary, document, trials=0):
    expected = {q["query_id"]: q["groups"] for q in w.oracle(document)}
    result = subprocess.run([str(binary), str(trials)], input=w.transport(document),
                            text=True, capture_output=True, timeout=180, check=True)
    records = [json.loads(line) for line in result.stdout.splitlines()]
    assert records and records[0]["type"] == "material"
    seen = set()
    for record in records[1:]:
        key = record["program"], record["query_id"]
        assert key not in seen and key[0] in PROGRAMS
        seen.add(key)
        assert record["groups"] == expected[key[1]], (key, record["groups"], expected[key[1]])
        assert len(record["whole_query_warm_ns"]) == trials
        if key[1].endswith("/coarse-none"):
            assert record["work"]["blocks"] == 0
            assert record["work"]["decoded_values"] == 0
            assert record["work"]["hash_updates"] == 0
    assert seen == {(p, q) for p in PROGRAMS for q in expected}
    return records


def kinds_example():
    rng = random.Random(713)
    patched = [100 + i % 3 for i in range(256)]
    patched[73], patched[211] = w.U32, 0
    values = ([7] * 256 + [3] * 100 + [9] * 156 +
              [rng.getrandbits(32) for _ in range(256)] +
              [1_000_000 + 1000 * i + i % 7 for i in range(256)] +
              [3_000_000 - 1000 * i + i % 5 for i in range(256)] + patched)
    selection = {"p": None}
    document = {"format": w.FORMAT, "kind": "authored-synthetic", "snapshot": "basis-0",
            "parts": [{"part_id": "p", "dictionary_edition": "p-d0", "position_limit": len(values),
                       "dictionary": [w.U64], "rows": [
                           {"position": i, "category": w.U64, "x": x} for i, x in enumerate(values)]}],
            "queries": [w.query("all", selection),
                        w.query("selected", {"p": list(range(0, len(values), 3))}),
                        w.query("patch-selected", {"p": [1280, 1353, 1491]}),
                        w.query("threshold", selection, min_x=1_100_000),
                        w.query("absent", selection, min_x=w.U32 + 1)]}
    document["parts"][0]["rows"][1299]["x"] = None
    document["parts"][0]["rows"][1300]["category"] = None
    return document


def checks(binary):
    documents = [w.example(), coarse_example(), w.generate(713, rows=17, parts=2), kinds_example()]
    recoded = copy.deepcopy(documents[0])
    for part in recoded["parts"]:
        part["dictionary"].reverse()
        part["dictionary_edition"] += "-recoded"
    documents.append(recoded)
    full = {"format": w.FORMAT, "kind": "authored-synthetic", "snapshot": "basis-0",
            "parts": [{"part_id": "full", "dictionary_edition": "full-d0", "position_limit": 65536,
                       "dictionary": [w.U64], "rows": [
                           {"position": p, "category": w.U64, "x": w.U32} for p in range(65536)]}],
            "queries": [w.query("full", {"full": None})]}
    assert w.oracle(full)[0]["groups"] == [{"key": w.U64, "count": 65536, "sum": 281474976645120}]
    documents.append(full)
    documents.append({"format": w.FORMAT, "kind": "authored-synthetic", "snapshot": "basis-0",
                      "parts": [{"part_id": "empty", "dictionary_edition": "empty-d0", "position_limit": 0,
                                 "dictionary": [], "rows": []},
                                {"part_id": "null", "dictionary_edition": "null-d0", "position_limit": 1,
                                 "dictionary": [], "rows": [{"position": 0, "category": None, "x": 7}]}],
                      "queries": [w.query("null-only", {"empty": None, "null": None})]})
    comparisons = 0
    for i, document in enumerate(documents):
        for grain, grouping in ((256, 16), (127, 5)):
            current = w.mask_variants(document, positions_per_block=grain, blocks_per_group=grouping)
            records = evaluate(binary, current)
            if i == 3:
                assert all(records[0]["kind_blocks"].get(str(kind), 0) for kind in range(5)), records[0]
                assert any(r.get("work", {}).get("selected_delta_reductions", 0) for r in records)
            comparisons += len(records) - 1
    # Consume complete warm timed results too; no overhead subtraction or timing claim.
    evaluate(binary, kinds_example(), trials=3)
    print(f"Encoded consumers: {comparisons} exact query/program comparisons; five current kinds, "
          "NULL/holes/tails, recoded dictionaries, coarse masks and consumed timing path pass.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--input", type=Path, nargs="+")
    parser.add_argument("--trials", type=int, default=0)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not args.input:
        checks(args.binary)
        return
    report = {"kind": "complete-encoded-query-local-warm", "cases": []}
    for path in args.input:
        document = json.loads(path.read_text())
        records = evaluate(args.binary, document, args.trials)
        report["cases"].append({"input": str(path), "source": document.get("source"), "records": records})
        print(f"{path}: {len(document['queries'])} queries agree across all three encoded programs")
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
