#!/usr/bin/env python3
"""Derive frame-execution-logical-v1 documents from raw ClickBench rows.

Each part is one consecutive 65,536-row slice of the raw corpus in its original
row order, so category and value stay row-aligned. A part's dictionary lists
its distinct categories in first-occurrence order, which gives each part its
own code assignment (dictionary edition). Nulls are absent from the corpus;
`--null-x-when-zero` injects them and is recorded in the document's source.
"""

import argparse
import json
import os
import sys

import polars as pl

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "execution"))
import workload  # noqa: E402

PART = 65536


def derive(parquet, category, x, parts, first_part, null_x_when_zero):
    frame = pl.read_parquet(parquet, columns=[category, x],
                            n_rows=(first_part + parts) * PART)
    cat = frame[category].to_list()
    val = frame[x].to_list()
    document = {
        "format": workload.FORMAT, "kind": "corpus-derived", "snapshot": "basis-0",
        "source": {
            "corpus": "clickbench-rows (raw hits_0..2, original row order)",
            "path": os.path.basename(parquet),
            "category_column": category, "x_column": x,
            "parts": f"rows [{first_part * PART}, {(first_part + parts) * PART})",
            "category_encoding": "signed values mapped to u64 by two's complement",
            "dictionary_order": "first occurrence within the part",
            "injected_nulls": (f"x is null where {x} == 0 (synthetic)"
                               if null_x_when_zero else "none"),
        },
        "parts": [], "queries": [],
    }
    for p in range(first_part, first_part + parts):
        rows, dictionary, seen = [], [], set()
        for i in range(p * PART, (p + 1) * PART):
            c = cat[i] & workload.U64
            if c not in seen:
                seen.add(c)
                dictionary.append(c)
            v = val[i]
            if v < 0 or v > workload.U32:
                raise ValueError(f"{x} value {v} outside u32")
            rows.append({"position": i - p * PART, "category": c,
                         "x": None if (null_x_when_zero and v == 0) else v})
        document["parts"].append({
            "part_id": f"part-{p}", "dictionary_edition": f"part-{p}-first-occurrence",
            "position_limit": PART, "dictionary": dictionary, "rows": rows})
    names = [part["part_id"] for part in document["parts"]]
    everything = {n: None for n in names}
    common = sorted(set.intersection(*(set(part["dictionary"])
                                       for part in document["parts"])))
    scattered = {n: list(range(0, PART, 3)) for n in names}
    interval = {n: list(range(16384, 32768)) for n in names}
    q = workload.query
    document["queries"] = [
        q("all", everything),
        q("scattered", scattered, min_x=1),
        q("interval", interval),
        q("few-categories", everything, categories=common[:2], include_null=False),
        q("regroup", everything, shift=8),
    ]
    workload.validate(document)
    return document


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("parquet")
    ap.add_argument("--category", required=True)
    ap.add_argument("--x", required=True)
    ap.add_argument("--parts", type=int, default=3)
    ap.add_argument("--first-part", type=int, default=0)
    ap.add_argument("--null-x-when-zero", action="store_true")
    ap.add_argument("--output", required=True)
    args = ap.parse_args()
    document = derive(args.parquet, args.category, args.x, args.parts,
                      args.first_part, args.null_x_when_zero)
    with open(args.output, "w") as out:
        json.dump(document, out)
        out.write("\n")


if __name__ == "__main__":
    main()
