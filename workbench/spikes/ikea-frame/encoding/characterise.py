#!/usr/bin/env python3
"""Characterise integer-like columns for Frame encoding choices.

Reads one column at a time from a Parquet file. For each 256-row block it
models the payload bits of candidate encodings, then charges a per-block
descriptor, so descriptor size can be swept separately. Costs are logical
bits; they ignore SeriesPack tile geometry and are a first signal, not
physical sizes. Nulls keep their positions (forward-filled, presence mask not
charged); null counts are reported. "global_for" is one frame of reference
over the whole column, not a native-width plain buffer (reported separately).
"""

import argparse
import json
import sys

import numpy as np
import polars as pl

BLOCK = 256
PART = 65536
EXCEPTION_POSITION_BITS = 8
EXCEPTION_BUDGETS = (0, 2, 8, 32)
DESCRIPTOR_SWEEP = (16, 64, 128)    # bits per non-constant block
CONSTANT_KIND_BITS = 8              # a constant block: kind code plus its value


def widths(span):
    """Exact bits needed for unsigned values in [0, span], elementwise."""
    span = np.asarray(span, dtype=np.uint64)
    out = np.zeros(span.shape, dtype=np.int64)
    for k in range(64):
        out += span >= np.uint64(1) << np.uint64(k)
    return out


def ordered(values):
    """Map integers to uint64 preserving order (offset binary for signed)."""
    if values.dtype.kind == "u":
        return values.astype(np.uint64)
    return values.astype(np.int64).view(np.uint64) ^ np.uint64(1 << 63)


def blocks(values):
    n = len(values) // BLOCK * BLOCK
    return values[:n].reshape(-1, BLOCK)


def for_payload(b, budget):
    """Frame-of-reference payload bits per block with `budget` exceptions."""
    s = np.sort(b, axis=1)
    keep = BLOCK - budget
    span = (s[:, keep - 1:] - s[:, :budget + 1]).min(axis=1)
    full = widths(s[:, -1] - s[:, 0])
    exc = budget * (EXCEPTION_POSITION_BITS + full)
    return widths(span) * BLOCK + exc


def delta_payload(b):
    d = ordered(np.diff(b, axis=1).view(np.int64))
    w = widths(d.max(axis=1) - d.min(axis=1))
    # Residuals plus 15 checkpoints at w + 8 bits (no exceptions here).
    return w * (BLOCK - 1) + 15 * (w + 8)


def runs_payload(b):
    runs = 1 + (np.diff(b, axis=1) != 0).sum(axis=1)
    value = widths(b.max(axis=1) - b.min(axis=1))
    return runs * (value + 8), runs


def dictionary_codes(values):
    """Part-wide sorted dictionary codes, and dictionary bits per value."""
    codes = np.empty_like(values)
    dict_bits = 0
    for start in range(0, len(values), PART):
        part = values[start:start + PART]
        uniq, inv = np.unique(part, return_inverse=True)
        codes[start:start + PART] = inv
        span = widths(np.array([uniq[-1] - uniq[0]], dtype=np.uint64))[0]
        dict_bits += len(uniq) * max(int(span), 1)
    return codes, dict_bits / len(values)


def characterise(name, series, nulls):
    raw = series.to_numpy()
    if raw.dtype.kind == "M":
        raw = raw.astype("datetime64[s]").astype(np.int64)
    values = ordered(raw)
    b = blocks(values)
    if len(b) == 0:
        return None
    nblocks = len(b)
    constant = (b.max(axis=1) == b.min(axis=1))
    global_w = int(widths(np.array([values.max() - values.min()], dtype=np.uint64))[0])
    native_w = raw.dtype.itemsize * 8

    codes, dict_cost = dictionary_codes(values[: nblocks * BLOCK])
    cb = blocks(codes)
    runs_bits, runs = runs_payload(b)
    candidates = {
        "global_for": np.full(nblocks, global_w * BLOCK),
        "delta": delta_payload(b),
        "runs": runs_bits,
    }
    for e in EXCEPTION_BUDGETS:
        candidates[f"for_e{e}"] = for_payload(b, e)
    candidates["dict_for"] = np.minimum.reduce(
        [for_payload(cb, e) for e in EXCEPTION_BUDGETS]) + dict_cost * BLOCK
    candidates["dict_runs"] = runs_payload(cb)[0] + dict_cost * BLOCK

    row = {
        "column": name, "dtype": str(series.dtype), "rows": len(values) + nulls,
        "nulls": nulls, "native_bits": native_w, "global_for_bits": global_w,
        "constant_blocks": float(constant.mean()),
        "monotone_blocks": float((np.diff(b, axis=1) >= 0).all(axis=1).mean()),
        "mean_runs": float(runs.mean()),
        "dict_bits_per_value": dict_cost,
    }
    const_cost = CONSTANT_KIND_BITS + global_w
    for d in DESCRIPTOR_SWEEP:
        best_single, best_single_bits = None, None
        for k, payload in candidates.items():
            total = np.where(constant, const_cost, payload + d).sum() / (nblocks * BLOCK)
            row[f"{k}_d{d}"] = float(total)
            if best_single_bits is None or total < best_single_bits:
                best_single, best_single_bits = k, float(total)
        mixed = np.minimum.reduce(list(candidates.values())) + d
        mixed = np.where(constant, const_cost, mixed)
        winners = np.argmin(np.stack(list(candidates.values())), axis=0)
        names = list(candidates)
        share = {names[i]: float(((winners == i) & ~constant).mean())
                 for i in np.unique(winners)}
        row[f"best_single_d{d}"] = best_single
        row[f"best_single_bits_d{d}"] = best_single_bits
        row[f"mixed_bits_d{d}"] = float(mixed.sum() / (nblocks * BLOCK))
        row[f"mixed_share_d{d}"] = share
    return row


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("parquet")
    ap.add_argument("--out", required=True, help="JSON lines output")
    args = ap.parse_args()
    schema = pl.read_parquet_schema(args.parquet)
    with open(args.out, "w") as out:
        for name, dtype in schema.items():
            if not (dtype.is_integer() or dtype.is_temporal()):
                continue
            col = pl.read_parquet(args.parquet, columns=[name])[name]
            # Dense image: nulls keep their position with a forward-filled value
            # (a free filler); the presence mask is not charged here.
            dense = col.fill_null(strategy="forward").fill_null(strategy="backward")
            row = characterise(name, dense, int(col.null_count()))
            if row:
                out.write(json.dumps(row) + "\n")
                out.flush()
                print(f"{name}: global {row['global_for_bits']}, mixed@64 "
                      f"{row['mixed_bits_d64']:.2f}, best@64 "
                      f"{row['best_single_d64']} {row['best_single_bits_d64']:.2f}",
                      file=sys.stderr)


if __name__ == "__main__":
    main()
