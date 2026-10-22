#!/usr/bin/env python3
"""Single-thread DuckDB and Polars timings for simple column operations.

For each column: SUM, COUNT of a threshold predicate (value >= median), and a
decode-and-consume proxy (MAX over the column, which DuckDB answers from data,
not statistics, only when a filter defeats zone maps; so a filtered MAX is
used). DuckDB runs against a checkpointed, compressed database file; Polars
against an in-memory, uncompressed Series. Per-query fixed overhead is measured
with the same query over a one-row table and reported separately, not
subtracted silently. These are local, indicative timings, not fleet results.
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
            queries = {
                "sum": f'SELECT sum("{c}") FROM {{}}',
                "count_ge": f'SELECT count(*) FROM {{}} WHERE "{c}" >= {threshold}',
                "max_filtered": f'SELECT max("{c}") FROM {{}} WHERE "{c}" <> {threshold}',
            }
            row = {"column": c, "rows": rows, "threshold": threshold}
            for name, q in queries.items():
                full = median_ns(lambda: db.execute(q.format("t")).fetchall())
                fixed = median_ns(lambda: db.execute(q.format("one")).fetchall())
                row[f"duckdb_{name}_ns"] = full
                row[f"duckdb_{name}_fixed_ns"] = fixed
                row[f"duckdb_{name}_ns_per_value"] = (full - fixed) / rows
            polars_ops = {
                "sum": lambda: s.sum(),
                "count_ge": lambda: (s >= threshold).sum(),
                "max_filtered": lambda: s.filter(s != threshold).max(),
            }
            for name, fn in polars_ops.items():
                t = median_ns(fn)
                row[f"polars_{name}_ns"] = t
                row[f"polars_{name}_ns_per_value"] = t / rows
            out.write(json.dumps(row) + "\n")
            out.flush()
            print(c, {k: round(v, 3) for k, v in row.items()
                      if k.endswith("per_value")})


if __name__ == "__main__":
    main()
