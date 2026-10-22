#!/usr/bin/env python3
"""Baseline compressed sizes per column from DuckDB storage and Parquet.

For the integer-like columns of a Parquet file this reports:

- DuckDB native storage after CHECKPOINT: codecs chosen per segment, and an
  approximate byte size from segment offsets within each storage block (the
  last segment in a block is charged to the next segment's offset or the
  block's used end, so this is an estimate, not an exact accounting).
- Parquet written by DuckDB with UNCOMPRESSED (lightweight encodings only) and
  ZSTD page compression, from `parquet_metadata` column-chunk sizes.
"""

import argparse
import collections
import json
import os
import tempfile

import duckdb
import polars as pl

BLOCK_SIZE = 262144


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("parquet")
    ap.add_argument("--out", required=True)
    ap.add_argument("--shuffle", action="store_true",
                    help="store rows in a pseudo-random order (hash of row number)")
    args = ap.parse_args()
    schema = pl.read_parquet_schema(args.parquet)
    cols = [n for n, t in schema.items() if t.is_integer() or t.is_temporal()]
    select = ", ".join(f'"{c}"' for c in cols)
    result = {c: {"column": c} for c in cols}
    with tempfile.TemporaryDirectory() as tmp:
        db = duckdb.connect(os.path.join(tmp, "t.duckdb"))
        db.execute("SET threads = 1")
        order = " ORDER BY hash(file_row_number)" if args.shuffle else ""
        db.execute(f"CREATE TABLE t AS SELECT {select} FROM "
                   f"read_parquet(?, file_row_number = true){order}", [args.parquet])
        db.execute("CHECKPOINT")
        rows = db.execute("SELECT count(*) FROM t").fetchone()[0]
        segs = db.execute(
            "SELECT column_name, segment_type, compression, count, block_id, "
            "block_offset FROM pragma_storage_info('t') WHERE persistent").fetchall()
        by_block = collections.defaultdict(list)
        codecs = collections.defaultdict(collections.Counter)
        for name, stype, comp, count, block_id, offset in segs:
            if stype != "VALIDITY":
                codecs[name][comp] += count
            by_block[block_id].append((offset, name))
        est = collections.defaultdict(int)
        for block_id, entries in by_block.items():
            entries.sort()
            for i, (offset, name) in enumerate(entries):
                end = entries[i + 1][0] if i + 1 < len(entries) else BLOCK_SIZE
                est[name] += end - offset
        for c in cols:
            result[c].update(rows=rows, duckdb_codecs=dict(codecs[c]),
                             duckdb_bits_est=est[c] * 8 / rows)
        for codec in ("UNCOMPRESSED", "ZSTD"):
            path = os.path.join(tmp, f"{codec}.parquet")
            db.execute(f"COPY t TO '{path}' (FORMAT PARQUET, COMPRESSION {codec})")
            sizes = db.execute(
                "SELECT path_in_schema, sum(total_compressed_size) "
                "FROM parquet_metadata(?) GROUP BY 1", [path]).fetchall()
            for name, size in sizes:
                if name in result:
                    result[name][f"parquet_{codec.lower()}_bits"] = size * 8 / rows
    with open(args.out, "w") as out:
        for c in cols:
            out.write(json.dumps(result[c]) + "\n")


if __name__ == "__main__":
    main()
