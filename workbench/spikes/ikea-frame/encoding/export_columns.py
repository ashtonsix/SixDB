#!/usr/bin/env python3
"""Export ClickBench columns as raw little-endian u64 files for native probes.

Signed integers are mapped to offset binary (order-preserving), so every file
holds unsigned working-domain values in original row order. A JSON sidecar
records the column's dtype and the mapping, so exact logical results can be
recovered (logical = u - 2^63 for signed columns).
"""

import argparse
import json
import os

import numpy as np
import polars as pl


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("parquet")
    ap.add_argument("--columns", nargs="+", required=True)
    ap.add_argument("--output", required=True, help="directory")
    args = ap.parse_args()
    os.makedirs(args.output, exist_ok=True)
    for name in args.columns:
        s = pl.read_parquet(args.parquet, columns=[name])[name]
        if s.null_count():
            raise SystemExit(f"{name} has nulls; this exporter keeps dense values only")
        raw = s.to_numpy()
        signed = raw.dtype.kind == "i"
        u = (raw.astype(np.int64).view(np.uint64) ^ np.uint64(1 << 63)) if signed \
            else raw.astype(np.uint64)
        safe = "".join(ch if ch.isalnum() else "_" for ch in name).strip("_")
        u.astype("<u8").tofile(os.path.join(args.output, f"{safe}.u64"))
        with open(os.path.join(args.output, f"{safe}.json"), "w") as meta:
            json.dump({"column": name, "dtype": str(s.dtype), "rows": len(u),
                       "mapping": "offset-binary" if signed else "identity"}, meta)


if __name__ == "__main__":
    main()
