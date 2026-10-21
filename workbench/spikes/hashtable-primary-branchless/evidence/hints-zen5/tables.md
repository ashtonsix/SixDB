# Predictable-first lookup hints: native scorecard

Native Zen 5 AVX-512 whole-table independent reads, ns/lookup (lower is better). Each full-cohort cell is the median of 18 intervals in six fresh processes, three seeds. All arms consume the complete hit/key/value answer. Original Ikea and the actual Boost/Abseil controls retain their original callback loops; the two getter specializations and selector use the corresponding consumer loop. Inputs, encoded geometry and table-owned memory are fixed.

## Hot independent reads

| W | Value bytes | Miss % | Original | Predictable | Unpredictable | Selected | Boost | Abseil |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 32 | 0 | 0 | 0.964 | 0.954 | 1.478 | 1.000 | 1.410 | 1.255 |
| 32 | 0 | 50 | 6.909 | 6.907 | 1.468 | 1.565 | 6.378 | 6.395 |
| 32 | 0 | 100 | 1.089 | 1.029 | 1.457 | 1.074 | 1.192 | 1.552 |
| 32 | 8 | 0 | 1.408 | 1.366 | 2.090 | 1.420 | 1.797 | 1.729 |
| 32 | 8 | 50 | 7.094 | 7.024 | 2.157 | 2.493 | 6.317 | 6.608 |
| 32 | 8 | 100 | 1.126 | 1.087 | 2.396 | 1.161 | 1.165 | 1.754 |
| 40 | 0 | 0 | 2.129 | 2.058 | 2.868 | 2.110 | 1.434 | 1.258 |
| 40 | 0 | 50 | 8.760 | 8.869 | 2.879 | 2.956 | 6.139 | 6.273 |
| 40 | 0 | 100 | 2.253 | 2.174 | 2.877 | 2.172 | 1.102 | 1.575 |
| 40 | 8 | 0 | 3.063 | 2.888 | 3.980 | 2.898 | 1.743 | 1.749 |
| 40 | 8 | 50 | 9.117 | 8.898 | 3.979 | 4.048 | 6.324 | 6.647 |
| 40 | 8 | 100 | 2.723 | 2.489 | 3.872 | 2.558 | 1.137 | 1.774 |

## Predictable 50/50 and workload shifts

Ordering controls retain the exact multiset and within-class order. Phased traces rotate through all-hit, random, all-miss and alternating segments. Segment lengths are 131,071 / 16,381 / 509 requests with a 7,759-request offset, so transitions do not line up with the selector window. The selector sees only measured costs, never the phase label or membership oracle.

| W | Value bytes | Order | Original | Predictable | Unpredictable | Selected | Selected U % | Switches/trace |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 32 | 0 | alternating | 1.022 | 0.992 | 1.470 | 1.039 | 1.59 | 0 |
| 32 | 0 | 64-runs | 1.069 | 1.080 | 1.474 | 1.060 | 1.59 | 0 |
| 32 | 0 | grouped | 1.024 | 0.990 | 1.464 | 1.029 | 1.59 | 0 |
| 32 | 0 | 131071-phase | 2.564 | 2.551 | 1.470 | 1.243 | 26.98 | 4 |
| 32 | 0 | 16381-phase | 2.486 | 2.456 | 1.467 | 1.875 | 25.40 | 30 |
| 32 | 0 | 509-phase | 2.536 | 2.501 | 1.467 | 2.181 | 36.51 | 6 |
| 32 | 8 | alternating | 1.282 | 1.257 | 2.159 | 1.326 | 1.59 | 0 |
| 32 | 8 | 64-runs | 1.302 | 1.250 | 2.149 | 1.318 | 1.59 | 0 |
| 32 | 8 | grouped | 1.273 | 1.225 | 2.245 | 1.288 | 1.59 | 0 |
| 32 | 8 | 131071-phase | 2.805 | 2.751 | 2.198 | 1.683 | 26.98 | 4 |
| 32 | 8 | 16381-phase | 2.726 | 2.664 | 2.200 | 2.309 | 25.40 | 30 |
| 32 | 8 | 509-phase | 2.771 | 2.718 | 2.152 | 2.680 | 33.33 | 6 |
| 40 | 0 | alternating | 2.240 | 2.089 | 2.873 | 2.139 | 1.59 | 0 |
| 40 | 0 | 64-runs | 2.201 | 2.095 | 2.873 | 2.137 | 1.59 | 0 |
| 40 | 0 | grouped | 2.189 | 2.117 | 2.861 | 2.146 | 1.59 | 0 |
| 40 | 0 | 131071-phase | 3.931 | 3.877 | 2.869 | 2.426 | 26.98 | 4 |
| 40 | 0 | 16381-phase | 3.823 | 3.772 | 2.864 | 3.156 | 25.40 | 30 |
| 40 | 0 | 509-phase | 3.882 | 3.827 | 2.869 | 3.550 | 34.92 | 6 |
| 40 | 8 | alternating | 2.990 | 2.830 | 3.976 | 2.852 | 1.59 | 0 |
| 40 | 8 | 64-runs | 2.962 | 2.769 | 3.981 | 2.781 | 1.59 | 0 |
| 40 | 8 | grouped | 2.893 | 2.691 | 3.930 | 2.731 | 1.59 | 0 |
| 40 | 8 | 131071-phase | 4.546 | 4.353 | 3.946 | 3.164 | 26.98 | 4 |
| 40 | 8 | 16381-phase | 4.452 | 4.274 | 3.952 | 3.845 | 25.40 | 30 |
| 40 | 8 | 509-phase | 4.510 | 4.328 | 3.957 | 4.243 | 33.33 | 6 |

## Cold independent reads

Population 14,260,633; each interval follows a 256 MiB conditioning scan. Prefetch32 means home prefetch 32 useful requests ahead for Ikea/Abseil. Boost has native internal candidate prefetch, without that public home hook; its native result is repeated in prefetch32 rows. The selector preserves lookahead across internal block boundaries.

| W | Value bytes | Prefetch | Miss % | Original | Predictable | Unpredictable | Selected | Boost native | Abseil | Selected U % |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 40 | 0 | 0 | 0 | 10.803 | 10.785 | 17.868 | 10.992 | 21.504 | 16.491 | 1.59 |
| 40 | 0 | 0 | 50 | 15.805 | 15.200 | 17.872 | 15.417 | 22.216 | 17.567 | 1.59 |
| 40 | 0 | 0 | 100 | 12.397 | 11.622 | 17.822 | 11.895 | 13.653 | 12.123 | 1.59 |
| 40 | 0 | 32 | 0 | 5.159 | 4.991 | 5.623 | 5.044 | 21.504 | 12.362 | 3.17 |
| 40 | 0 | 32 | 50 | 8.066 | 8.159 | 5.675 | 5.656 | 22.216 | 15.033 | 95.24 |
| 40 | 0 | 32 | 100 | 5.164 | 4.918 | 5.675 | 4.983 | 13.653 | 10.830 | 3.17 |
| 40 | 8 | 0 | 0 | 18.943 | 19.022 | 28.327 | 19.147 | 26.784 | 21.790 | 1.59 |
| 40 | 8 | 0 | 50 | 20.355 | 19.476 | 29.621 | 19.966 | 25.026 | 20.910 | 3.17 |
| 40 | 8 | 0 | 100 | 12.914 | 11.960 | 22.337 | 12.279 | 14.304 | 13.038 | 1.59 |
| 40 | 8 | 32 | 0 | 17.949 | 16.420 | 25.605 | 16.779 | 26.784 | 20.881 | 2.38 |
| 40 | 8 | 32 | 50 | 16.168 | 14.802 | 21.971 | 15.037 | 25.026 | 19.430 | 1.59 |
| 40 | 8 | 32 | 100 | 4.975 | 4.813 | 5.970 | 4.875 | 14.304 | 11.963 | 3.17 |
| 50 | 0 | 0 | 0 | 28.443 | 28.360 | 29.826 | 28.468 | 21.443 | 16.488 | 1.59 |
| 50 | 0 | 0 | 50 | 30.705 | 30.362 | 30.059 | 30.416 | 22.161 | 17.546 | 3.17 |
| 50 | 0 | 0 | 100 | 28.676 | 28.554 | 29.883 | 28.523 | 13.594 | 12.158 | 2.38 |
| 50 | 0 | 32 | 0 | 6.028 | 5.818 | 6.116 | 5.846 | 21.443 | 12.357 | 7.14 |
| 50 | 0 | 32 | 50 | 9.082 | 9.000 | 6.127 | 6.168 | 22.161 | 15.118 | 95.24 |
| 50 | 0 | 32 | 100 | 6.044 | 5.794 | 6.094 | 5.836 | 13.594 | 10.831 | 6.35 |
| 50 | 8 | 0 | 0 | 36.775 | 36.633 | 43.224 | 35.061 | 26.821 | 21.835 | 2.38 |
| 50 | 8 | 0 | 50 | 37.757 | 37.016 | 43.460 | 37.251 | 25.053 | 20.953 | 3.17 |
| 50 | 8 | 0 | 100 | 28.487 | 28.030 | 39.816 | 28.314 | 14.409 | 12.987 | 1.59 |
| 50 | 8 | 32 | 0 | 30.624 | 30.594 | 36.435 | 30.777 | 26.821 | 20.864 | 1.59 |
| 50 | 8 | 32 | 50 | 26.949 | 26.753 | 28.740 | 26.827 | 25.053 | 19.492 | 1.59 |
| 50 | 8 | 32 | 100 | 6.760 | 6.577 | 7.540 | 6.477 | 14.409 | 11.957 | 1.59 |

## W32 microbatch seam tax

Separate hot Ikea-only experiment: three fresh seed processes/type, two counterbalanced trials and three repetitions per method. Each interval processes 262,144 useful queries split into preallocated batches. Query generation/allocation is outside timing. Original/predictable/unpredictable/selected use the same immutable batches; selected state persists across them. These timings include per-batch calls and integration, so compare within each row; they are not bare per-get latency. No PMU is collected in this diagnostic. Vendor controls remain in the full study above.

| Value bytes | Batch | Original hit | Predictable hit | Selected hit | Original miss | Selected miss | Original random | Selected random |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | 1 | 5.227 | 5.425 | 13.178 | 5.438 | 13.046 | 9.508 | 14.891 |
| 0 | 8 | 1.287 | 1.318 | 2.268 | 1.407 | 2.247 | 7.366 | 2.664 |
| 0 | 32 | 1.023 | 1.019 | 1.228 | 1.174 | 1.291 | 7.235 | 1.811 |
| 0 | 64 | 1.003 | 1.001 | 1.107 | 1.159 | 1.172 | 7.125 | 1.702 |
| 0 | 256 | 0.997 | 0.983 | 1.013 | 1.150 | 1.101 | 7.018 | 1.645 |
| 0 | 1024 | 0.974 | 0.964 | 1.020 | 1.119 | 1.092 | 6.998 | 1.610 |
| 0 | 4096 | 0.971 | 0.961 | 1.007 | 1.113 | 1.080 | 6.970 | 1.654 |
| 8 | 1 | 5.896 | 5.791 | 13.574 | 5.677 | 13.355 | 9.843 | 16.136 |
| 8 | 8 | 1.724 | 1.677 | 2.505 | 1.509 | 2.324 | 7.634 | 3.626 |
| 8 | 32 | 1.512 | 1.470 | 1.691 | 1.213 | 1.398 | 7.472 | 2.845 |
| 8 | 64 | 1.467 | 1.421 | 1.543 | 1.191 | 1.281 | 7.336 | 2.745 |
| 8 | 256 | 1.463 | 1.412 | 1.461 | 1.183 | 1.192 | 7.225 | 2.583 |
| 8 | 1024 | 1.425 | 1.381 | 1.442 | 1.150 | 1.174 | 7.197 | 2.579 |
| 8 | 4096 | 1.412 | 1.373 | 1.430 | 1.152 | 1.166 | 7.184 | 2.565 |

## Actual mixed-read counter/selector work

| W | Value bytes | Arm | ns | Cycles/op | Instructions/op | Branch misses/op | U share % | Probes/1M |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 32 | 0 | original | 6.90945 | 31.01410 | 26.52279 | 0.50190 | 0.000 | 0 |
| 32 | 0 | predictable | 6.90709 | 31.10661 | 25.01944 | 0.50157 | 0.000 | 0 |
| 32 | 0 | unpredictable | 1.46794 | 6.60514 | 34.05059 | 0.00183 | 100.000 | 0 |
| 32 | 0 | adaptive | 1.56544 | 7.04068 | 34.20851 | 0.01196 | 98.413 | 124 |
| 32 | 0 | boost | 6.37824 | 28.71740 | 27.92677 | 0.52980 | 0.000 | 0 |
| 32 | 0 | abseil | 6.39461 | 28.79662 | 29.25096 | 0.52834 | 0.000 | 0 |
| 32 | 8 | original | 7.09390 | 31.93112 | 32.01855 | 0.50108 | 0.000 | 0 |
| 32 | 8 | predictable | 7.02435 | 31.64622 | 30.51708 | 0.50211 | 0.000 | 0 |
| 32 | 8 | unpredictable | 2.15653 | 9.70740 | 41.04684 | 0.00183 | 100.000 | 0 |
| 32 | 8 | adaptive | 2.49269 | 11.22682 | 41.20019 | 0.01255 | 98.413 | 124 |
| 32 | 8 | boost | 6.31656 | 28.43574 | 29.45287 | 0.52927 | 0.000 | 0 |
| 32 | 8 | abseil | 6.60807 | 29.73555 | 32.75015 | 0.52587 | 0.000 | 0 |
| 40 | 0 | original | 8.76004 | 39.43740 | 58.50035 | 0.50038 | 0.000 | 0 |
| 40 | 0 | predictable | 8.86930 | 39.89863 | 57.00035 | 0.50037 | 0.000 | 0 |
| 40 | 0 | unpredictable | 2.87938 | 12.94789 | 65.00034 | 0.00002 | 100.000 | 0 |
| 40 | 0 | adaptive | 2.95573 | 13.32063 | 65.18638 | 0.00709 | 98.413 | 124 |
| 40 | 0 | boost | 6.13852 | 27.64090 | 26.91114 | 0.52895 | 0.000 | 0 |
| 40 | 0 | abseil | 6.27292 | 28.21362 | 30.26198 | 0.52955 | 0.000 | 0 |
| 40 | 8 | original | 9.11733 | 41.05400 | 69.53981 | 0.50126 | 0.000 | 0 |
| 40 | 8 | predictable | 8.89847 | 40.05299 | 67.54023 | 0.50092 | 0.000 | 0 |
| 40 | 8 | unpredictable | 3.97921 | 17.93113 | 78.07819 | 0.00272 | 100.000 | 0 |
| 40 | 8 | adaptive | 4.04809 | 18.23981 | 78.23602 | 0.01328 | 98.413 | 124 |
| 40 | 8 | boost | 6.32353 | 28.47801 | 28.43641 | 0.52714 | 0.000 | 0 |
| 40 | 8 | abseil | 6.64698 | 29.87559 | 34.76245 | 0.52748 | 0.000 | 0 |
