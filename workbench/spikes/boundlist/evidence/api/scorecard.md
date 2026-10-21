# API cleanup: absolute complete-consumer samples

Medians in ns/request. C1 is dependent latency; C8 is reciprocal throughput
from eight independent requests. Neither is batch latency. Bytes are logical
encoded bytes and granted backing, not an inferred cache-residence label.


## granite-rapids / avx2 / existing values / stream

| Source / encoding | Operation | Before C1 | After C1 | Fused C1 | Before C8 | After C8 | Fused C8 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Absolute Ends | Ordinary reverse → value | 27.14 | 26.95 | — | 16.17 | 16.50 | — |
| Absolute Ends | Typed reverse → value | 26.23 | 26.31 | 16.78 | 13.12 | 13.27 | 15.12 |
| Absolute Ends | Value by rank | 5.10 | 5.13 | 5.35 | 2.28 | 2.26 | 2.29 |
| Sizes | Ordinary reverse → value | 28.52 | 28.72 | — | 12.55 | 12.68 | — |
| Sizes | Typed reverse → value | 27.36 | 27.65 | 25.23 | 10.85 | 10.95 | 9.73 |
| Sizes | Value by rank | 10.51 | 10.50 | 10.49 | 3.25 | 3.25 | 3.23 |
| Relative Ends | Ordinary reverse → value | 24.37 | 24.48 | — | 12.00 | 12.22 | — |
| Relative Ends | Typed reverse → value | 22.18 | 22.39 | 18.53 | 9.16 | 9.30 | 7.96 |
| Relative Ends | Value by rank | 7.00 | 7.05 | 6.00 | 2.82 | 2.83 | 2.73 |

## granite-rapids / avx2 / geometry API / geometry

| Source / encoding | Operation | Before C1 | After C1 | Fused C1 | Before C8 | After C8 | Fused C8 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| u16 | Point → rank → payload | 20.81 | 20.99 | 12.11 | 7.41 | 7.68 | 13.60 |
| u16 | Range → window → payload | 24.70 | 24.35 | 17.10 | 15.27 | 15.09 | 20.84 |
| u16 | Rank → bounds → payload | 5.09 | 4.95 | 4.24 | 0.79 | 1.05 | 0.70 |
| u64 | Point → rank → payload | 23.64 | 23.65 | 29.62 | 10.72 | 10.22 | 29.94 |
| u64 | Range → window → payload | 30.18 | 29.35 | 59.71 | 21.40 | 20.61 | 61.44 |
| u64 | Rank → bounds → payload | 5.09 | 4.94 | 4.24 | 0.79 | 1.00 | 0.69 |

## granite-rapids / avx2 / geometry API / stream

| Source / encoding | Operation | Before C1 | After C1 | Fused C1 | Before C8 | After C8 | Fused C8 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Absolute Ends | Raw point → value | 24.77 | 26.03 | — | 11.96 | 11.95 | — |
| Absolute Ends | Selected point → value | 24.80 | 26.09 | — | 12.13 | 12.11 | — |
| Absolute Ends | Selected range → value | 35.30 | 35.18 | — | 29.21 | 29.13 | — |
| Sizes | Raw point → value | 26.95 | 27.05 | — | 10.30 | 10.25 | — |
| Sizes | Selected point → value | 26.96 | 27.01 | — | 10.76 | 10.91 | — |
| Sizes | Selected range → value | 40.24 | 40.84 | — | 29.37 | 29.86 | — |
| Relative Ends | Raw point → value | 22.02 | 24.03 | — | 9.06 | 9.11 | — |
| Relative Ends | Selected point → value | 22.49 | 24.05 | — | 9.43 | 9.70 | — |
| Relative Ends | Selected range → value | 35.91 | 36.61 | — | 29.68 | 30.16 | — |

## granite-rapids / avx512 / existing values / stream

| Source / encoding | Operation | Before C1 | After C1 | Fused C1 | Before C8 | After C8 | Fused C8 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Absolute Ends | Ordinary reverse → value | 26.60 | 26.75 | — | 14.34 | 14.47 | — |
| Absolute Ends | Typed reverse → value | 25.67 | 25.82 | 15.12 | 13.02 | 12.92 | 7.80 |
| Absolute Ends | Value by rank | 5.11 | 5.11 | 5.35 | 2.29 | 2.30 | 2.30 |
| Sizes | Ordinary reverse → value | 26.79 | 26.90 | — | 12.52 | 12.46 | — |
| Sizes | Typed reverse → value | 26.32 | 26.36 | 26.50 | 9.87 | 9.90 | 10.62 |
| Sizes | Value by rank | 10.53 | 10.55 | 10.52 | 3.20 | 3.20 | 3.10 |
| Relative Ends | Ordinary reverse → value | 21.31 | 21.35 | — | 9.89 | 9.92 | — |
| Relative Ends | Typed reverse → value | 20.68 | 21.00 | 19.15 | 9.23 | 9.52 | 7.71 |
| Relative Ends | Value by rank | 7.02 | 7.02 | 6.01 | 2.81 | 2.81 | 2.70 |

## granite-rapids / avx512 / geometry API / geometry

| Source / encoding | Operation | Before C1 | After C1 | Fused C1 | Before C8 | After C8 | Fused C8 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| u16 | Point → rank → payload | 18.07 | 18.43 | 9.72 | 6.03 | 6.26 | 6.99 |
| u16 | Range → window → payload | 20.64 | 20.89 | 11.04 | 12.77 | 12.59 | 12.37 |
| u16 | Rank → bounds → payload | 5.09 | 5.00 | 4.23 | 0.78 | 1.03 | 0.68 |
| u64 | Point → rank → payload | 22.01 | 22.26 | 16.27 | 8.78 | 9.00 | 18.90 |
| u64 | Range → window → payload | 26.12 | 26.34 | 34.78 | 17.81 | 17.42 | 32.93 |
| u64 | Rank → bounds → payload | 5.04 | 4.95 | 4.25 | 0.78 | 0.99 | 0.70 |

## granite-rapids / avx512 / geometry API / stream

| Source / encoding | Operation | Before C1 | After C1 | Fused C1 | Before C8 | After C8 | Fused C8 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Absolute Ends | Raw point → value | 25.10 | 26.52 | — | 12.10 | 12.03 | — |
| Absolute Ends | Selected point → value | 25.07 | 26.46 | — | 12.43 | 12.25 | — |
| Absolute Ends | Selected range → value | 34.67 | 35.36 | — | 27.57 | 27.87 | — |
| Sizes | Raw point → value | 26.50 | 26.28 | — | 9.69 | 9.55 | — |
| Sizes | Selected point → value | 26.63 | 26.78 | — | 10.42 | 10.28 | — |
| Sizes | Selected range → value | 38.32 | 38.70 | — | 28.70 | 30.09 | — |
| Relative Ends | Raw point → value | 19.84 | 21.42 | — | 7.96 | 7.91 | — |
| Relative Ends | Selected point → value | 20.32 | 21.50 | — | 8.23 | 8.54 | — |
| Relative Ends | Selected range → value | 31.84 | 31.95 | — | 25.05 | 24.61 | — |

## neoverse-v2 / neon / existing values / stream

| Source / encoding | Operation | Before C1 | After C1 | Fused C1 | Before C8 | After C8 | Fused C8 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Absolute Ends | Ordinary reverse → value | 36.12 | 36.24 | — | 17.17 | 17.23 | — |
| Absolute Ends | Typed reverse → value | 33.25 | 33.24 | 31.45 | 17.46 | 17.40 | 27.55 |
| Absolute Ends | Value by rank | 5.59 | 5.52 | 5.86 | 2.79 | 2.76 | 2.81 |
| Sizes | Ordinary reverse → value | 40.17 | 40.17 | — | 18.08 | 18.15 | — |
| Sizes | Typed reverse → value | 37.86 | 37.91 | 34.01 | 17.44 | 17.45 | 13.31 |
| Sizes | Value by rank | 10.87 | 10.87 | 11.00 | 4.01 | 3.99 | 4.18 |
| Relative Ends | Ordinary reverse → value | 37.55 | 37.47 | — | 21.45 | 21.45 | — |
| Relative Ends | Typed reverse → value | 34.31 | 34.51 | 25.23 | 19.92 | 19.96 | 10.24 |
| Relative Ends | Value by rank | 7.54 | 7.54 | 6.67 | 3.50 | 3.49 | 3.41 |

## neoverse-v2 / neon / geometry API / geometry

| Source / encoding | Operation | Before C1 | After C1 | Fused C1 | Before C8 | After C8 | Fused C8 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| u16 | Point → rank → payload | 26.91 | 26.93 | 21.59 | 8.28 | 8.68 | 21.78 |
| u16 | Range → window → payload | 32.25 | 32.12 | 36.29 | 17.78 | 17.14 | 38.60 |
| u16 | Rank → bounds → payload | 6.09 | 6.09 | 5.08 | 1.06 | 1.12 | 0.84 |
| u64 | Point → rank → payload | 29.12 | 30.86 | 62.97 | 15.26 | 12.22 | 60.83 |
| u64 | Range → window → payload | 36.12 | 35.18 | 118.84 | 27.58 | 21.91 | 117.91 |
| u64 | Rank → bounds → payload | 6.09 | 6.09 | 5.02 | 1.09 | 1.19 | 0.90 |

## neoverse-v2 / neon / geometry API / stream

| Source / encoding | Operation | Before C1 | After C1 | Fused C1 | Before C8 | After C8 | Fused C8 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Absolute Ends | Raw point → value | 30.30 | 31.97 | — | 12.50 | 12.67 | — |
| Absolute Ends | Selected point → value | 30.34 | 32.19 | — | 13.39 | 13.39 | — |
| Absolute Ends | Selected range → value | 42.72 | 47.04 | — | 38.38 | 41.13 | — |
| Sizes | Raw point → value | 35.53 | 35.40 | — | 13.99 | 13.88 | — |
| Sizes | Selected point → value | 36.02 | 35.44 | — | 14.35 | 14.31 | — |
| Sizes | Selected range → value | 48.98 | 49.19 | — | 39.92 | 40.16 | — |
| Relative Ends | Raw point → value | 32.49 | 32.07 | — | 16.35 | 16.68 | — |
| Relative Ends | Selected point → value | 32.75 | 32.52 | — | 17.01 | 17.31 | — |
| Relative Ends | Selected range → value | 48.18 | 48.40 | — | 45.17 | 45.07 | — |

## zen5 / avx2 / existing values / stream

| Source / encoding | Operation | Before C1 | After C1 | Fused C1 | Before C8 | After C8 | Fused C8 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Absolute Ends | Ordinary reverse → value | 23.40 | 23.39 | — | 15.16 | 15.58 | — |
| Absolute Ends | Typed reverse → value | 20.55 | 20.54 | 11.40 | 14.01 | 13.93 | 6.67 |
| Absolute Ends | Value by rank | 3.88 | 3.87 | 3.88 | 1.65 | 1.65 | 1.62 |
| Sizes | Ordinary reverse → value | 26.70 | 26.60 | — | 13.52 | 13.37 | — |
| Sizes | Typed reverse → value | 24.81 | 24.82 | 23.38 | 7.63 | 7.59 | 7.33 |
| Sizes | Value by rank | 9.70 | 9.69 | 9.71 | 2.34 | 2.53 | 2.32 |
| Relative Ends | Ordinary reverse → value | 18.93 | 18.87 | — | 12.05 | 12.00 | — |
| Relative Ends | Typed reverse → value | 16.73 | 16.52 | 14.25 | 7.56 | 7.55 | 6.20 |
| Relative Ends | Value by rank | 4.81 | 4.81 | 4.28 | 2.36 | 2.47 | 2.27 |

## zen5 / avx2 / geometry API / geometry

| Source / encoding | Operation | Before C1 | After C1 | Fused C1 | Before C8 | After C8 | Fused C8 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| u16 | Point → rank → payload | 16.88 | 16.12 | 8.79 | 5.95 | 5.86 | 3.72 |
| u16 | Range → window → payload | 17.67 | 18.10 | 10.21 | 12.01 | 11.64 | 7.31 |
| u16 | Rank → bounds → payload | 3.56 | 3.55 | 3.33 | 0.57 | 0.80 | 0.48 |
| u64 | Point → rank → payload | 21.33 | 19.74 | 15.88 | 8.25 | 7.66 | 16.74 |
| u64 | Range → window → payload | 24.18 | 24.06 | 29.50 | 17.55 | 17.35 | 32.14 |
| u64 | Rank → bounds → payload | 3.57 | 3.55 | 3.34 | 0.56 | 0.76 | 0.49 |

## zen5 / avx2 / geometry API / stream

| Source / encoding | Operation | Before C1 | After C1 | Fused C1 | Before C8 | After C8 | Fused C8 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Absolute Ends | Raw point → value | 19.94 | 19.54 | — | 9.81 | 9.79 | — |
| Absolute Ends | Selected point → value | 19.69 | 19.82 | — | 10.25 | 10.15 | — |
| Absolute Ends | Selected range → value | 26.11 | 26.41 | — | 23.73 | 24.24 | — |
| Sizes | Raw point → value | 25.07 | 24.92 | — | 7.62 | 7.70 | — |
| Sizes | Selected point → value | 25.13 | 24.97 | — | 7.71 | 7.82 | — |
| Sizes | Selected range → value | 30.35 | 30.48 | — | 22.92 | 23.23 | — |
| Relative Ends | Raw point → value | 16.67 | 16.89 | — | 7.52 | 7.39 | — |
| Relative Ends | Selected point → value | 16.70 | 16.87 | — | 7.89 | 8.10 | — |
| Relative Ends | Selected range → value | 24.85 | 25.03 | — | 23.28 | 23.69 | — |

## zen5 / avx512 / existing values / stream

| Source / encoding | Operation | Before C1 | After C1 | Fused C1 | Before C8 | After C8 | Fused C8 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Absolute Ends | Ordinary reverse → value | 21.83 | 21.62 | — | 14.40 | 14.40 | — |
| Absolute Ends | Typed reverse → value | 20.06 | 20.08 | 11.50 | 13.24 | 13.12 | 5.92 |
| Absolute Ends | Value by rank | 3.90 | 3.88 | 3.88 | 1.65 | 1.65 | 1.63 |
| Sizes | Ordinary reverse → value | 25.50 | 25.05 | — | 13.14 | 13.17 | — |
| Sizes | Typed reverse → value | 23.64 | 23.63 | 23.92 | 7.33 | 7.35 | 7.54 |
| Sizes | Value by rank | 9.72 | 9.72 | 9.70 | 2.31 | 2.34 | 2.34 |
| Relative Ends | Ordinary reverse → value | 16.96 | 17.01 | — | 10.66 | 10.63 | — |
| Relative Ends | Typed reverse → value | 16.77 | 16.86 | 14.10 | 10.45 | 10.41 | 6.87 |
| Relative Ends | Value by rank | 4.81 | 4.79 | 4.27 | 2.50 | 2.25 | 2.24 |

## zen5 / avx512 / geometry API / geometry

| Source / encoding | Operation | Before C1 | After C1 | Fused C1 | Before C8 | After C8 | Fused C8 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| u16 | Point → rank → payload | 14.05 | 14.52 | 12.58 | 4.80 | 4.87 | 2.99 |
| u16 | Range → window → payload | 15.17 | 15.46 | 9.51 | 9.71 | 9.98 | 5.63 |
| u16 | Rank → bounds → payload | 3.55 | 3.56 | 3.34 | 0.56 | 0.81 | 0.48 |
| u64 | Point → rank → payload | 18.07 | 18.94 | 12.40 | 7.15 | 6.86 | 8.14 |
| u64 | Range → window → payload | 20.13 | 20.82 | 19.56 | 14.47 | 14.41 | 22.35 |
| u64 | Rank → bounds → payload | 3.56 | 3.57 | 3.34 | 0.56 | 0.76 | 0.49 |

## zen5 / avx512 / geometry API / stream

| Source / encoding | Operation | Before C1 | After C1 | Fused C1 | Before C8 | After C8 | Fused C8 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Absolute Ends | Raw point → value | 19.64 | 19.34 | — | 9.06 | 8.86 | — |
| Absolute Ends | Selected point → value | 19.59 | 19.51 | — | 9.55 | 9.29 | — |
| Absolute Ends | Selected range → value | 24.75 | 25.21 | — | 22.82 | 22.96 | — |
| Sizes | Raw point → value | 23.83 | 23.78 | — | 7.39 | 7.53 | — |
| Sizes | Selected point → value | 23.88 | 23.71 | — | 7.49 | 7.47 | — |
| Sizes | Selected range → value | 28.31 | 28.45 | — | 21.31 | 21.34 | — |
| Relative Ends | Raw point → value | 15.12 | 15.30 | — | 6.99 | 6.75 | — |
| Relative Ends | Selected point → value | 15.39 | 15.11 | — | 7.12 | 7.26 | — |
| Relative Ends | Selected range → value | 22.74 | 22.83 | — | 21.21 | 21.02 | — |
