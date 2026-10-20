# Boundlist production scorecard

All times are **median ns/request**. Serial is one result-dependent chain;
8-way/16-way columns are reciprocal throughput per request, not batch latency.
Hot-list tables include query preparation and dependent child consumption.
Setup and memory are separate; full min/max/repetitions and losing cells remain in the CSV matrices.

## Complete point consumer: 4,096 u32 endpoints, positive deltas

| Host / ISA | Ordinary serial | Bound serial | Ordinary batch8 | Bound batch8 |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 AVX2 | 28.68 | 23.28 | 7.88 | 6.18 |
| Zen 5 AVX-512 | 26.60 | 21.60 | 6.96 | 5.73 |
| Granite AVX2 | 32.41 | 28.86 | 10.64 | 7.83 |
| Granite AVX-512 | 29.49 | 27.73 | 9.65 | 7.00 |
| Neoverse V2 NEON | 40.50 | 33.73 | 10.85 | 8.53 |

## Complete point consumer: 16,384 u64 endpoints, positive deltas

| Host / ISA | Ordinary serial | Bound serial | Ordinary batch8 | Bound batch8 |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 AVX2 | 51.98 | 36.95 | 12.38 | 9.52 |
| Zen 5 AVX-512 | 49.31 | 33.10 | 11.04 | 7.82 |
| Granite AVX2 | 59.39 | 44.57 | 15.41 | 11.53 |
| Granite AVX-512 | 56.18 | 41.94 | 13.52 | 9.57 |
| Neoverse V2 NEON | 60.28 | 45.32 | 15.87 | 12.99 |

## Complete point consumer: 256 u32 endpoints, positive deltas

| Host / ISA | Ordinary serial | Bound serial | Ordinary batch8 | Bound batch8 |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 AVX2 | 18.62 | 18.68 | 5.92 | 5.91 |
| Zen 5 AVX-512 | 16.58 | 16.60 | 5.04 | 4.99 |
| Granite AVX2 | 21.57 | 21.57 | 7.26 | 7.70 |
| Granite AVX-512 | 18.66 | 18.66 | 6.13 | 6.26 |
| Neoverse V2 NEON | 28.49 | 28.46 | 8.34 | 8.77 |

## Complete occupied-extrema range: 4,096 u32 ranks, duplicates and population

| Host / implementation | Serial | 8-way |
| --- | ---: | ---: |
| Zen 5 AVX2 · Ordinary | 50.41 | 45.09 |
| Zen 5 AVX2 · Bound | 50.81 | 44.15 |
| Zen 5 AVX2 · Fused binary | 86.96 | 93.88 |
| Zen 5 AVX-512 · Ordinary | 43.97 | 39.17 |
| Zen 5 AVX-512 · Bound | 48.41 | 42.40 |
| Zen 5 AVX-512 · Fused binary | 79.09 | 83.86 |
| Granite AVX2 · Ordinary | 60.33 | 56.47 |
| Granite AVX2 · Bound | 61.84 | 54.45 |
| Granite AVX2 · Fused binary | 107.12 | 105.52 |
| Granite AVX-512 · Ordinary | 53.48 | 47.34 |
| Granite AVX-512 · Bound | 60.88 | 57.75 |
| Granite AVX-512 · Fused binary | 91.76 | 95.23 |
| Neoverse V2 NEON · Ordinary | 77.98 | 72.30 |
| Neoverse V2 NEON · Bound | 56.39 | 48.83 |
| Neoverse V2 NEON · Fused binary | 110.62 | 112.72 |

## Complete physical range: W16 sidecar, 256 ranks

| Host / ISA | Before serial | Adopted serial | Before 8-way | Adopted 8-way |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 AVX2 | 41.83 | 34.07 | 29.61 | 19.24 |
| Zen 5 AVX-512 | 37.97 | 23.28 | 21.61 | 8.32 |
| Granite AVX2 | 51.14 | 39.72 | 47.97 | 25.59 |
| Granite AVX-512 | 44.09 | 26.74 | 36.15 | 10.78 |
| Neoverse V2 NEON | 61.02 | 55.82 | 45.78 | 38.41 |

## Large random range consumer: full nodes

| Host / ISA | Raw serial | Typed serial | Raw Loom16 | Typed Loom16 |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 AVX2 | 467.48 | 466.62 | 35.93 | 37.94 |
| Zen 5 AVX-512 | 467.45 | 466.71 | 35.30 | 37.17 |
| Granite AVX2 | 413.29 | 405.71 | 43.53 | 46.81 |
| Granite AVX-512 | 404.69 | 383.74 | 43.15 | 43.50 |
| Neoverse V2 NEON | 400.67 | 403.31 | 46.24 | 51.46 |

## Large random range consumer: mixed 64–256 ranks

| Host / ISA | Raw serial | Typed serial | Raw Loom16 | Typed Loom16 |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 AVX2 | 387.28 | 340.31 | 35.87 | 36.36 |
| Zen 5 AVX-512 | 385.81 | 339.20 | 35.41 | 35.83 |
| Granite AVX2 | 308.43 | 260.82 | 40.59 | 41.22 |
| Granite AVX-512 | 312.25 | 265.32 | 40.11 | 41.68 |
| Neoverse V2 NEON | 339.30 | 295.99 | 38.31 | 37.81 |

Both controls use 64B owner frames. The active Width operand bound is 288MiB;
Granite reports 480MiB L3, so this is not a proven above-LLC/DRAM table.
The fixed-node typed phase has a measurable throughput cost against the raw
geometry control; the mixed-node phase can avoid absent-read work. Hints are
line-covered and the scheduler is the actual Loom Ring.

## Upper-bound counterexample: 4,096 u32 ranks, runs of 80

| Host / ISA | Ordinary serial | Bound serial | Ordinary batch8 | Bound batch8 |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 AVX2 | 21.76 | 21.76 | 6.06 | 6.30 |
| Zen 5 AVX-512 | 19.51 | 19.52 | 5.83 | 6.39 |
| Granite AVX2 | 23.07 | 23.12 | 8.16 | 9.43 |
| Granite AVX-512 | 20.69 | 20.76 | 7.92 | 8.43 |
| Neoverse V2 NEON | 27.77 | 27.78 | 7.16 | 8.37 |

This panel uses prepared queries, an 8B selected child and a full 1024-query
feedback cycle. It is a separate envelope from the generated-query/hash panel.
The heuristic declines interpolation here; binding/batch plumbing can still cost
more than a raw source. Use ordinary sources for this regime.

## Source preparation: repeated hot loop, N4096/u32/positive deltas

| Host / ISA | Loop-mean ns/bind | Total binding bytes | Extra over raw16B |
| --- | ---: | ---: | ---: |
| Zen 5 AVX2 | 10.186 | 40 | 24 |
| Zen 5 AVX-512 | 13.108 | 40 | 24 |
| Granite AVX2 | 15.943 | 40 | 24 |
| Granite AVX-512 | 19.886 | 40 | 24 |
| Neoverse V2 NEON | 10.428 | 40 | 24 |

Each setup observation averages 4096 cache-hot constructions, not independent
repetitions or cold-source setup. Rebuild after endpoint/count/placement changes;
version lifetime determines whether repeated-query savings amortize setup.
Encoded sidecar bytes and 16B typed accesses are unchanged. Width phases are 16B
(population adds a borrowed-source pointer); numeric query phases are 32B.
