#!/usr/bin/env python3
"""Enumerate metadata read unions for capacity 256 and a 64-byte-aligned base.

This transcribes MetadataCursor/read_metadata16 and ScanPack6's branch-local
loads. It counts distinct payload bytes and lines, not issued bytes or misses.
No compiler, encoded input, machine timing or external package is required.
"""
import argparse
from collections import Counter
import json
from statistics import mean

CAPACITY = 256
LINE_BYTES = 64
KINDS = ("direct32", "local16", "scan128")
RANGES = ((0, 256), (3, 37), (15, 18), (255, 1))
# Each entry is the actual chunk set read for a group of 32 ScanPack6 values.
SCAN6_CHUNKS = ((0,), (0, 1), (1, 2), (2,))


def merge(intervals):
    result = []
    for start, stop in sorted(intervals):
        if result and start <= result[-1][1]:
            result[-1] = (result[-1][0], max(stop, result[-1][1]))
        else:
            result.append((start, stop))
    return result


def refill_children(kind, frame):
    """Half-open byte intervals read for one packed sixteen-entry frame."""
    assert kind in ("local16", "scan128") and 0 <= frame < CAPACITY // 16
    if kind == "local16":
        base = 32 * frame
        return {"checkpoint": [(base, base + 2)],
                "population": [(base + 2, base + 20)],
                "length": [(base + 20, base + 32)]}
    population = CAPACITY // 8 + 18 * frame
    length = CAPACITY * 10 // 8 + 96 * (frame // 8) + 16 * (frame % 2)
    return {"checkpoint": [(2 * frame, 2 * frame + 2)],
            "population": [(population, population + 18)],
            "length": [(length + 32 * chunk, length + 32 * chunk + 16)
                       for chunk in SCAN6_CHUNKS[(frame % 8) // 2]]}


def read_intervals(kind, first, count):
    assert kind in KINDS and 0 <= first <= CAPACITY and 0 <= count <= CAPACITY - first
    if count == 0:
        return []
    if kind == "direct32":
        # The live cursor reads requested scalar records, not read_metadata16.
        return [(4 * first, 4 * (first + count))]
    return merge([interval
                  for frame in range(first // 16, (first + count - 1) // 16 + 1)
                  for child in refill_children(kind, frame).values()
                  for interval in child])


def describe(intervals):
    intervals = merge(intervals)
    payload = {byte for start, stop in intervals for byte in range(start, stop)}
    lines = sorted({byte // LINE_BYTES for byte in payload})
    return {"intervals": intervals, "unique_bytes": len(payload), "lines": lines,
            "line_count": len(lines),
            "envelope_bytes": max(payload) - min(payload) + 1 if payload else 0,
            "two_adjacent_lines": not lines or lines[-1] - lines[0] <= 1}


def audit():
    result = {"capacity": CAPACITY, "base_alignment": LINE_BYTES, "points": {}, "ranges": {}}
    for kind in KINDS:
        extent = CAPACITY * (4 if kind == "direct32" else 2)
        points = [describe(read_intervals(kind, i, 1)) for i in range(CAPACITY)]
        assert all(0 <= start < stop <= extent for p in points for start, stop in p["intervals"])
        assert read_intervals(kind, 0, CAPACITY) == [(0, extent)]
        assert read_intervals(kind, CAPACITY, 0) == []
        result["points"][kind] = {
            "unique_bytes_mean": mean(p["unique_bytes"] for p in points),
            "unique_bytes_max": max(p["unique_bytes"] for p in points),
            "lines_mean": mean(p["line_count"] for p in points),
            "lines_max": max(p["line_count"] for p in points),
            "line_count_distribution": dict(sorted(Counter(p["line_count"] for p in points).items())),
            "two_adjacent_line_failures": sum(not p["two_adjacent_lines"] for p in points)}
        result["ranges"][kind] = {f"{first}/{count}": describe(read_intervals(kind, first, count))
                                  for first, count in RANGES}
    # Every child refill is local under the two-adjacent-line condition. Their
    # enclosing placement is checked separately; point 80 supplies a witness.
    assert all(describe(child)["two_adjacent_lines"]
               for kind in ("local16", "scan128") for frame in range(CAPACITY // 16)
               for child in refill_children(kind, frame).values())
    result["scan128_witness"] = {
        "points": [80, 96],
        "children": {name: describe(intervals)
                     for name, intervals in refill_children("scan128", 5).items()},
        "combined": describe(read_intervals("scan128", 80, 1))}
    assert not result["scan128_witness"]["combined"]["two_adjacent_lines"]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="Emit the complete structured result")
    args = parser.parse_args()
    result = audit()
    if args.json:
        print(json.dumps(result, indent=2))
        return
    print(f"capacity={CAPACITY}, base_alignment={LINE_BYTES}; byte intervals are half-open")
    for kind in KINDS:
        print(f"point {kind}: {json.dumps(result['points'][kind], sort_keys=True)}")
        for key, value in result["ranges"][kind].items():
            print(f"range {kind} {key}: {json.dumps(value, sort_keys=True)}")
    print("scan128 witness 80..95:", json.dumps(result["scan128_witness"], sort_keys=True))


if __name__ == "__main__":
    main()
