# Frame read/build scorecard — carried Intervals regions

Current core source: **85f22f57**, including the carried-region Intervals fix.
The original 26afac10 results remain in the earlier evidence directories.

All five columns, four operations and both conditioning modes are shown for
all qualified profiles. Values are median ns per whole query (65,536 rows, or
4,096 materialized independent points), five rotating-arm rounds. Every result
is checked against the original-value oracle; point order and duplicates count.

**Ikea/spike > 1 means Ikea takes longer. Native/Ikea > 1 means Ikea is faster.**
The spike reads trusted fitted-builder storage; Ikea uses its checked public API
over emitted, placed images. Ratios include admission and representation/placement
costs. They are not same-byte kernel-only deltas. The native control is genuine
typed FOR storage with exact wide SUM and a consumed output for every point.
Ikea points are repeated checked get calls; native points are one pre-admitted
batch gather, so this is consumer throughput rather than leaf latency.

Repeated samples average 32 queries. Eviction samples execute one query after a
256 MiB sweep; this establishes no cache tier or DRAM service. These are one-unit
regression cells, not recipe recommendations, saturated-bandwidth measurements,
new Polars/DuckDB comparisons or the sparse correlated A->B seam study.
[GRADUATION.md](../GRADUATION.md) records scope, source identity and decisions.
[README.md](README.md) specifies the trace and comparison boundaries.

Regenerate each profile table with
`python3 workbench/spikes/frame-phase-3/landing/summarise.py EVIDENCE/PROFILE/measure.jsonl`.

## Zen 5 AVX-512

[Retained evidence](evidence/carried-zen5/artifact.json).

| Column | Operation | Conditioning | Ikea ns/query | Ikea/spike | Native/Ikea |
| --- | --- | --- | ---: | ---: | ---: |
| EventTime | count_ge | eviction_sweep_256MiB | 55511.0 | 0.66 | 0.13 |
| EventTime | count_ge | repeated | 24593.7 | 0.43 | 0.06 |
| EventTime | points_materialized | eviction_sweep_256MiB | 178661.0 | 0.93 | 0.11 |
| EventTime | points_materialized | repeated | 59671.2 | 0.96 | 0.03 |
| EventTime | selected_sum_period3 | eviction_sweep_256MiB | 67191.0 | 1.12 | 0.12 |
| EventTime | selected_sum_period3 | repeated | 34906.8 | 1.01 | 0.08 |
| EventTime | sum | eviction_sweep_256MiB | 34780.0 | 0.97 | 0.20 |
| EventTime | sum | repeated | 8895.4 | 0.97 | 0.14 |
| OS | count_ge | eviction_sweep_256MiB | 12750.0 | 0.87 | 0.27 |
| OS | count_ge | repeated | 1990.9 | 1.04 | 0.77 |
| OS | points_materialized | eviction_sweep_256MiB | 116031.0 | 1.03 | 0.13 |
| OS | points_materialized | repeated | 60211.8 | 1.05 | 0.02 |
| OS | selected_sum_period3 | eviction_sweep_256MiB | 31950.0 | 0.92 | 0.15 |
| OS | selected_sum_period3 | repeated | 9005.7 | 1.07 | 0.31 |
| OS | sum | eviction_sweep_256MiB | 18740.0 | 0.89 | 0.17 |
| OS | sum | repeated | 2393.5 | 1.12 | 0.27 |
| URLHash | count_ge | eviction_sweep_256MiB | 21790.0 | 1.06 | 0.61 |
| URLHash | count_ge | repeated | 5317.2 | 1.05 | 0.38 |
| URLHash | points_materialized | eviction_sweep_256MiB | 163201.0 | 1.22 | 0.12 |
| URLHash | points_materialized | repeated | 18118.9 | 1.22 | 0.09 |
| URLHash | selected_sum_period3 | eviction_sweep_256MiB | 29001.0 | 0.51 | 1.48 |
| URLHash | selected_sum_period3 | repeated | 11081.0 | 0.30 | 0.49 |
| URLHash | sum | eviction_sweep_256MiB | 35191.0 | 0.64 | 0.41 |
| URLHash | sum | repeated | 3715.7 | 0.27 | 0.60 |
| UserID | count_ge | eviction_sweep_256MiB | 4470.0 | 0.94 | 2.96 |
| UserID | count_ge | repeated | 380.6 | 1.54 | 5.39 |
| UserID | points_materialized | eviction_sweep_256MiB | 195662.0 | 1.16 | 0.10 |
| UserID | points_materialized | repeated | 61278.3 | 1.05 | 0.03 |
| UserID | selected_sum_period3 | eviction_sweep_256MiB | 57881.0 | 0.87 | 0.71 |
| UserID | selected_sum_period3 | repeated | 13473.8 | 0.70 | 0.41 |
| UserID | sum | eviction_sweep_256MiB | 43840.0 | 0.87 | 0.33 |
| UserID | sum | repeated | 6129.1 | 0.71 | 0.38 |
| gen_ts | count_ge | eviction_sweep_256MiB | 5840.0 | 1.03 | 1.23 |
| gen_ts | count_ge | repeated | 3241.0 | 1.01 | 0.48 |
| gen_ts | points_materialized | eviction_sweep_256MiB | 237522.0 | 0.99 | 0.08 |
| gen_ts | points_materialized | repeated | 104918.1 | 1.01 | 0.01 |
| gen_ts | selected_sum_period3 | eviction_sweep_256MiB | 58370.0 | 1.04 | 0.37 |
| gen_ts | selected_sum_period3 | repeated | 38707.8 | 0.98 | 0.07 |
| gen_ts | sum | eviction_sweep_256MiB | 34341.0 | 0.55 | 0.21 |
| gen_ts | sum | repeated | 11471.0 | 0.29 | 0.11 |

## Zen 5 AVX2

[Retained evidence](evidence/carried-zen5/artifact.json).

| Column | Operation | Conditioning | Ikea ns/query | Ikea/spike | Native/Ikea |
| --- | --- | --- | ---: | ---: | ---: |
| EventTime | count_ge | eviction_sweep_256MiB | 58340.0 | 0.65 | 0.13 |
| EventTime | count_ge | repeated | 29226.5 | 0.48 | 0.16 |
| EventTime | points_materialized | eviction_sweep_256MiB | 188641.0 | 1.03 | 0.10 |
| EventTime | points_materialized | repeated | 59380.5 | 0.96 | 0.03 |
| EventTime | selected_sum_period3 | eviction_sweep_256MiB | 64150.0 | 0.98 | 0.13 |
| EventTime | selected_sum_period3 | repeated | 36658.1 | 0.96 | 0.15 |
| EventTime | sum | eviction_sweep_256MiB | 39131.0 | 0.88 | 0.19 |
| EventTime | sum | repeated | 14207.9 | 0.98 | 0.15 |
| OS | count_ge | eviction_sweep_256MiB | 19270.0 | 0.85 | 0.31 |
| OS | count_ge | repeated | 5447.9 | 1.00 | 0.87 |
| OS | points_materialized | eviction_sweep_256MiB | 109651.0 | 1.03 | 0.12 |
| OS | points_materialized | repeated | 60036.4 | 1.05 | 0.02 |
| OS | selected_sum_period3 | eviction_sweep_256MiB | 29130.0 | 0.89 | 0.24 |
| OS | selected_sum_period3 | repeated | 8407.6 | 0.96 | 0.64 |
| OS | sum | eviction_sweep_256MiB | 19510.0 | 0.90 | 0.16 |
| OS | sum | repeated | 2804.1 | 1.09 | 0.38 |
| URLHash | count_ge | eviction_sweep_256MiB | 21720.0 | 0.96 | 0.61 |
| URLHash | count_ge | repeated | 8216.9 | 0.99 | 0.49 |
| URLHash | points_materialized | eviction_sweep_256MiB | 159111.0 | 1.23 | 0.12 |
| URLHash | points_materialized | repeated | 17944.8 | 1.22 | 0.09 |
| URLHash | selected_sum_period3 | eviction_sweep_256MiB | 29400.0 | 0.67 | 0.56 |
| URLHash | selected_sum_period3 | repeated | 20463.6 | 0.60 | 0.40 |
| URLHash | sum | eviction_sweep_256MiB | 25391.0 | 0.97 | 0.54 |
| URLHash | sum | repeated | 15438.6 | 1.16 | 0.24 |
| UserID | count_ge | eviction_sweep_256MiB | 5120.0 | 1.05 | 2.55 |
| UserID | count_ge | repeated | 475.9 | 1.33 | 8.43 |
| UserID | points_materialized | eviction_sweep_256MiB | 202421.0 | 1.18 | 0.10 |
| UserID | points_materialized | repeated | 61558.7 | 1.06 | 0.03 |
| UserID | selected_sum_period3 | eviction_sweep_256MiB | 52291.0 | 0.78 | 0.34 |
| UserID | selected_sum_period3 | repeated | 22238.3 | 0.86 | 0.37 |
| UserID | sum | eviction_sweep_256MiB | 44620.0 | 0.81 | 0.30 |
| UserID | sum | repeated | 15195.1 | 0.97 | 0.24 |
| gen_ts | count_ge | eviction_sweep_256MiB | 6070.0 | 1.02 | 1.30 |
| gen_ts | count_ge | repeated | 3233.2 | 0.93 | 1.47 |
| gen_ts | points_materialized | eviction_sweep_256MiB | 245892.0 | 0.99 | 0.08 |
| gen_ts | points_materialized | repeated | 111794.7 | 1.02 | 0.01 |
| gen_ts | selected_sum_period3 | eviction_sweep_256MiB | 54370.0 | 0.99 | 0.16 |
| gen_ts | selected_sum_period3 | repeated | 45011.0 | 0.97 | 0.12 |
| gen_ts | sum | eviction_sweep_256MiB | 43101.0 | 0.77 | 0.17 |
| gen_ts | sum | repeated | 33129.7 | 0.70 | 0.06 |

## Graviton 4 NEON

[Retained evidence](evidence/carried-graviton4/artifact.json).

| Column | Operation | Conditioning | Ikea ns/query | Ikea/spike | Native/Ikea |
| --- | --- | --- | ---: | ---: | ---: |
| EventTime | count_ge | eviction_sweep_256MiB | 86131.0 | 1.07 | 0.18 |
| EventTime | count_ge | repeated | 58686.5 | 0.97 | 0.20 |
| EventTime | points_materialized | eviction_sweep_256MiB | 128797.0 | 1.03 | 0.14 |
| EventTime | points_materialized | repeated | 80619.4 | 1.04 | 0.04 |
| EventTime | selected_sum_period3 | eviction_sweep_256MiB | 96000.0 | 1.08 | 0.22 |
| EventTime | selected_sum_period3 | repeated | 73392.7 | 1.01 | 0.23 |
| EventTime | sum | eviction_sweep_256MiB | 38522.0 | 0.98 | 0.26 |
| EventTime | sum | repeated | 20221.0 | 0.99 | 0.18 |
| OS | count_ge | eviction_sweep_256MiB | 17844.0 | 0.92 | 0.91 |
| OS | count_ge | repeated | 7827.2 | 1.01 | 1.91 |
| OS | points_materialized | eviction_sweep_256MiB | 104640.0 | 1.04 | 0.07 |
| OS | points_materialized | repeated | 80337.6 | 1.15 | 0.03 |
| OS | selected_sum_period3 | eviction_sweep_256MiB | 29124.0 | 0.86 | 0.82 |
| OS | selected_sum_period3 | repeated | 14920.5 | 0.86 | 1.56 |
| OS | sum | eviction_sweep_256MiB | 18137.0 | 1.01 | 0.24 |
| OS | sum | repeated | 5511.3 | 1.08 | 0.47 |
| URLHash | count_ge | eviction_sweep_256MiB | 35619.0 | 1.09 | 0.48 |
| URLHash | count_ge | repeated | 22954.7 | 1.05 | 0.37 |
| URLHash | points_materialized | eviction_sweep_256MiB | 141654.0 | 1.20 | 0.15 |
| URLHash | points_materialized | repeated | 33002.5 | 1.25 | 0.11 |
| URLHash | selected_sum_period3 | eviction_sweep_256MiB | 44727.0 | 0.71 | 0.65 |
| URLHash | selected_sum_period3 | repeated | 32366.0 | 0.58 | 0.72 |
| URLHash | sum | eviction_sweep_256MiB | 32106.0 | 0.85 | 0.58 |
| URLHash | sum | repeated | 22217.1 | 0.72 | 0.51 |
| UserID | count_ge | eviction_sweep_256MiB | 6902.0 | 1.16 | 2.51 |
| UserID | count_ge | repeated | 1469.8 | 1.25 | 6.01 |
| UserID | points_materialized | eviction_sweep_256MiB | 157446.0 | 1.10 | 0.13 |
| UserID | points_materialized | repeated | 71067.0 | 1.16 | 0.05 |
| UserID | selected_sum_period3 | eviction_sweep_256MiB | 69208.0 | 0.99 | 0.42 |
| UserID | selected_sum_period3 | repeated | 43028.6 | 0.91 | 0.54 |
| UserID | sum | eviction_sweep_256MiB | 40841.0 | 0.90 | 0.46 |
| UserID | sum | repeated | 23862.4 | 0.91 | 0.47 |
| gen_ts | count_ge | eviction_sweep_256MiB | 8991.0 | 1.04 | 1.68 |
| gen_ts | count_ge | repeated | 5778.6 | 0.85 | 2.05 |
| gen_ts | points_materialized | eviction_sweep_256MiB | 201027.0 | 1.05 | 0.09 |
| gen_ts | points_materialized | repeated | 166249.3 | 1.02 | 0.02 |
| gen_ts | selected_sum_period3 | eviction_sweep_256MiB | 82896.0 | 1.02 | 0.26 |
| gen_ts | selected_sum_period3 | repeated | 72035.9 | 1.00 | 0.24 |
| gen_ts | sum | eviction_sweep_256MiB | 32751.0 | 0.40 | 0.30 |
| gen_ts | sum | repeated | 23082.1 | 0.31 | 0.16 |
