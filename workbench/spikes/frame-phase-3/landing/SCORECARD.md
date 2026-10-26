# Frame extraction scorecard

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

[Retained evidence](evidence/zen5-avx512/artifact.json).

| Column | Operation | Conditioning | Ikea ns/query | Ikea/spike | Native/Ikea |
| --- | --- | --- | ---: | ---: | ---: |
| EventTime | count_ge | eviction_sweep_256MiB | 71520.0 | 0.84 | 0.10 |
| EventTime | count_ge | repeated | 43052.1 | 0.74 | 0.04 |
| EventTime | points_materialized | eviction_sweep_256MiB | 171701.0 | 0.92 | 0.11 |
| EventTime | points_materialized | repeated | 59986.0 | 0.97 | 0.03 |
| EventTime | selected_sum_period3 | eviction_sweep_256MiB | 79080.0 | 1.29 | 0.10 |
| EventTime | selected_sum_period3 | repeated | 52936.8 | 1.47 | 0.05 |
| EventTime | sum | eviction_sweep_256MiB | 33940.0 | 0.96 | 0.20 |
| EventTime | sum | repeated | 8865.4 | 0.97 | 0.14 |
| OS | count_ge | eviction_sweep_256MiB | 12810.0 | 0.84 | 0.25 |
| OS | count_ge | repeated | 1960.3 | 1.01 | 0.78 |
| OS | points_materialized | eviction_sweep_256MiB | 105270.0 | 1.02 | 0.13 |
| OS | points_materialized | repeated | 59804.7 | 1.04 | 0.02 |
| OS | selected_sum_period3 | eviction_sweep_256MiB | 29800.0 | 1.01 | 0.15 |
| OS | selected_sum_period3 | repeated | 8784.4 | 1.03 | 0.31 |
| OS | sum | eviction_sweep_256MiB | 17190.0 | 1.02 | 0.16 |
| OS | sum | repeated | 2396.2 | 1.12 | 0.27 |
| URLHash | count_ge | eviction_sweep_256MiB | 20110.0 | 0.97 | 0.63 |
| URLHash | count_ge | repeated | 5323.8 | 1.07 | 0.42 |
| URLHash | points_materialized | eviction_sweep_256MiB | 146521.0 | 1.18 | 0.13 |
| URLHash | points_materialized | repeated | 17983.5 | 1.21 | 0.09 |
| URLHash | selected_sum_period3 | eviction_sweep_256MiB | 27840.0 | 0.52 | 1.39 |
| URLHash | selected_sum_period3 | repeated | 11214.1 | 0.35 | 0.49 |
| URLHash | sum | eviction_sweep_256MiB | 33180.0 | 0.63 | 0.40 |
| URLHash | sum | repeated | 3671.9 | 0.26 | 0.64 |
| UserID | count_ge | eviction_sweep_256MiB | 4460.0 | 1.03 | 2.74 |
| UserID | count_ge | repeated | 377.8 | 1.54 | 5.52 |
| UserID | points_materialized | eviction_sweep_256MiB | 189621.0 | 1.25 | 0.10 |
| UserID | points_materialized | repeated | 61371.6 | 1.05 | 0.03 |
| UserID | selected_sum_period3 | eviction_sweep_256MiB | 52830.0 | 0.86 | 0.74 |
| UserID | selected_sum_period3 | repeated | 13390.1 | 0.73 | 0.41 |
| UserID | sum | eviction_sweep_256MiB | 39021.0 | 0.85 | 0.34 |
| UserID | sum | repeated | 6189.7 | 0.72 | 0.39 |
| gen_ts | count_ge | eviction_sweep_256MiB | 5780.0 | 1.08 | 1.21 |
| gen_ts | count_ge | repeated | 2864.7 | 0.90 | 0.55 |
| gen_ts | points_materialized | eviction_sweep_256MiB | 199691.0 | 0.85 | 0.09 |
| gen_ts | points_materialized | repeated | 105003.8 | 1.02 | 0.01 |
| gen_ts | selected_sum_period3 | eviction_sweep_256MiB | 60000.0 | 1.04 | 0.36 |
| gen_ts | selected_sum_period3 | repeated | 41818.1 | 1.00 | 0.07 |
| gen_ts | sum | eviction_sweep_256MiB | 31681.0 | 0.55 | 0.21 |
| gen_ts | sum | repeated | 11474.5 | 0.28 | 0.11 |

## Zen 5 AVX2

[Retained evidence](evidence/zen5-avx2/artifact.json).

| Column | Operation | Conditioning | Ikea ns/query | Ikea/spike | Native/Ikea |
| --- | --- | --- | ---: | ---: | ---: |
| EventTime | count_ge | eviction_sweep_256MiB | 80991.0 | 0.88 | 0.11 |
| EventTime | count_ge | repeated | 45710.0 | 0.76 | 0.10 |
| EventTime | points_materialized | eviction_sweep_256MiB | 179011.0 | 0.98 | 0.10 |
| EventTime | points_materialized | repeated | 60776.3 | 0.96 | 0.02 |
| EventTime | selected_sum_period3 | eviction_sweep_256MiB | 83100.0 | 1.26 | 0.11 |
| EventTime | selected_sum_period3 | repeated | 53685.6 | 1.39 | 0.10 |
| EventTime | sum | eviction_sweep_256MiB | 39801.0 | 0.87 | 0.20 |
| EventTime | sum | repeated | 14115.4 | 0.99 | 0.15 |
| OS | count_ge | eviction_sweep_256MiB | 19070.0 | 0.85 | 0.34 |
| OS | count_ge | repeated | 5966.0 | 0.99 | 0.87 |
| OS | points_materialized | eviction_sweep_256MiB | 111870.0 | 1.02 | 0.13 |
| OS | points_materialized | repeated | 63576.7 | 1.04 | 0.03 |
| OS | selected_sum_period3 | eviction_sweep_256MiB | 31990.0 | 0.84 | 0.22 |
| OS | selected_sum_period3 | repeated | 8912.9 | 0.94 | 0.65 |
| OS | sum | eviction_sweep_256MiB | 20060.0 | 0.94 | 0.17 |
| OS | sum | repeated | 3131.9 | 1.10 | 0.37 |
| URLHash | count_ge | eviction_sweep_256MiB | 23970.0 | 1.05 | 0.55 |
| URLHash | count_ge | repeated | 8367.2 | 1.00 | 0.50 |
| URLHash | points_materialized | eviction_sweep_256MiB | 162751.0 | 1.25 | 0.12 |
| URLHash | points_materialized | repeated | 17945.8 | 1.20 | 0.09 |
| URLHash | selected_sum_period3 | eviction_sweep_256MiB | 32910.0 | 0.69 | 0.55 |
| URLHash | selected_sum_period3 | repeated | 20492.0 | 0.54 | 0.40 |
| URLHash | sum | eviction_sweep_256MiB | 26130.0 | 0.99 | 0.52 |
| URLHash | sum | repeated | 15599.2 | 1.14 | 0.25 |
| UserID | count_ge | eviction_sweep_256MiB | 5660.0 | 1.06 | 2.45 |
| UserID | count_ge | repeated | 480.9 | 1.36 | 8.33 |
| UserID | points_materialized | eviction_sweep_256MiB | 204052.0 | 1.22 | 0.10 |
| UserID | points_materialized | repeated | 62160.1 | 1.07 | 0.03 |
| UserID | selected_sum_period3 | eviction_sweep_256MiB | 55291.0 | 0.77 | 0.35 |
| UserID | selected_sum_period3 | repeated | 22312.3 | 0.85 | 0.37 |
| UserID | sum | eviction_sweep_256MiB | 43640.0 | 0.80 | 0.31 |
| UserID | sum | repeated | 15737.9 | 1.01 | 0.24 |
| gen_ts | count_ge | eviction_sweep_256MiB | 6590.0 | 1.13 | 1.19 |
| gen_ts | count_ge | repeated | 3254.1 | 0.93 | 1.50 |
| gen_ts | points_materialized | eviction_sweep_256MiB | 219772.0 | 1.03 | 0.09 |
| gen_ts | points_materialized | repeated | 112314.5 | 1.05 | 0.01 |
| gen_ts | selected_sum_period3 | eviction_sweep_256MiB | 55510.0 | 1.00 | 0.15 |
| gen_ts | selected_sum_period3 | repeated | 50434.1 | 1.04 | 0.11 |
| gen_ts | sum | eviction_sweep_256MiB | 44530.0 | 0.79 | 0.16 |
| gen_ts | sum | repeated | 33252.7 | 0.69 | 0.06 |

## Graviton 4 NEON

[Retained evidence](evidence/graviton4/artifact.json).

| Column | Operation | Conditioning | Ikea ns/query | Ikea/spike | Native/Ikea |
| --- | --- | --- | ---: | ---: | ---: |
| EventTime | count_ge | eviction_sweep_256MiB | 95745.0 | 1.19 | 0.16 |
| EventTime | count_ge | repeated | 71002.5 | 1.17 | 0.17 |
| EventTime | points_materialized | eviction_sweep_256MiB | 124302.0 | 1.07 | 0.14 |
| EventTime | points_materialized | repeated | 81738.1 | 1.06 | 0.04 |
| EventTime | selected_sum_period3 | eviction_sweep_256MiB | 113125.0 | 1.26 | 0.19 |
| EventTime | selected_sum_period3 | repeated | 88819.8 | 1.22 | 0.19 |
| EventTime | sum | eviction_sweep_256MiB | 37234.0 | 0.96 | 0.26 |
| EventTime | sum | repeated | 20315.1 | 0.99 | 0.18 |
| OS | count_ge | eviction_sweep_256MiB | 17328.0 | 0.92 | 0.92 |
| OS | count_ge | repeated | 7826.2 | 1.01 | 1.90 |
| OS | points_materialized | eviction_sweep_256MiB | 101041.0 | 0.98 | 0.08 |
| OS | points_materialized | repeated | 81219.3 | 1.17 | 0.03 |
| OS | selected_sum_period3 | eviction_sweep_256MiB | 32565.0 | 0.90 | 0.73 |
| OS | selected_sum_period3 | repeated | 14814.8 | 0.86 | 1.58 |
| OS | sum | eviction_sweep_256MiB | 19902.0 | 1.00 | 0.21 |
| OS | sum | repeated | 5439.6 | 1.04 | 0.47 |
| URLHash | count_ge | eviction_sweep_256MiB | 36637.0 | 1.08 | 0.54 |
| URLHash | count_ge | repeated | 23004.2 | 1.04 | 0.32 |
| URLHash | points_materialized | eviction_sweep_256MiB | 141302.0 | 1.17 | 0.15 |
| URLHash | points_materialized | repeated | 33222.3 | 1.26 | 0.11 |
| URLHash | selected_sum_period3 | eviction_sweep_256MiB | 45966.0 | 0.71 | 0.66 |
| URLHash | selected_sum_period3 | repeated | 32606.8 | 0.59 | 0.72 |
| URLHash | sum | eviction_sweep_256MiB | 32769.0 | 0.78 | 0.65 |
| URLHash | sum | repeated | 22442.2 | 0.73 | 0.49 |
| UserID | count_ge | eviction_sweep_256MiB | 7594.0 | 1.20 | 2.56 |
| UserID | count_ge | repeated | 1472.7 | 1.19 | 4.89 |
| UserID | points_materialized | eviction_sweep_256MiB | 161370.0 | 1.08 | 0.13 |
| UserID | points_materialized | repeated | 71348.2 | 1.16 | 0.05 |
| UserID | selected_sum_period3 | eviction_sweep_256MiB | 66663.0 | 0.91 | 0.47 |
| UserID | selected_sum_period3 | repeated | 43184.6 | 0.91 | 0.54 |
| UserID | sum | eviction_sweep_256MiB | 45049.0 | 0.93 | 0.47 |
| UserID | sum | repeated | 23913.1 | 0.91 | 0.46 |
| gen_ts | count_ge | eviction_sweep_256MiB | 8777.0 | 0.97 | 1.82 |
| gen_ts | count_ge | repeated | 5793.2 | 0.90 | 2.04 |
| gen_ts | points_materialized | eviction_sweep_256MiB | 204876.0 | 1.05 | 0.09 |
| gen_ts | points_materialized | repeated | 175599.8 | 1.08 | 0.02 |
| gen_ts | selected_sum_period3 | eviction_sweep_256MiB | 84368.0 | 1.02 | 0.26 |
| gen_ts | selected_sum_period3 | repeated | 74725.6 | 1.05 | 0.23 |
| gen_ts | sum | eviction_sweep_256MiB | 33822.0 | 0.40 | 0.30 |
| gen_ts | sum | repeated | 23126.9 | 0.32 | 0.16 |
