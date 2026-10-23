#!/usr/bin/env python3
"""Single-thread DuckDB and Polars timings for simple column operations.

For each column: SUM, COUNT of a threshold predicate (value >= median), and a
filtered MAX query (a query, not a decode-to-array equivalent). DuckDB runs
against a checkpointed, compressed database file; Polars against an in-memory,
uncompressed Series. Every answer is checked against an exact Python-integer
reference; SUM is compared under exact (unbounded) arithmetic, and a peer whose
result differs (for example a wrapped u64/i64 sum) is reported as a different
law, not timed as equivalent. Full query times are primary; the same query on a
one-row table is recorded only as context, since plans and statistics differ.
These are local, indicative timings, not fleet results.
"""

import argparse
import json
import os
import statistics
import tempfile
import time

import duckdb
import polars as pl

REPEATS = 15


def median_ns(fn):
    fn()
    samples = []
    for _ in range(REPEATS):
        start = time.perf_counter_ns()
        fn()
        samples.append(time.perf_counter_ns() - start)
    return statistics.median(samples)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("parquet")
    ap.add_argument("--columns", nargs="+", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    cols = args.columns
    frame = pl.read_parquet(args.parquet, columns=cols)
    rows = frame.height
    with tempfile.TemporaryDirectory() as tmp, open(args.out, "w") as out:
        db = duckdb.connect(os.path.join(tmp, "t.duckdb"))
        db.execute("SET threads = 1")
        select = ", ".join(f'"{c}"' for c in cols)
        db.execute(f"CREATE TABLE t AS SELECT {select} FROM read_parquet(?)",
                   [args.parquet])
        db.execute(f"CREATE TABLE one AS SELECT {select} FROM t LIMIT 1")
        db.execute("CHECKPOINT")
        for c in cols:
            s = frame[c]
            threshold = int(s.median())
            exact = [int(v) for v in s.to_list()]
            reference = {
                "sum": sum(exact),
                "count_ge": sum(1 for v in exact if v >= threshold),
                "max_filtered": max(v for v in exact if v != threshold),
            }
            queries = {
                "sum": f'SELECT sum("{c}") FROM {{}}',
                "count_ge": f'SELECT count(*) FROM {{}} WHERE "{c}" >= {threshold}',
                "max_filtered": f'SELECT max("{c}") FROM {{}} WHERE "{c}" <> {threshold}',
            }
            row = {"column": c, "rows": rows, "threshold": threshold,
                   "reference": {k: str(v) for k, v in reference.items()}}
            for name, q in queries.items():
                answer = int(db.execute(q.format("t")).fetchone()[0])
                row[f"duckdb_{name}_matches"] = answer == reference[name]
                full = median_ns(lambda: db.execute(q.format("t")).fetchall())
                one_row = median_ns(lambda: db.execute(q.format("one")).fetchall())
                row[f"duckdb_{name}_ns"] = full
                row[f"duckdb_{name}_one_row_ns"] = one_row
                row[f"duckdb_{name}_ns_per_value"] = full / rows
            wide = pl.Int128 if s.dtype in (pl.Int64, pl.UInt64) else pl.Int64
            polars_ops = {
                "sum": lambda: s.sum(),
                # Same exact law as the reference: widen before summing.
                "sum_widened": lambda: s.cast(wide).sum(),
                "count_ge": lambda: (s >= threshold).sum(),
                "max_filtered": lambda: s.filter(s != threshold).max(),
            }
            reference["sum_widened"] = reference["sum"]
            for name, fn in polars_ops.items():
                row[f"polars_{name}_matches"] = int(fn()) == reference[name]
                t = median_ns(fn)
                row[f"polars_{name}_ns"] = t
                row[f"polars_{name}_ns_per_value"] = t / rows
            out.write(json.dumps(row) + "\n")
            out.flush()
            print(c, {k: (round(v, 3) if isinstance(v, float) else v)
                      for k, v in row.items()
                      if k.endswith("per_value") or k.endswith("matches")})


if __name__ == "__main__":
    main()
