#!/usr/bin/env python3
"""Compare filter-head cuts and window bounds on exported columns.

For each u64 working-domain column (whole 256-row blocks) this reports:

- Head entropy (bits) of an 8-bit head under three cuts: early (top byte of the
  64-bit working value, after removing the column's common leading zeros only
  if --trim-early), part-late (top byte of value - part minimum at the part's
  width) and block-late (top byte of value - block minimum at the block's
  width). Entropy is measured per head plane over the part or block it covers,
  then averaged by rows.
- Head resolution: the fraction of 16-row windows needing no suffix for
  x >= median: every row's head differs from the threshold's head, or the head
  is the whole value (width <= 8), or the base alone decides (q <= minimum).
  Windows with any head tie are reported separately.
- Window bounds: the fraction of 16-row windows settled by exact min/max, and
  by conservative 8-bit bounds quantised against the block's range
  (floor of min, ceiling of max, in 256 steps).

A model over real values; it times nothing and builds no bodies.
"""

import argparse
import json
import math
import os

import numpy as np

BLOCK, PART, WINDOW = 256, 65536, 16


def entropy(values):
    _, counts = np.unique(values, return_counts=True)
    p = counts / counts.sum()
    return float(-(p * np.log2(p)).sum())


def head(rel, width, h=8):
    """Top h bits of rel at the given value width (all bits when width <= h)."""
    shift = max(0, width - h)
    return (rel >> np.uint64(shift)).astype(np.uint64), shift


def analyse(values):
    n = len(values) // BLOCK * BLOCK
    v = values[:n]
    q = np.uint64(np.sort(v)[len(v) // 2])
    out = {}
    cuts = {}
    # Early: top byte of the 64-bit working value.
    cuts["early"] = [(v, np.uint64(0), 64)]
    # Part-late: relative to each part's minimum at the part's width.
    cuts["part_late"] = []
    for s in range(0, n, PART):
        p = v[s:s + PART]
        lo = p.min()
        cuts["part_late"].append((p, lo, int(p.max() - lo).bit_length()))
    # Block-late: relative to each block's minimum at the block's width.
    cuts["block_late"] = []
    for s in range(0, n, BLOCK):
        b = v[s:s + BLOCK]
        lo = b.min()
        cuts["block_late"].append((b, lo, int(b.max() - lo).bit_length()))
    for name, pieces in cuts.items():
        bits = 0.0
        resolved = windows = tie_windows = 0
        for seg, lo, w in pieces:
            heads, shift = head(seg - lo, w)
            bits += entropy(heads) * len(seg)
            # Threshold head in this segment's coordinates.
            # x >= q holds for the whole segment when q <= lo (base alone).
            below = q <= lo
            qh = None if below else (q - lo) >> np.uint64(shift)
            for s in range(0, len(seg), WINDOW):
                hw = heads[s:s + WINDOW]
                windows += 1
                if below:
                    resolved += 1          # decided from the base alone
                    continue
                ties = int((hw == qh).sum())
                tie_windows += ties > 0
                # A tied head needs the suffix only when the head is not the
                # whole value (shift > 0); with shift == 0 the head is exact.
                if ties == 0 or shift == 0:
                    resolved += 1
        out[f"{name}_head_entropy_bits"] = bits / n
        # Windows needing no suffix, and (separately) windows with any head tie.
        out[f"{name}_head_resolved_windows"] = resolved / windows
        out[f"{name}_head_tie_windows"] = tie_windows / windows
    exact = quant = windows = 0
    for s in range(0, n, BLOCK):
        b = v[s:s + BLOCK]
        lo, hi = int(b.min()), int(b.max())
        step = max(1, math.ceil((hi - lo + 1) / 256))
        for w in range(0, BLOCK, WINDOW):
            x = b[w:w + WINDOW]
            mn, mx = int(x.min()), int(x.max())
            windows += 1
            qq = int(q)
            exact += mn >= qq or mx < qq
            qmn = lo + (mn - lo) // step * step
            qmx = lo + min(hi - lo, ((mx - lo) // step + 1) * step - 1)
            quant += qmn >= qq or qmx < qq
    out["window_exact_settled"] = exact / windows
    out["window_q8_settled"] = quant / windows
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("inputs", nargs="+")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    with open(args.out, "w") as out:
        for path in args.inputs:
            values = np.fromfile(path, dtype="<u8")
            row = {"column": os.path.relpath(path)}
            row.update(analyse(values))
            out.write(json.dumps(row) + "\n")
            out.flush()


if __name__ == "__main__":
    main()
