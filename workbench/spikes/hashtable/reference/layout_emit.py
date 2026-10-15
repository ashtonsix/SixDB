#!/usr/bin/env python3
"""Emit current solver output for layout_check, without synthesizing native layouts.

Usage: python3 layout_emit.py > build/hashtable/layout-cases.txt
The small textual bridge avoids a JSON dependency in the native verifier. Original
physical fields are retained separately from the shared native descriptions.
"""
import hashlib
import argparse
import json
from pathlib import Path
import sys

import layout


def representatives():
    for placement in ("rows", "planes", "groups8", "groups16"):
        for packing in ("byte", "tight"):
            for role in ("primary", "secondary"):
                for kind, kb, kc, value_bytes, slots in (
                    ("set", 8, 55, 0, 17),
                    ("map", 8, 47, 8, 14),
                    ("multimap", 7, 9, 81, 9),
                ):
                    geometry = layout.Geometry(kb, kc, value_bytes, kind, "neon", role=role)
                    name = f"{kind}-{placement}-{packing}-{role}-v{value_bytes}"
                    yield name, layout.construct(geometry, slots, placement, packing,
                                                  "global" if role == "primary" else "grouped", 96,
                                                  8)
    for bits in (1, 7, 8):
        for physical in sorted({bits, 8}):
            geometry = layout.Geometry(bits, 0, 2, "map", "neon")
            yield f"exact-{bits}-physical{physical}", layout.construct(
                geometry, min(14, 1 << bits), "rows", "byte", "global", 1, physical)
    for role in ("primary", "secondary"):
        geometry = layout.Geometry(0, 0, 0, "set", "neon", role=role)
        yield f"singleton-{role}", layout.construct(geometry, 1, "rows", "tight", "global", 1)
    geometry = layout.Geometry(8, 47, 8, "map", "neon")
    yield "word-row16", layout.construct(geometry, 16, "rows", "byte", "global", 1)
    geometry = layout.Geometry(8, 55, 17, "multimap", "neon", block_bits=56, role="secondary")
    yield "wide-reference-group32", layout.construct(geometry, 32, "groups16", "tight", "grouped", 64)


def emit(stream):
    cases = list(representatives())
    source_hash = hashlib.sha256(Path(layout.__file__).read_bytes()).hexdigest()
    print("HTLAYOUT1", source_hash, len(cases), file=stream)
    quote = json.dumps
    for name, candidate in cases:
        lowered = layout.lower(candidate)
        descriptions, units = lowered["tuple_descriptions"], lowered["tuple_units"]
        print(quote(name), quote(candidate.identity()), candidate.stride,
              len(candidate.fields), len(descriptions), len(units), file=stream)
        # These physical fields are the independent value/write oracle. They
        # are not reconstructed from the emitted TuplePack codes or maps.
        for field in candidate.fields:
            print(quote(field.name), field.slot, field.start, field.bits, file=stream)
        for description in descriptions:
            codes, projections = description["codes"], description["projections"]
            print(description["bytes"], len(codes), len(projections), file=stream)
            for code in codes:
                print(*code, file=stream)
            for projection in projections:
                print(quote(projection["field"]), projection["packet_bytes"],
                      len(projection["code_indices"]), *projection["code_indices"],
                      *projection["logical_bits"], file=stream)
        for unit in units:
            description = descriptions[unit["description"]]
            assert len(unit["projections"]) == len(description["projections"])
            print(unit["offset"], unit["description"], len(unit["projections"]), file=stream)
            for projection, shared in zip(unit["projections"], description["projections"]):
                assert {k: v for k, v in projection.items() if k != "slot"} == shared
                print(quote(projection["field"]), projection["slot"], file=stream)


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path)
    args=parser.parse_args()
    if args.output:
        with args.output.open("w") as stream:
            emit(stream)
    else:
        emit(sys.stdout)
