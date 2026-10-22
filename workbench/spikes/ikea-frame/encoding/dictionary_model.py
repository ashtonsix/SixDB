#!/usr/bin/env python3
"""Price part-wide dictionary bodies against the real block encoder's bytes.

For each exported column (u64 working-domain values, whole 256-row blocks), a
65,536-row part gets one dictionary: distinct values, sorted (order-preserving
codes) or ranked by frequency (frequent values get small codes). The dictionary
is charged as a SeriesPack plane of its values relative to the part minimum
(bit-tight, 8-row tiles). Each 256-row block of codes then takes the cheapest
of the real encoder's code-capable kinds, costed with the same byte rules as
material.cpp: constant (descriptor only), packed FOR, runs with u8 run ends,
and patched FOR with e in {2, 8, 32}. Descriptors are 16 bytes per block.

This is a byte model on top of the real layout rules, not real bodies: it
reports what dictionary bodies would cost before building them.
"""

import argparse
import glob
import json
import os

import numpy as np

BLOCK = 256
PART = 65536
DESCRIPTOR = 16


def width(span):
    return int(span).bit_length()


def plane(k, n):
    return (n + 7) // 8 * k


def block_cost(codes):
    lo, hi = int(codes.min()), int(codes.max())
    if lo == hi:
        return DESCRIPTOR, "constant"
    k = width(hi - lo)
    best = (plane(k, BLOCK), "packed")
    runs = 1 + int((codes[1:] != codes[:-1]).sum())
    best = min(best, (plane(k, runs) + runs, "runs"))
    s = np.sort(codes)
    for e in (2, 8, 32):
        keep = BLOCK - e
        span = int((s[keep - 1:] - s[:e + 1]).min())
        pk = max(1, width(span))
        if pk >= k:
            continue
        start = int(np.argmin(s[keep - 1:] - s[:e + 1]))
        base = int(s[start])
        n = int(((codes < base) | (codes - base > (1 << pk) - 1)).sum())
        if n:
            best = min(best, (plane(pk, BLOCK) + 10 + n + plane(k, n), "patched"))
    return best[0] + DESCRIPTOR, best[1]


def price(values, order):
    total = 0
    dict_bytes = 0
    kinds = {}
    for start in range(0, len(values), PART):
        part = values[start:start + PART]
        uniq, inverse, counts = np.unique(part, return_inverse=True, return_counts=True)
        if order == "frequency":
            rank = np.empty(len(uniq), dtype=np.int64)
            rank[np.argsort(-counts, kind="stable")] = np.arange(len(uniq))
            codes = rank[inverse]
        else:
            codes = inverse.astype(np.int64)
        d = plane(max(1, width(int(uniq[-1]) - int(uniq[0]))), len(uniq)) + 8
        dict_bytes += d
        total += d
        for b in range(0, len(part), BLOCK):
            cost, kind = block_cost(codes[b:b + BLOCK])
            total += cost
            kinds[kind] = kinds.get(kind, 0) + 1
    return 8 * total / len(values), 8 * dict_bytes / len(values), kinds


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("inputs", nargs="+", help="u64 files")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    with open(args.out, "w") as out:
        for path in args.inputs:
            values = np.fromfile(path, dtype="<u8")
            row = {"column": os.path.basename(path)[:-4], "rows": len(values)}
            for order in ("sorted", "frequency"):
                bits, dict_bits, kinds = price(values, order)
                row[order] = {"bits_per_value": bits, "dictionary_bits_per_value": dict_bits,
                              "kinds": kinds}
            out.write(json.dumps(row) + "\n")
            out.flush()


if __name__ == "__main__":
    main()
