# Geometry/value ownership scorecard

All pairs are **frozen 35f05633 → current**, absolute nanoseconds. Each entry is the median of five alternating process trials. The CSV retains all samples, extrema, checksums verified by the reporter, and reported stored/binding/scratch footprints. The ownership cohort is the final shared-walker code; geometry encodings use the earlier identical-codec cohort. Source/host recovery is in `sources.json`.

C1 is a dependent request chain; C8 is time per request across eight independent chains. A native request consumes sixteen decoded slots and an actual payload word. Block packing is time per block across either a dependent block stream or eight blocks; it is not per-slot latency. Projection and range entries are ns/value. None includes allocation or version publication.

## Value readers: one 256-rank block

Maximum 4095, variable reservations, U and tombstones present, grouped lengths. These are illustrative complete consumers; all partial/omitted/fixed/population cases remain in `ownership.csv`.

| Profile | Geometry | C1 ns/value | C8 ns/value | Shuffled projection ns/value | Carried range ns/value |
| --- | --- | ---: | ---: | ---: | ---: |
| Neoverse V2 NEON | direct | 6.49 → 6.49 | 1.34 → 1.43 | 1.61 → 1.62 | 1.45 → 1.39 |
| Neoverse V2 NEON | width | 11.48 → 11.60 | 2.69 → 2.68 | 2.53 → 2.51 | 1.45 → 1.44 |
| Zen 5 AVX2 | direct | 4.25 → 4.26 | 0.87 → 0.88 | 1.05 → 1.04 | 0.77 → 0.77 |
| Zen 5 AVX2 | width | 9.88 → 9.91 | 1.56 → 1.60 | 1.63 → 1.68 | 0.78 → 0.72 |
| Zen 5 AVX-512 | direct | 4.28 → 4.26 | 0.84 → 0.88 | 1.06 → 1.06 | 0.77 → 0.77 |
| Zen 5 AVX-512 | width | 9.90 → 9.87 | 1.56 → 1.63 | 1.64 → 1.69 | 0.88 → 0.78 |
| Granite AVX2 | direct | 5.83 → 5.80 | 1.18 → 1.21 | 1.43 → 1.55 | 1.05 → 1.05 |
| Granite AVX2 | width | 10.73 → 10.75 | 2.15 → 2.38 | 2.49 → 2.50 | 1.21 → 1.09 |
| Granite AVX-512 | direct | 5.86 → 5.83 | 1.20 → 1.21 | 1.44 → 1.45 | 1.05 → 1.11 |
| Granite AVX-512 | width | 10.70 → 10.68 | 2.11 → 2.23 | 2.45 → 2.46 | 1.45 → 1.05 |

## Native complete consumers

Same maximum/U/grouping; one block. Direct register consumption and the actual native CPS chain remain separate in the CSV. This slice is the direct native consumer.

| Profile | Geometry | C1 ns/group | C8 ns/group |
| --- | --- | ---: | ---: |
| Neoverse V2 NEON | direct | 24.93 → 24.92 | 9.06 → 8.56 |
| Neoverse V2 NEON | width | 27.23 → 27.39 | 10.80 → 10.55 |
| Zen 5 AVX2 | direct | 16.61 → 16.27 | 4.52 → 5.66 |
| Zen 5 AVX2 | width | 20.05 → 20.04 | 5.40 → 5.46 |
| Zen 5 AVX-512 | direct | 18.46 → 18.44 | 4.20 → 4.05 |
| Zen 5 AVX-512 | width | 21.54 → 21.48 | 4.85 → 4.86 |
| Granite AVX2 | direct | 17.57 → 17.81 | 5.78 → 5.67 |
| Granite AVX2 | width | 21.34 → 21.28 | 7.58 → 7.59 |
| Granite AVX-512 | direct | 18.37 → 18.64 | 5.09 → 5.17 |
| Granite AVX-512 | width | 20.18 → 20.26 | 5.82 → 5.84 |

## Writes and packing

Writes include consumed source-qualified effects; append also includes periodic block reset. Maximum 4095 and U present. Packing uses grouped lengths and shrinks live slack; input restoration and model checks are outside its timer.

| Profile | Geometry | Replace C1 ns/op | Replace C8 ns/op | Append C1 ns/op | Append C8 ns/op | Pack dependent ns/block | Pack eight ns/block |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Neoverse V2 NEON | direct | 18.84 → 19.04 | 12.47 → 12.50 | 13.19 → 13.94 | 19.31 → 19.53 | 2177.09 → 2077.50 | 2131.32 → 2112.50 |
| Neoverse V2 NEON | width | 25.09 → 25.18 | 16.31 → 15.90 | 16.42 → 17.62 | 19.24 → 19.59 | 2371.34 → 2206.03 | 2291.03 → 2243.10 |
| Zen 5 AVX2 | direct | 15.33 → 15.22 | 8.47 → 8.37 | 9.70 → 10.33 | 11.17 → 11.35 | 1112.84 → 1101.25 | 1126.29 → 1112.74 |
| Zen 5 AVX2 | width | 22.25 → 21.97 | 11.03 → 10.97 | 10.62 → 11.38 | 11.23 → 11.53 | 1239.06 → 1230.00 | 1269.23 → 1227.82 |
| Zen 5 AVX-512 | direct | 15.30 → 15.24 | 8.51 → 8.39 | 9.82 → 10.31 | 10.96 → 11.28 | 1125.00 → 1101.88 | 1137.07 → 1121.68 |
| Zen 5 AVX-512 | width | 22.38 → 21.92 | 10.99 → 10.98 | 10.31 → 11.35 | 10.92 → 11.44 | 1241.25 → 1219.69 | 1241.61 → 1220.75 |
| Granite AVX2 | direct | 18.95 → 19.01 | 11.59 → 11.32 | 16.06 → 16.19 | 17.82 → 16.34 | 1700.38 → 1691.28 | 1564.77 → 1630.57 |
| Granite AVX2 | width | 25.40 → 25.46 | 15.65 → 15.04 | 16.12 → 15.84 | 16.56 → 17.58 | 1792.03 → 1828.94 | 1707.94 → 1753.31 |
| Granite AVX-512 | direct | 18.76 → 18.97 | 11.63 → 11.39 | 14.96 → 16.14 | 17.76 → 16.33 | 1678.84 → 1653.53 | 1549.45 → 1647.12 |
| Granite AVX-512 | width | 25.62 → 25.96 | 15.33 → 15.42 | 14.97 → 16.44 | 16.34 → 17.85 | 1800.66 → 1821.31 | 1706.62 → 1775.53 |

## Losing cells remain part of the assessment

Below is the largest median ratio for each profile/consumer family, selected to expose adverse cases rather than describe a typical operation. Before/after trial ranges show variation; overlapping ranges do not prove equality, and non-overlap over five trials is not a statistical model. In particular, large dependent reads vary substantially between process runs.

| Profile | Consumer | Case | Before ns | After ns | Before trial range | After trial range |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| Neoverse V2 NEON | reads | layout=width, maximum=4095, rows=1048576, used=0, tombstones=1, fixed=0, placement=1, metric=dependent_value_chain | 134.88 | 154.98 | 114.31–143.67 | 118.24–165.68 |
| Neoverse V2 NEON | packets | layout=width, maximum=4095, nodes=4096, used=0, placement=0, consumer=direct_native, chains=8 | 11.20 | 11.42 | 11.16–11.39 | 11.17–11.44 |
| Neoverse V2 NEON | mutation | layout=width, maximum=255, nodes=64, used=0, metric=append_with_reset_effects, chains=1 | 12.97 | 14.30 | 12.70–13.52 | 13.99–14.98 |
| Neoverse V2 NEON | packing | layout=width, maximum=4095, used=0, placement=1, shrink=1, blocks=1 | 1840.56 | 1975.59 | 1827.91–1887.47 | 1970.12–1980.34 |
| Zen 5 AVX2 | reads | layout=width, maximum=255, rows=256, used=0, tombstones=1, fixed=0, placement=0, metric=carried_range | 0.67 | 0.80 | 0.66–0.67 | 0.80–0.80 |
| Zen 5 AVX2 | packets | layout=direct, maximum=4095, nodes=1, used=1, placement=0, consumer=direct_native, chains=8 | 4.52 | 5.66 | 4.51–4.83 | 5.57–5.67 |
| Zen 5 AVX2 | mutation | layout=width, maximum=255, nodes=64, used=1, metric=append_with_reset_effects, chains=1 | 10.34 | 11.46 | 10.17–10.51 | 11.33–11.84 |
| Zen 5 AVX2 | packing | layout=direct, maximum=255, used=0, placement=1, shrink=0, blocks=8 | 879.96 | 931.41 | 876.33–929.30 | 919.50–957.70 |
| Zen 5 AVX-512 | reads | layout=width, maximum=255, rows=256, used=1, tombstones=1, fixed=1, placement=0, metric=ordered_projection | 0.54 | 0.62 | 0.54–0.54 | 0.62–0.63 |
| Zen 5 AVX-512 | packets | layout=direct, maximum=255, nodes=1, used=1, placement=0, consumer=native_chain, chains=8 | 5.76 | 5.98 | 5.72–5.84 | 5.76–6.02 |
| Zen 5 AVX-512 | mutation | layout=width, maximum=255, nodes=64, used=0, metric=append_with_reset_effects, chains=8 | 9.06 | 10.01 | 8.99–9.34 | 9.86–10.08 |
| Zen 5 AVX-512 | packing | layout=direct, maximum=4095, used=1, placement=0, shrink=0, blocks=1 | 1099.69 | 1149.09 | 1092.81–1112.19 | 1085.62–1153.44 |
| Granite AVX2 | reads | layout=width, maximum=255, rows=16384, used=1, tombstones=1, fixed=0, placement=0, metric=carried_range | 1.07 | 1.41 | 1.07–1.12 | 1.00–1.43 |
| Granite AVX2 | packets | layout=direct, maximum=255, nodes=1, used=1, placement=1, consumer=native_chain, chains=8 | 7.31 | 7.46 | 7.30–7.48 | 7.31–7.56 |
| Granite AVX2 | mutation | layout=width, maximum=4095, nodes=64, used=1, metric=append_with_reset_effects, chains=8 | 16.56 | 17.58 | 16.39–16.79 | 17.50–17.85 |
| Granite AVX2 | packing | layout=direct, maximum=4095, used=1, placement=0, shrink=0, blocks=8 | 1525.82 | 1669.58 | 1492.35–1547.10 | 1637.54–1827.14 |
| Granite AVX-512 | reads | layout=width, maximum=255, rows=16384, used=1, tombstones=1, fixed=0, placement=0, metric=carried_range | 1.07 | 1.41 | 1.06–1.15 | 0.95–1.41 |
| Granite AVX-512 | packets | layout=direct, maximum=255, nodes=1, used=1, placement=0, consumer=direct_native, chains=1 | 18.51 | 18.99 | 18.42–18.99 | 18.70–19.17 |
| Granite AVX-512 | mutation | layout=width, maximum=4095, nodes=64, used=1, metric=append_with_reset_effects, chains=1 | 14.97 | 16.44 | 14.27–15.84 | 15.31–16.71 |
| Granite AVX-512 | packing | layout=direct, maximum=4095, used=0, placement=0, shrink=0, blocks=8 | 1292.62 | 1499.73 | 1260.95–1319.80 | 1462.06–1568.96 |

## Geometry-only encoding slice

One 256-rank node, maximum 4095. Times include admitted query preparation but omit the public generic signed/wider-domain frontend. Reverse includes the extent check. Six repetitions include forward/reverse format order. These are geometry controls, not qualified full StreamPack format replacements.

| Profile | Format | Encoded B | Point C1 ns | Point C8 ns | Reverse C1 ns | Reverse C8 ns | Change R C1 ns |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Neoverse V2 NEON | direct | 1058 | 5.73 | 1.54 | 31.18 | 14.34 | 44.12 |
| Neoverse V2 NEON | sizes | 614 | 11.02 | 2.53 | 35.84 | 25.37 | 14.21 |
| Neoverse V2 NEON | relative_ends | 614 | 6.59 | 2.10 | 26.31 | 7.74 | 18.48 |
| Neoverse V2 NEON | direct_checkpoints | 1122 | 5.74 | 1.54 | 25.19 | 8.04 | 47.01 |
| Neoverse V2 NEON | fused_direct_scan | 1058 | 5.74 | 1.51 | 28.78 | 24.21 | — |
| Neoverse V2 NEON | sizes_full_prefix | 614 | 10.97 | 2.54 | 30.47 | 8.28 | 14.19 |
| Zen 5 AVX2 | direct | 1058 | 4.21 | 0.99 | 18.99 | 8.78 | 19.05 |
| Zen 5 AVX2 | sizes | 614 | 10.66 | 1.75 | 23.70 | 12.87 | 8.33 |
| Zen 5 AVX2 | relative_ends | 614 | 4.65 | 1.58 | 15.90 | 4.37 | 10.07 |
| Zen 5 AVX2 | direct_checkpoints | 1122 | 4.23 | 1.00 | 16.00 | 4.10 | 21.21 |
| Zen 5 AVX2 | fused_direct_scan | 1058 | 4.24 | 0.99 | 17.63 | 6.75 | — |
| Zen 5 AVX2 | sizes_full_prefix | 614 | 10.62 | 1.76 | 18.61 | 4.85 | 8.46 |
| Zen 5 AVX-512 | direct | 1058 | 4.24 | 0.99 | 17.66 | 7.25 | 14.82 |
| Zen 5 AVX-512 | sizes | 614 | 10.62 | 1.71 | 19.34 | 4.45 | 5.27 |
| Zen 5 AVX-512 | relative_ends | 614 | 4.63 | 1.53 | 12.98 | 3.71 | 6.53 |
| Zen 5 AVX-512 | direct_checkpoints | 1122 | 4.24 | 0.99 | 13.60 | 3.58 | 18.64 |
| Zen 5 AVX-512 | fused_direct_scan | 1058 | 4.25 | 1.00 | 17.77 | 6.41 | — |
| Zen 5 AVX-512 | sizes_full_prefix | 614 | 10.61 | 1.71 | 16.71 | 4.21 | 5.24 |
| Granite AVX2 | direct | 1058 | 5.20 | 1.11 | 23.16 | 10.48 | 25.17 |
| Granite AVX2 | sizes | 614 | 10.73 | 2.55 | 26.07 | 15.88 | 10.96 |
| Granite AVX2 | relative_ends | 614 | 5.71 | 1.70 | 19.15 | 5.79 | 13.30 |
| Granite AVX2 | direct_checkpoints | 1122 | 5.22 | 1.07 | 19.79 | 5.92 | 27.98 |
| Granite AVX2 | fused_direct_scan | 1058 | 5.21 | 1.07 | 21.13 | 13.43 | — |
| Granite AVX2 | sizes_full_prefix | 614 | 10.83 | 2.54 | 20.29 | 6.70 | 10.93 |
| Granite AVX-512 | direct | 1058 | 5.21 | 1.07 | 21.24 | 8.76 | 18.66 |
| Granite AVX-512 | sizes | 614 | 10.82 | 2.45 | 19.75 | 5.24 | 7.60 |
| Granite AVX-512 | relative_ends | 614 | 5.75 | 1.72 | 15.08 | 4.03 | 9.01 |
| Granite AVX-512 | direct_checkpoints | 1122 | 5.20 | 1.10 | 18.18 | 4.40 | 24.60 |
| Granite AVX-512 | fused_direct_scan | 1058 | 5.21 | 1.08 | 23.32 | 11.65 | — |
| Granite AVX-512 | sizes_full_prefix | 614 | 10.76 | 2.40 | 17.52 | 5.59 | 7.60 |

Geometry backing bytes in the CSV count touched storage, not measured per-arm RSS or a verified hardware service level. The full matrix includes maxima 255/4095/4096/65535, nine active counts and four populations. Request-table calibration is retained without subtraction.
