# Branchless primary: complete comparisons

Native Zen 5 AVX-512 independent reads, ns/lookup (lower is better). Each cell is the median of 18 intervals in six fresh processes, three seeds; repetitions within a process are not independent trials.

The callback cohort retains the original consumer. Result, lean and carry cohorts use the same value-returning adapter for all libraries, sum every hit key and every value byte, and verify identical answers. The maintained adapter calls ordinary `find`; candidates alone have the research `lookup`. Geometry and owned bytes do not change across Ikea arms.

## Final hot comparison

| Key bits | Value bytes | Miss % | Original callback | Maintained result adapter | Carry | Boost adapter | Abseil adapter | Carry / maintained adapter |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 32 | 0 | 0 | 0.972 | 1.005 | 1.649 | 1.642 | 1.362 | 1.64× |
| 32 | 0 | 50 | 6.932 | 6.918 | 1.620 | 6.347 | 6.424 | 0.23× |
| 32 | 0 | 100 | 1.086 | 1.109 | 1.639 | 1.259 | 1.640 | 1.48× |
| 32 | 8 | 0 | 1.417 | 1.554 | 2.357 | 2.089 | 1.719 | 1.52× |
| 32 | 8 | 50 | 7.128 | 7.401 | 2.357 | 6.768 | 6.576 | 0.32× |
| 32 | 8 | 100 | 1.128 | 1.242 | 2.249 | 1.478 | 1.736 | 1.81× |
| 40 | 0 | 0 | 2.135 | 2.244 | 2.880 | 1.653 | 1.370 | 1.28× |
| 40 | 0 | 50 | 8.805 | 8.775 | 2.874 | 6.376 | 6.446 | 0.33× |
| 40 | 0 | 100 | 2.253 | 2.345 | 2.878 | 1.218 | 1.658 | 1.23× |
| 40 | 8 | 0 | 3.078 | 3.358 | 4.121 | 2.076 | 1.737 | 1.23× |
| 40 | 8 | 50 | 9.144 | 9.408 | 4.088 | 6.744 | 6.478 | 0.43× |
| 40 | 8 | 100 | 2.716 | 2.969 | 3.964 | 1.429 | 1.811 | 1.34× |

## Final cold comparison

Same logical population 14,260,633. Each interval follows a 256 MiB cache-conditioning scan. `32` means caller home prefetch 32 requests ahead for Ikea/Abseil; Boost uses its native internal prefetch and has no equivalent public home-address hook. Its native result is repeated in both rows for comparison.

| Key bits | Value bytes | Prefetch | Miss % | Original callback | Maintained adapter | Carry | Boost native adapter | Abseil adapter |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 40 | 0 | 0 | 0 | 10.971 | 13.495 | 20.286 | 24.445 | 18.959 |
| 40 | 0 | 0 | 50 | 16.058 | 17.591 | 20.250 | 25.923 | 19.487 |
| 40 | 0 | 0 | 100 | 12.571 | 14.362 | 20.182 | 15.756 | 13.986 |
| 40 | 0 | 32 | 0 | 5.181 | 5.399 | 5.871 | 24.445 | 13.643 |
| 40 | 0 | 32 | 50 | 8.120 | 8.317 | 5.883 | 25.923 | 16.457 |
| 40 | 0 | 32 | 100 | 5.201 | 5.460 | 6.098 | 15.756 | 12.300 |
| 40 | 8 | 0 | 0 | 19.570 | 22.523 | 32.557 | 32.633 | 24.787 |
| 40 | 8 | 0 | 50 | 20.751 | 23.603 | 33.646 | 30.487 | 23.250 |
| 40 | 8 | 0 | 100 | 12.988 | 14.875 | 25.577 | 17.284 | 15.923 |
| 40 | 8 | 32 | 0 | 18.198 | 20.514 | 26.881 | 32.633 | 25.893 |
| 40 | 8 | 32 | 50 | 16.370 | 19.202 | 22.850 | 30.487 | 22.933 |
| 40 | 8 | 32 | 100 | 5.021 | 5.393 | 6.170 | 17.284 | 14.050 |
| 50 | 0 | 0 | 0 | 28.920 | 29.477 | 30.807 | 24.891 | 19.047 |
| 50 | 0 | 0 | 50 | 31.291 | 32.108 | 30.890 | 25.870 | 19.367 |
| 50 | 0 | 0 | 100 | 29.243 | 29.942 | 31.096 | 15.787 | 13.922 |
| 50 | 0 | 32 | 0 | 6.057 | 6.222 | 6.538 | 24.891 | 13.787 |
| 50 | 0 | 32 | 50 | 9.152 | 9.234 | 6.533 | 25.870 | 16.471 |
| 50 | 0 | 32 | 100 | 6.088 | 6.384 | 6.537 | 15.787 | 12.588 |
| 50 | 8 | 0 | 0 | 37.873 | 39.548 | 45.055 | 32.569 | 25.123 |
| 50 | 8 | 0 | 50 | 38.751 | 41.917 | 45.186 | 30.455 | 23.296 |
| 50 | 8 | 0 | 100 | 29.362 | 31.038 | 38.135 | 17.677 | 16.247 |
| 50 | 8 | 32 | 0 | 31.292 | 37.099 | 37.696 | 32.569 | 26.006 |
| 50 | 8 | 32 | 50 | 27.586 | 30.525 | 29.789 | 30.455 | 22.663 |
| 50 | 8 | 32 | 100 | 6.863 | 7.080 | 7.821 | 17.677 | 14.199 |

## Paired hot variants

Each variant is compared with the baseline from its own counterbalanced cohort; do not treat changes between separate consumer/cohort baselines as a kernel effect.

| Cohort / arm | Key bits | Value bytes | Hit | Random 50/50 | Miss | Baseline hit / 50/50 / miss | 50/50 branch misses/op |
| --- | --- | --- | --- | --- | --- | --- | --- |
| callback/candidate | 32 | 0 | 1.661 | 1.656 | 1.652 | 0.972 / 6.932 / 1.086 | 0.00041 |
| callback/candidate | 32 | 8 | 2.233 | 7.322 | 1.820 | 1.417 / 7.128 / 1.128 | 0.50164 |
| callback/candidate | 40 | 0 | 3.061 | 3.055 | 3.047 | 2.135 / 8.805 / 2.253 | 0.00001 |
| callback/candidate | 40 | 8 | 4.107 | 9.630 | 3.679 | 3.078 / 9.144 / 2.716 | 0.50116 |
| result/result | 32 | 0 | 1.785 | 1.782 | 1.779 | 1.006 / 6.897 / 1.111 | 0.00041 |
| result/result | 32 | 8 | 2.558 | 2.539 | 2.498 | 1.540 / 7.389 / 1.242 | 0.00041 |
| result/result | 40 | 0 | 3.290 | 3.290 | 3.275 | 2.243 / 8.737 / 2.332 | 0.00001 |
| result/result | 40 | 8 | 4.638 | 4.590 | 4.517 | 3.329 / 9.395 / 2.941 | 0.00072 |
| result/dummy | 32 | 0 | 1.788 | 1.786 | 1.784 | 1.006 / 6.897 / 1.111 | 0.00041 |
| result/dummy | 32 | 8 | 2.543 | 7.608 | 1.917 | 1.540 / 7.389 / 1.242 | 0.50048 |
| result/dummy | 40 | 0 | 3.298 | 3.295 | 3.269 | 2.243 / 8.737 / 2.332 | 0.00001 |
| result/dummy | 40 | 8 | 4.765 | 4.759 | 4.641 | 3.329 / 9.395 / 2.941 | 0.00072 |
| lean/select | 32 | 0 | 1.788 | 1.804 | 1.791 | 1.007 / 6.907 / 1.106 | 0.00041 |
| lean/select | 32 | 8 | 2.769 | 2.754 | 2.687 | 1.563 / 7.394 / 1.253 | 0.00041 |
| lean/select | 40 | 0 | 3.312 | 3.306 | 3.297 | 2.250 / 8.749 / 2.333 | 0.00001 |
| lean/select | 40 | 8 | 4.899 | 4.879 | 4.762 | 3.340 / 9.422 / 2.971 | 0.00072 |
| lean/lean | 32 | 0 | 1.658 | 1.625 | 1.640 | 1.007 / 6.907 / 1.106 | 0.00193 |
| lean/lean | 32 | 8 | 2.628 | 2.617 | 2.550 | 1.563 / 7.394 / 1.253 | 0.00191 |
| lean/lean | 40 | 0 | 2.912 | 2.889 | 2.887 | 2.250 / 8.749 / 2.333 | 0.00001 |
| lean/lean | 40 | 8 | 4.503 | 4.467 | 4.303 | 3.340 / 9.422 / 2.971 | 0.00243 |
| carry/carry | 32 | 0 | 1.649 | 1.620 | 1.639 | 1.005 / 6.918 / 1.109 | 0.00193 |
| carry/carry | 32 | 8 | 2.357 | 2.357 | 2.249 | 1.554 / 7.401 / 1.242 | 0.00192 |
| carry/carry | 40 | 0 | 2.880 | 2.874 | 2.878 | 2.244 / 8.775 / 2.345 | 0.00001 |
| carry/carry | 40 | 8 | 4.121 | 4.088 | 3.964 | 3.358 / 9.408 / 2.969 | 0.00248 |

## Final mixed-read counters

| Key bits | Value bytes | Arm | ns | Cycles/op | Instructions/op | Branches/op | Branch misses/op |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 32 | 0 | base | 6.91794 | 30.94446 | 30.02072 | 2.50274 | 0.50128 |
| 32 | 0 | carry | 1.62042 | 7.24360 | 38.05292 | 2.00875 | 0.00193 |
| 32 | 0 | boost | 6.34700 | 28.39563 | 30.70345 | 3.04372 | 0.52973 |
| 32 | 0 | abseil | 6.42439 | 28.74491 | 30.74941 | 4.05595 | 0.52794 |
| 32 | 8 | base | 7.40074 | 33.07348 | 36.52128 | 3.00318 | 0.50134 |
| 32 | 8 | carry | 2.35657 | 10.48788 | 45.04960 | 2.01119 | 0.00192 |
| 32 | 8 | boost | 6.76788 | 30.34638 | 37.20274 | 4.54391 | 0.52967 |
| 32 | 8 | abseil | 6.57577 | 29.41900 | 37.24947 | 4.55639 | 0.53067 |
| 40 | 0 | base | 8.77514 | 39.24358 | 63.00109 | 2.50026 | 0.50089 |
| 40 | 0 | carry | 2.87431 | 12.87539 | 69.00032 | 2.00007 | 0.00001 |
| 40 | 0 | boost | 6.37560 | 28.38043 | 29.68743 | 3.04026 | 0.52751 |
| 40 | 0 | abseil | 6.44614 | 28.77816 | 31.76298 | 4.05899 | 0.52791 |
| 40 | 8 | base | 9.40846 | 42.22693 | 75.03822 | 4.00370 | 0.50162 |
| 40 | 8 | carry | 4.08805 | 18.30112 | 82.07426 | 3.01299 | 0.00248 |
| 40 | 8 | boost | 6.74437 | 30.25912 | 36.18768 | 4.54004 | 0.52767 |
| 40 | 8 | abseil | 6.47807 | 28.98326 | 39.26320 | 4.55918 | 0.52711 |

## Owned bytes

Same table inputs/capacities as the earlier memory study, selected by bytes rather than winning timing. Static code/constants are outside all table-owned allocation counts. Carry uses one shared eight-byte zero word per concrete type. W50 map8 retains the earlier 25% smaller-than-Boost reference and is not exactly memory-equal.

| Case | Value bytes | Ikea | Boost | Abseil |
| --- | --- | --- | --- | --- |
| hot32 | 0 | 131320 | 155708 | 163856 |
| hot32 | 8 | 393592 | 401460 | 425952 |
| hot40 | 0 | 262264 | 278584 | 294904 |
| hot40 | 8 | 393656 | 524336 | 557000 |
| cold40 | 0 | 268435576 | 285212728 | 301989880 |
| cold40 | 8 | 536871032 | 536870960 | 570425288 |
| cold50 | 0 | 268435576 | 285212728 | 301989880 |
| cold50 | 8 | 402718840 | 536870960 | 570425288 |
