# StreamPack extraction parity

Absolute medians from five counterbalanced reader/mutation repetitions and seven packing repetitions. Reader and mutation ns are per value/command; packing ns are per 256-rank block. C1 is dependent serial work; C8 reader/mutation values are reciprocal throughput per request. Packing B8 is an independent stream of eight complete blocks, not per-slot interleaving.

## Complete readers

The slice uses W16, grouped R/U, variable geometry with used lengths. Each cell is before → current; independent fused controls are retained in `reads.csv`.

| Host/ISA | Blocks | Geometry | C1 ns/value | C8 ns/value | Ordered ns/value | Shuffled ns/value | Carried ns/value |
| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| granite/avx2 | 1 | direct | 5.86 → 5.93 | 1.15 → 1.15 | 1.41 → 1.41 | 1.43 → 1.43 | 1.05 → 1.05 |
| granite/avx2 | 1 | width | 10.69 → 10.71 | 2.14 → 2.15 | 2.54 → 2.49 | 2.49 → 2.49 | 1.21 → 1.15 |
| granite/avx2 | 4096 | direct | 130.08 → 134.16 | 12.80 → 12.42 | 1.75 → 1.68 | 9.99 → 9.91 | 1.30 → 1.31 |
| granite/avx2 | 4096 | width | 177.51 → 183.86 | 13.84 → 13.39 | 3.35 → 3.38 | 10.30 → 10.55 | 1.35 → 1.29 |
| granite/avx512 | 1 | direct | 5.83 → 5.86 | 1.15 → 1.15 | 1.42 → 1.41 | 1.43 → 1.45 | 1.05 → 1.05 |
| granite/avx512 | 1 | width | 10.72 → 10.71 | 2.15 → 2.12 | 2.45 → 2.50 | 2.45 → 2.45 | 1.45 → 1.45 |
| granite/avx512 | 4096 | direct | 175.48 → 188.28 | 14.34 → 16.75 | 1.81 → 2.42 | 10.36 → 17.50 | 1.32 → 1.33 |
| granite/avx512 | 4096 | width | 169.81 → 163.74 | 13.15 → 13.07 | 2.91 → 3.11 | 10.06 → 10.13 | 1.35 → 1.30 |
| neon/neon | 1 | direct | 6.50 → 6.50 | 1.47 → 1.45 | 1.61 → 1.65 | 1.61 → 1.65 | 1.37 → 1.45 |
| neon/neon | 1 | width | 11.48 → 11.50 | 2.69 → 2.72 | 2.51 → 2.48 | 2.53 → 2.52 | 1.49 → 1.47 |
| neon/neon | 4096 | direct | 250.20 → 252.40 | 29.61 → 28.38 | 2.08 → 2.07 | 14.61 → 14.19 | 1.88 → 1.95 |
| neon/neon | 4096 | width | 232.94 → 233.43 | 28.09 → 27.63 | 2.80 → 2.80 | 14.48 → 14.82 | 1.99 → 1.91 |
| zen5/avx2 | 1 | direct | 4.29 → 4.29 | 0.85 → 0.84 | 1.03 → 1.03 | 1.04 → 1.07 | 0.74 → 0.77 |
| zen5/avx2 | 1 | width | 9.93 → 9.91 | 1.59 → 1.59 | 1.67 → 1.65 | 1.67 → 1.67 | 0.78 → 0.78 |
| zen5/avx2 | 4096 | direct | 152.01 → 132.47 | 23.05 → 16.76 | 1.16 → 1.12 | 20.44 → 17.98 | 1.18 → 1.14 |
| zen5/avx2 | 4096 | width | 110.19 → 110.11 | 15.51 → 15.72 | 1.67 → 1.66 | 18.38 → 18.42 | 0.86 → 0.86 |
| zen5/avx512 | 1 | direct | 4.30 → 4.29 | 0.84 → 0.84 | 1.03 → 1.03 | 1.04 → 1.06 | 0.74 → 0.77 |
| zen5/avx512 | 1 | width | 9.97 → 9.98 | 1.60 → 1.61 | 1.62 → 1.64 | 1.64 → 1.65 | 0.94 → 0.89 |
| zen5/avx512 | 4096 | direct | 119.59 → 128.80 | 16.65 → 15.93 | 1.17 → 1.17 | 17.13 → 17.27 | 1.07 → 1.03 |
| zen5/avx512 | 4096 | width | 113.33 → 108.32 | 13.81 → 15.89 | 1.82 → 1.84 | 18.40 → 18.19 | 1.08 → 1.01 |

## Mutations with source-qualified effects

Width/W16, grouped R/U. Append periodically resets the same first eight blocks; only replace spreads over the larger fixture. The clock includes the useful value consumer and the actual journal.

| Host/ISA | Allocated blocks | Operation | C1 ns/command | C8 ns/command |
| --- | ---: | --- | ---: | ---: |
| granite/avx2 | 8 | append_with_reset_effects | 14.32 → 15.20 | 16.72 → 16.59 |
| granite/avx2 | 8 | replace_with_effects | 21.15 → 21.06 | 14.18 → 14.19 |
| granite/avx2 | 4096 | append_with_reset_effects | 14.60 → 16.15 | 16.97 → 16.65 |
| granite/avx2 | 4096 | replace_with_effects | 104.44 → 101.03 | 48.84 → 49.61 |
| granite/avx512 | 8 | append_with_reset_effects | 14.63 → 15.25 | 16.44 → 16.42 |
| granite/avx512 | 8 | replace_with_effects | 21.35 → 21.52 | 13.53 → 13.45 |
| granite/avx512 | 4096 | append_with_reset_effects | 14.17 → 15.10 | 16.77 → 16.57 |
| granite/avx512 | 4096 | replace_with_effects | 136.85 → 106.26 | 67.81 → 61.00 |
| neon/neon | 8 | append_with_reset_effects | 16.79 → 17.29 | 19.30 → 19.20 |
| neon/neon | 8 | replace_with_effects | 20.65 → 20.65 | 14.66 → 14.70 |
| neon/neon | 4096 | append_with_reset_effects | 16.78 → 17.17 | 19.58 → 19.53 |
| neon/neon | 4096 | replace_with_effects | 231.48 → 233.50 | 124.96 → 122.29 |
| zen5/avx2 | 8 | append_with_reset_effects | 10.28 → 10.26 | 10.77 → 10.68 |
| zen5/avx2 | 8 | replace_with_effects | 18.78 → 18.82 | 9.75 → 9.76 |
| zen5/avx2 | 4096 | append_with_reset_effects | 10.54 → 10.55 | 11.75 → 11.80 |
| zen5/avx2 | 4096 | replace_with_effects | 108.94 → 80.73 | 46.09 → 40.96 |
| zen5/avx512 | 8 | append_with_reset_effects | 10.49 → 10.52 | 11.31 → 11.36 |
| zen5/avx512 | 8 | replace_with_effects | 19.97 → 20.12 | 10.35 → 10.50 |
| zen5/avx512 | 4096 | append_with_reset_effects | 11.40 → 11.37 | 11.72 → 12.15 |
| zen5/avx512 | 4096 | replace_with_effects | 125.23 → 92.38 | 54.92 → 54.74 |

## Actual soft packing

W16, grouped R/U, shrink enabled, every fifth rank retired, live U varies 8–20 and R is 32. Each trial restores the fragmented input outside its clock; both treatments use the same aligned journal storage. The selected loop captures the immutable assigned count before stores. Journal storage and its complete digest are included; independent byte/geometry checks run outside the clock. Clock-pair overhead is included. This is a conditioned block operation, not a cold-memory benchmark.

| Host/ISA | Geometry | Serial ns/block | B8 stream ns/block |
| --- | --- | ---: | ---: |
| granite/avx2 | direct | 1760.97 → 1755.80 | 1706.73 → 1545.26 |
| granite/avx2 | width | 1809.60 → 1773.54 | 1766.11 → 1691.67 |
| granite/avx512 | direct | 1664.83 → 1671.94 | 1739.59 → 1574.93 |
| granite/avx512 | width | 1800.73 → 1762.47 | 1796.79 → 1684.06 |
| neon/neon | direct | 2173.06 → 2116.49 | 2216.77 → 2171.61 |
| neon/neon | width | 2400.10 → 2322.25 | 2403.35 → 2357.67 |
| zen5/avx2 | direct | 1316.57 → 1185.04 | 1322.13 → 1276.38 |
| zen5/avx2 | width | 1406.29 → 1384.38 | 1446.80 → 1414.48 |
| zen5/avx512 | direct | 1201.41 → 1228.83 | 1294.99 → 1195.75 |
| zen5/avx512 | width | 1446.65 → 1463.52 | 1467.60 → 1435.34 |

## Full-surface differences

These are descriptive extrema across every paired case, not a pooled timing or a significance test. Positive delta means current is slower. Units remain distinct between families.

| Family | Host/ISA | Cells | Median ratio current/before | Largest ns loss | Largest ns gain |
| --- | --- | ---: | ---: | ---: | ---: |
| read ns/value | granite/avx2 | 240 | 1.000 | 21.00 | -12.97 |
| read ns/value | granite/avx512 | 240 | 1.000 | 30.01 | -33.20 |
| read ns/value | neon/neon | 240 | 1.000 | 2.85 | -4.07 |
| read ns/value | zen5/avx2 | 240 | 0.999 | 16.06 | -19.53 |
| read ns/value | zen5/avx512 | 240 | 1.000 | 85.66 | -9.90 |
| mutation ns/command | granite/avx2 | 64 | 1.000 | 1.55 | -11.09 |
| mutation ns/command | granite/avx512 | 64 | 0.998 | 1.24 | -46.49 |
| mutation ns/command | neon/neon | 64 | 1.000 | 3.41 | -6.75 |
| mutation ns/command | zen5/avx2 | 64 | 0.997 | 10.19 | -29.20 |
| mutation ns/command | zen5/avx512 | 64 | 0.998 | 0.78 | -32.85 |
| packing ns/block | granite/avx2 | 64 | 0.987 | 64.66 | -453.56 |
| packing ns/block | granite/avx512 | 64 | 0.983 | 91.02 | -281.01 |
| packing ns/block | neon/neon | 64 | 0.983 | 16.54 | -89.88 |
| packing ns/block | zen5/avx2 | 64 | 0.977 | 77.07 | -131.53 |
| packing ns/block | zen5/avx512 | 64 | 0.984 | 121.95 | -144.76 |

The full matrices preserve minimum/maximum and all repetitions by recoverable raw input. They include Direct/Width, W8/W16, optional U, grouped/separated placement, fixed/variable readers, and ordered/shuffled access. No new stored fields, allocation, count capacity or wire-format changes accompany extraction. Typed access is still 16 B; the stored schema is still 8 B.

Point/projection instruction comparison matches all 180 paired complete consumers after relocation/padding normalization. Large x86 timing swings in both directions therefore do not establish a runtime cost of the extraction seam; the raw ranges remain visible. Carried traversal is a distinct composition and has small remaining deltas. `packing_initial.csv` preserves the first experiment, before shared output placement and the count capture. It is not silently replaced by the follow-up.

Hardware receipts and process RSS are in the worker archives. Read fixtures report logical payload, allocated capacity, metadata and bindings separately. Large random reads here do not by themselves establish per-load DRAM service. Search, reverse and actual Loom comparisons remain in the separately retained graduation evidence.
