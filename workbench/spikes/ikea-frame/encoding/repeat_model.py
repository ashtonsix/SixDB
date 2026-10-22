#!/usr/bin/env python3
"""Price a "repeat of a recent value" block kind against lightweight kinds and ZSTD.

For hash-like columns the gap to Parquet+ZSTD is nearby repeats (LZ matches),
not zero-order entropy. This models the narrowest block kind that could
capture them while keeping Frame's access shape: each 65,536-row part decodes
independently, and a point read takes at most two plane reads.

Repeat block body (byte model, the same plane rules as material.cpp):
  [flag plane, 256 x 1 bit = 32 B][literal plane: FOR over the block's new
  values][reference plane: R-bit distances] + descriptor + 2 B counts.
A row is a reference when its value was emitted as a literal within the last
2^R literals of the same part (distance 1..2^R back in the part's literal
sequence); otherwise it is a new literal. References point only at literals,
never at references, so a point read is flag -> distance -> one literal point
read (the literal's block comes from a per-block prefix of literal counts that
the descriptors already carry). Each block takes the cheapest of the
lightweight kinds (dictionary_model.block_cost: constant, packed, runs,
patched; no delta) and the repeat kind at the part's best R.

ZSTD arms compress each part's raw little-endian u64 bytes with the zstd CLI,
as a byte bound with no random access. This is a byte model, not real bodies.
"""

import argparse
import json
import os
import subprocess

import numpy as np

from dictionary_model import BLOCK, DESCRIPTOR, PART, block_cost, plane, width

R_CHOICES = (8, 12, 16)


def repeat_part(part, r):
    """Per block: (cost bytes or None, references) for the repeat kind at R bits."""
    window = 1 << r
    last = {}  # value -> index of its latest literal in the part
    literals = 0
    out = []
    for b in range(0, len(part), BLOCK):
        block = part[b:b + BLOCK].tolist()
        lit = []
        refs = 0
        for v in block:
            at = last.get(v)
            if at is not None and literals - at <= window:
                refs += 1
            else:
                last[v] = literals
                literals += 1
                lit.append(v)
        if refs == 0:
            out.append((None, 0))
            continue
        lp = plane(max(1, width(max(lit) - min(lit))), len(lit)) if lit else 0
        out.append((32 + lp + plane(r, refs) + 2 + DESCRIPTOR, refs))
    return out


def zstd_bytes(part, level):
    run = subprocess.run(["zstd", f"-{level}", "-c", "-q"], input=part.astype("<u8").tobytes(),
                         capture_output=True, check=True)
    return len(run.stdout)


def price(values, zstd_levels):
    light = 0
    best = 0
    repeat_blocks = 0
    refs = 0
    chosen_r = {}
    zstd = {level: 0 for level in zstd_levels}
    for start in range(0, len(values), PART):
        part = values[start:start + PART]
        base = [block_cost(part[b:b + BLOCK])[0] for b in range(0, len(part), BLOCK)]
        light += sum(base)
        options = []
        for r in R_CHOICES:
            arms = repeat_part(part, r)
            total = 0
            used = 0
            n_refs = 0
            for cost, (rep, n) in zip(base, arms):
                if rep is not None and rep < cost:
                    total += rep
                    used += 1
                    n_refs += n
                else:
                    total += cost
            options.append((total, r, used, n_refs))
        total, r, used, n_refs = min(options)
        best += total
        repeat_blocks += used
        refs += n_refs
        chosen_r[r] = chosen_r.get(r, 0) + 1
        for level in zstd_levels:
            zstd[level] += zstd_bytes(part, level)
    n = len(values)
    return {
        "lightweight_bits": 8 * light / n,
        "with_repeat_bits": 8 * best / n,
        "repeat_block_fraction": repeat_blocks / (n // BLOCK),
        "reference_row_fraction": refs / n,
        "parts_by_r": chosen_r,
        **{f"zstd{level}_bits": 8 * zstd[level] / n for level in zstd_levels},
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("inputs", nargs="+", help="u64 files")
    ap.add_argument("--out", required=True)
    ap.add_argument("--zstd", default="3,19")
    args = ap.parse_args()
    levels = [int(x) for x in args.zstd.split(",") if x]
    with open(args.out, "a") as out:
        for path in args.inputs:
            values = np.fromfile(path, dtype="<u8")
            row = {"column": os.path.basename(path)[:-4],
                   "order": os.path.basename(os.path.dirname(path)), "rows": len(values),
                   **price(values, levels)}
            out.write(json.dumps(row) + "\n")
            out.flush()
            print(json.dumps(row), flush=True)


if __name__ == "__main__":
    main()
