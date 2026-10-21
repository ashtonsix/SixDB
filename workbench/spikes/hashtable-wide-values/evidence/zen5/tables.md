# Native Zen 5 scorecard

All timings are ns/operation (lower is better), medians over all retained
repetitions and three input seeds. Read rows consume every payload byte
unless marked first16. Memory is requested backing bytes/live entry.
P = predictable, U = unpredictable, Auto = caller-owned automatic selection.
PF32 = table-home prefetch 32 queries ahead. Manual16 and Loom16 delay
payload consumption after prefetching its complete byte window. Peer
controls use P and their native lookup. Loom timings include admission,
yield, drain and reduction, but exclude worker/bank setup and teardown.

## Proper ID-before-payload staging W32 / b17 / V32 / 2,673,868 entries

| Implementation / consumer | Hit | Random50 | Miss | Alternating50 | Runs64 |
| --- | --- | --- | --- | --- | --- |
| Ikea getter Direct PF0 | 28.50 | 26.10 | 14.81 | 25.80 | 22.09 |
| Ikea getter Direct PF32 | 23.77 | 22.15 | 6.71 | 20.72 | 15.47 |
| Ikea getter Payload-only Manual16 | 30.41 | 29.07 | 7.85 | 28.04 | 19.18 |
| Ikea getter Payload-only Loom16 | 36.16 | 34.33 | 8.72 | 31.59 | 22.87 |
| Borrowed inline Direct PF0 | 30.20 | 27.67 | 15.61 | 25.84 | 23.36 |
| Borrowed inline Direct PF32 | 24.35 | 22.95 | 6.94 | 21.58 | 15.66 |
| Borrowed inline Payload-only Manual16 | 12.79 | 12.43 | 7.74 | 11.39 | 10.80 |
| Borrowed inline Payload-only Loom16 | 14.43 | 15.20 | 8.79 | 12.49 | 11.75 |
| Ikea ID8 Direct PF0 | 41.07 | 34.79 | 13.27 | 35.53 | 28.54 |
| Ikea ID8 Direct PF32 | 39.31 | 35.44 | 6.33 | 37.14 | 23.45 |
| Ikea ID8 Payload-only Manual16 | 31.09 | 29.60 | 7.44 | 29.44 | 20.53 |
| Ikea ID8 Payload-only Loom16 | 37.37 | 35.39 | 8.47 | 33.92 | 23.64 |
| Ikea ID8 ID + payload Manual16 | 22.50 | 22.58 | 8.25 | 20.45 | 15.86 |
| Ikea ID8 ID + payload Loom16 | 24.16 | 24.62 | 10.20 | 21.54 | 17.32 |
| Ikea ID4 Direct PF0 | 40.57 | 39.80 | 16.75 | 39.07 | 31.28 |
| Ikea ID4 Direct PF32 | 47.58 | 42.37 | 15.22 | 42.15 | 33.42 |
| Ikea ID4 Payload-only Manual16 | 40.84 | 37.22 | 19.26 | 34.90 | 31.31 |
| Ikea ID4 Payload-only Loom16 | 48.67 | 41.94 | 20.46 | 37.98 | 35.39 |
| Ikea ID4 ID + payload Manual16 | 29.76 | 30.68 | 20.18 | 27.27 | 25.86 |
| Ikea ID4 ID + payload Loom16 | 32.86 | 34.16 | 21.87 | 29.30 | 27.26 |
| Abseil Direct PF0 | 26.08 | 21.71 | 12.03 | 19.35 | 19.05 |
| Abseil Direct PF32 | 28.15 | 25.15 | 12.51 | 21.71 | 20.85 |
| Abseil Payload-only Manual16 | 26.29 | 25.33 | 17.27 | 22.95 | 22.46 |
| Abseil Payload-only Loom16 | 30.17 | 30.52 | 17.83 | 24.27 | 24.61 |
| Boost Direct PF0 | 31.78 | 25.49 | 11.01 | 23.52 | 20.97 |
| Boost Direct PF32 | 31.54 | 25.67 | 11.03 | 24.10 | 21.95 |
| Boost Payload-only Manual16 | 37.97 | 32.60 | 14.53 | 28.80 | 26.16 |
| Boost Payload-only Loom16 | 42.20 | 40.91 | 16.02 | 35.15 | 30.09 |
| F14Fast Direct PF0 | 38.95 | 30.32 | 14.62 | 29.31 | 26.78 |
| F14Fast Direct PF32 | 28.12 | 23.23 | 6.27 | 22.90 | 18.17 |
| F14Fast Payload-only Manual16 | 36.96 | 29.67 | 7.24 | 29.21 | 22.26 |
| F14Fast Payload-only Loom16 | 43.92 | 38.01 | 8.16 | 41.86 | 27.89 |
| F14Value Direct PF0 | 32.61 | 28.26 | 17.82 | 26.83 | 25.03 |
| F14Value Direct PF32 | 26.87 | 22.53 | 11.38 | 20.80 | 19.35 |
| F14Value Payload-only Manual16 | 32.45 | 29.15 | 14.71 | 27.88 | 24.37 |
| F14Value Payload-only Loom16 | 40.77 | 37.01 | 17.31 | 35.09 | 30.11 |
| Abseil ID8 Direct PF0 | 43.78 | 32.28 | 10.83 | 30.42 | 27.11 |
| Abseil ID8 Direct PF32 | 48.15 | 36.57 | 11.49 | 37.90 | 30.16 |
| Abseil ID8 Payload-only Manual16 | 32.30 | 27.13 | 15.90 | 24.47 | 24.45 |
| Abseil ID8 Payload-only Loom16 | 34.13 | 30.87 | 15.84 | 26.08 | 25.62 |
| Abseil ID8 ID + payload Manual16 | 27.90 | 30.14 | 16.63 | 25.16 | 22.64 |
| Abseil ID8 ID + payload Loom16 | 28.98 | 31.27 | 17.65 | 25.60 | 23.86 |
| Boost ID8 Direct PF0 | 51.66 | 38.44 | 10.98 | 35.94 | 31.81 |
| Boost ID8 Direct PF32 | 52.08 | 39.32 | 10.84 | 35.27 | 31.88 |
| Boost ID8 Payload-only Manual16 | 39.26 | 34.71 | 14.05 | 32.43 | 28.85 |
| Boost ID8 Payload-only Loom16 | 50.55 | 43.35 | 18.27 | 35.77 | 32.43 |
| Boost ID8 ID + payload Manual16 | 43.43 | 38.90 | 15.99 | 34.35 | 28.34 |
| Boost ID8 ID + payload Loom16 | 47.47 | 45.11 | 18.59 | 38.83 | 33.52 |
| F14Fast ID8 Direct PF0 | 49.38 | 38.80 | 17.40 | 38.14 | 35.36 |
| F14Fast ID8 Direct PF32 | 48.26 | 37.20 | 11.66 | 36.89 | 30.58 |
| F14Fast ID8 Payload-only Manual16 | 34.44 | 30.31 | 14.33 | 28.00 | 24.92 |
| F14Fast ID8 Payload-only Loom16 | 41.06 | 36.91 | 16.00 | 36.91 | 29.40 |
| F14Fast ID8 ID + payload Manual16 | 33.18 | 33.22 | 15.75 | 29.53 | 26.11 |
| F14Fast ID8 ID + payload Loom16 | 40.12 | 38.31 | 18.32 | 35.13 | 30.54 |

## Proper ID-before-payload staging W32 / b17 / V64 / 2,673,868 entries

| Implementation / consumer | Hit | Random50 | Miss | Alternating50 | Runs64 |
| --- | --- | --- | --- | --- | --- |
| Ikea getter Direct PF0 | 29.46 | 26.99 | 15.12 | 26.18 | 22.79 |
| Ikea getter Direct PF32 | 24.22 | 22.07 | 6.79 | 21.59 | 15.80 |
| Ikea getter Payload-only Manual16 | 32.23 | 29.06 | 7.86 | 28.99 | 20.13 |
| Ikea getter Payload-only Loom16 | 43.21 | 37.90 | 9.25 | 33.51 | 27.17 |
| Borrowed inline Direct PF0 | 31.78 | 28.57 | 15.95 | 27.00 | 24.09 |
| Borrowed inline Direct PF32 | 25.16 | 22.75 | 6.79 | 22.43 | 16.89 |
| Borrowed inline Payload-only Manual16 | 13.42 | 12.78 | 7.95 | 11.99 | 11.01 |
| Borrowed inline Payload-only Loom16 | 15.18 | 15.88 | 9.70 | 13.32 | 12.50 |
| Ikea ID8 Direct PF0 | 42.38 | 36.10 | 13.09 | 35.95 | 28.74 |
| Ikea ID8 Direct PF32 | 39.75 | 35.16 | 6.29 | 36.79 | 24.00 |
| Ikea ID8 Payload-only Manual16 | 31.72 | 29.44 | 7.50 | 29.53 | 21.43 |
| Ikea ID8 Payload-only Loom16 | 42.80 | 36.94 | 8.54 | 34.46 | 27.78 |
| Ikea ID8 ID + payload Manual16 | 21.95 | 22.72 | 8.18 | 20.41 | 15.82 |
| Ikea ID8 ID + payload Loom16 | 24.38 | 25.66 | 10.32 | 22.35 | 18.07 |
| Ikea ID4 Direct PF0 | 42.69 | 40.69 | 16.68 | 40.43 | 31.80 |
| Ikea ID4 Direct PF32 | 49.95 | 43.00 | 15.30 | 41.99 | 32.62 |
| Ikea ID4 Payload-only Manual16 | 41.51 | 36.40 | 18.09 | 34.96 | 31.45 |
| Ikea ID4 Payload-only Loom16 | 49.12 | 43.17 | 22.05 | 41.53 | 36.51 |
| Ikea ID4 ID + payload Manual16 | 29.73 | 31.17 | 20.39 | 27.54 | 25.39 |
| Ikea ID4 ID + payload Loom16 | 33.37 | 34.38 | 22.68 | 30.95 | 28.99 |
| Abseil Direct PF0 | 28.28 | 24.56 | 12.41 | 24.11 | 20.85 |
| Abseil Direct PF32 | 32.59 | 29.17 | 13.46 | 28.03 | 23.60 |
| Abseil Payload-only Manual16 | 35.34 | 27.98 | 18.07 | 26.58 | 26.11 |
| Abseil Payload-only Loom16 | 36.17 | 34.45 | 20.26 | 30.08 | 28.60 |
| Boost Direct PF0 | 34.32 | 26.99 | 11.16 | 25.87 | 23.44 |
| Boost Direct PF32 | 34.20 | 27.76 | 11.62 | 25.70 | 23.50 |
| Boost Payload-only Manual16 | 40.80 | 33.79 | 15.55 | 32.26 | 28.95 |
| Boost Payload-only Loom16 | 56.54 | 44.12 | 19.74 | 37.99 | 36.19 |
| F14Fast Direct PF0 | 45.14 | 33.07 | 15.69 | 33.39 | 30.48 |
| F14Fast Direct PF32 | 31.92 | 24.01 | 6.58 | 21.83 | 19.62 |
| F14Fast Payload-only Manual16 | 39.02 | 31.89 | 7.59 | 30.90 | 23.60 |
| F14Fast Payload-only Loom16 | 47.70 | 41.08 | 8.53 | 43.67 | 28.70 |
| F14Value Direct PF0 | 36.67 | 30.80 | 18.30 | 28.72 | 27.40 |
| F14Value Direct PF32 | 30.02 | 24.03 | 12.29 | 23.55 | 21.44 |
| F14Value Payload-only Manual16 | 35.42 | 32.02 | 16.22 | 30.08 | 26.52 |
| F14Value Payload-only Loom16 | 43.07 | 39.19 | 19.27 | 40.15 | 31.41 |
| Abseil ID8 Direct PF0 | 44.33 | 31.87 | 11.06 | 31.37 | 28.26 |
| Abseil ID8 Direct PF32 | 50.12 | 38.01 | 11.80 | 37.88 | 31.02 |
| Abseil ID8 Payload-only Manual16 | 31.52 | 27.21 | 15.57 | 24.93 | 22.90 |
| Abseil ID8 Payload-only Loom16 | 33.01 | 31.12 | 16.45 | 27.23 | 26.13 |
| Abseil ID8 ID + payload Manual16 | 29.42 | 30.32 | 16.75 | 23.94 | 22.96 |
| Abseil ID8 ID + payload Loom16 | 30.40 | 32.66 | 18.23 | 26.11 | 25.11 |
| Boost ID8 Direct PF0 | 52.18 | 39.33 | 10.83 | 36.68 | 31.53 |
| Boost ID8 Direct PF32 | 50.56 | 39.35 | 11.10 | 35.67 | 32.16 |
| Boost ID8 Payload-only Manual16 | 40.58 | 34.76 | 14.49 | 34.38 | 28.57 |
| Boost ID8 Payload-only Loom16 | 54.32 | 45.59 | 20.26 | 38.54 | 39.27 |
| Boost ID8 ID + payload Manual16 | 42.33 | 39.01 | 16.07 | 34.20 | 30.27 |
| Boost ID8 ID + payload Loom16 | 51.29 | 46.91 | 19.53 | 40.63 | 37.91 |
| F14Fast ID8 Direct PF0 | 50.62 | 39.11 | 17.86 | 39.23 | 35.63 |
| F14Fast ID8 Direct PF32 | 45.56 | 37.15 | 11.14 | 37.22 | 28.71 |
| F14Fast ID8 Payload-only Manual16 | 34.45 | 31.02 | 14.42 | 28.65 | 26.03 |
| F14Fast ID8 Payload-only Loom16 | 40.59 | 38.23 | 16.93 | 36.83 | 30.70 |
| F14Fast ID8 ID + payload Manual16 | 33.88 | 32.99 | 16.14 | 29.73 | 26.34 |
| F14Fast ID8 ID + payload Loom16 | 40.45 | 39.16 | 18.24 | 35.15 | 30.91 |

## Proper ID-before-payload staging W32 / b16 / V128 / 1,336,934 entries

| Implementation / consumer | Hit | Random50 | Miss | Alternating50 | Runs64 |
| --- | --- | --- | --- | --- | --- |
| Ikea getter Direct PF0 | 28.21 | 25.12 | 12.27 | 23.55 | 20.06 |
| Ikea getter Direct PF32 | 24.43 | 23.85 | 6.16 | 22.73 | 15.91 |
| Ikea getter Payload-only Manual16 | 34.01 | 31.73 | 7.80 | 31.30 | 21.33 |
| Ikea getter Payload-only Loom16 | 48.26 | 44.14 | 9.93 | 44.76 | 29.86 |
| Borrowed inline Direct PF0 | 30.01 | 26.58 | 12.93 | 25.39 | 21.46 |
| Borrowed inline Direct PF32 | 26.49 | 23.86 | 6.08 | 22.37 | 16.76 |
| Borrowed inline Payload-only Manual16 | 13.79 | 13.93 | 7.60 | 12.20 | 10.54 |
| Borrowed inline Payload-only Loom16 | 15.52 | 16.53 | 9.46 | 14.13 | 13.18 |
| Ikea ID8 Direct PF0 | 37.67 | 31.93 | 9.98 | 31.59 | 24.24 |
| Ikea ID8 Direct PF32 | 38.73 | 34.32 | 5.26 | 33.97 | 23.19 |
| Ikea ID8 Payload-only Manual16 | 30.67 | 27.42 | 6.65 | 27.24 | 20.07 |
| Ikea ID8 Payload-only Loom16 | 41.74 | 37.97 | 8.41 | 39.74 | 26.28 |
| Ikea ID8 ID + payload Manual16 | 21.47 | 22.56 | 7.41 | 20.18 | 15.03 |
| Ikea ID8 ID + payload Loom16 | 23.47 | 25.47 | 10.07 | 22.78 | 17.87 |
| Ikea ID4 Direct PF0 | 37.01 | 34.36 | 12.88 | 33.70 | 27.03 |
| Ikea ID4 Direct PF32 | 44.67 | 38.82 | 11.30 | 38.43 | 29.86 |
| Ikea ID4 Payload-only Manual16 | 32.74 | 30.47 | 13.72 | 28.52 | 26.13 |
| Ikea ID4 Payload-only Loom16 | 41.56 | 37.80 | 15.73 | 34.62 | 29.28 |
| Ikea ID4 ID + payload Manual16 | 26.66 | 27.68 | 14.83 | 23.94 | 21.89 |
| Ikea ID4 ID + payload Loom16 | 31.32 | 30.76 | 17.28 | 27.42 | 23.98 |
| Abseil Direct PF0 | 26.89 | 23.71 | 10.54 | 22.25 | 19.42 |
| Abseil Direct PF32 | 34.31 | 29.03 | 11.42 | 30.94 | 24.09 |
| Abseil Payload-only Manual16 | 30.11 | 27.24 | 15.99 | 24.63 | 23.16 |
| Abseil Payload-only Loom16 | 34.21 | 32.68 | 18.31 | 28.26 | 27.72 |
| Boost Direct PF0 | 32.01 | 25.32 | 8.51 | 24.11 | 21.79 |
| Boost Direct PF32 | 31.31 | 25.24 | 9.13 | 23.94 | 21.25 |
| Boost Payload-only Manual16 | 35.91 | 30.52 | 11.49 | 28.99 | 24.32 |
| Boost Payload-only Loom16 | 51.28 | 45.09 | 14.31 | 47.05 | 33.96 |
| F14Fast Direct PF0 | 43.73 | 31.77 | 14.73 | 33.72 | 29.39 |
| F14Fast Direct PF32 | 30.50 | 24.28 | 6.10 | 24.20 | 19.77 |
| F14Fast Payload-only Manual16 | 38.44 | 30.25 | 7.04 | 30.21 | 22.94 |
| F14Fast Payload-only Loom16 | 57.97 | 41.89 | 8.01 | 45.14 | 33.75 |
| F14Value Direct PF0 | 34.37 | 28.05 | 15.29 | 27.12 | 25.54 |
| F14Value Direct PF32 | 30.76 | 25.48 | 11.37 | 25.21 | 22.29 |
| F14Value Payload-only Manual16 | 34.75 | 30.25 | 14.06 | 28.62 | 24.96 |
| F14Value Payload-only Loom16 | 50.75 | 41.19 | 17.32 | 39.86 | 35.92 |
| Abseil ID8 Direct PF0 | 50.03 | 35.40 | 9.39 | 34.28 | 30.45 |
| Abseil ID8 Direct PF32 | 50.53 | 37.03 | 9.49 | 39.45 | 30.32 |
| Abseil ID8 Payload-only Manual16 | 29.18 | 24.14 | 12.52 | 23.03 | 21.77 |
| Abseil ID8 Payload-only Loom16 | 33.33 | 30.48 | 14.65 | 25.45 | 23.80 |
| Abseil ID8 ID + payload Manual16 | 27.53 | 29.26 | 13.86 | 23.75 | 21.05 |
| Abseil ID8 ID + payload Loom16 | 28.72 | 31.94 | 15.81 | 26.00 | 23.54 |
| Boost ID8 Direct PF0 | 49.16 | 37.42 | 8.32 | 35.71 | 30.49 |
| Boost ID8 Direct PF32 | 49.79 | 37.57 | 8.20 | 35.14 | 30.84 |
| Boost ID8 Payload-only Manual16 | 35.47 | 30.02 | 10.76 | 30.35 | 24.89 |
| Boost ID8 Payload-only Loom16 | 46.05 | 44.02 | 14.16 | 43.03 | 32.16 |
| Boost ID8 ID + payload Manual16 | 36.58 | 34.32 | 11.90 | 30.52 | 25.81 |
| Boost ID8 ID + payload Loom16 | 46.89 | 43.14 | 16.12 | 40.96 | 32.81 |
| F14Fast ID8 Direct PF0 | 48.75 | 35.86 | 14.19 | 36.17 | 33.01 |
| F14Fast ID8 Direct PF32 | 51.20 | 38.69 | 9.95 | 38.10 | 31.05 |
| F14Fast ID8 Payload-only Manual16 | 30.68 | 28.30 | 11.63 | 25.74 | 22.99 |
| F14Fast ID8 Payload-only Loom16 | 44.71 | 37.68 | 13.61 | 34.60 | 31.78 |
| F14Fast ID8 ID + payload Manual16 | 33.95 | 31.84 | 12.96 | 28.29 | 25.43 |
| F14Fast ID8 ID + payload Loom16 | 41.85 | 36.28 | 15.45 | 33.09 | 30.29 |

## Proper ID-before-payload staging W40 / b17 / V64 / 1,782,579 entries

| Implementation / consumer | Hit | Random50 | Miss | Alternating50 | Runs64 |
| --- | --- | --- | --- | --- | --- |
| Ikea getter Direct PF0 | 32.54 | 29.89 | 16.93 | 28.02 | 24.97 |
| Ikea getter Direct PF32 | 25.82 | 23.60 | 7.11 | 22.48 | 17.25 |
| Ikea getter Payload-only Manual16 | 33.31 | 32.04 | 8.29 | 30.06 | 21.46 |
| Ikea getter Payload-only Loom16 | 43.43 | 39.43 | 9.61 | 40.61 | 27.84 |
| Borrowed inline Direct PF0 | 29.27 | 26.54 | 15.29 | 24.70 | 22.58 |
| Borrowed inline Direct PF32 | 25.26 | 23.19 | 6.81 | 22.11 | 16.46 |
| Borrowed inline Payload-only Manual16 | 14.59 | 15.89 | 8.01 | 12.48 | 11.49 |
| Borrowed inline Payload-only Loom16 | 16.26 | 17.57 | 9.31 | 13.96 | 13.07 |
| Ikea ID8 Direct PF0 | 43.17 | 38.94 | 15.59 | 40.02 | 30.71 |
| Ikea ID8 Direct PF32 | 46.00 | 41.17 | 6.96 | 42.11 | 28.00 |
| Ikea ID8 Payload-only Manual16 | 34.79 | 32.00 | 7.87 | 30.35 | 22.50 |
| Ikea ID8 Payload-only Loom16 | 41.62 | 37.72 | 9.02 | 38.58 | 27.05 |
| Ikea ID8 ID + payload Manual16 | 21.91 | 23.27 | 8.06 | 21.10 | 16.30 |
| Ikea ID8 ID + payload Loom16 | 24.44 | 25.59 | 10.41 | 22.98 | 18.14 |
| Ikea ID4 Direct PF0 | 35.99 | 32.38 | 14.16 | 30.99 | 26.46 |
| Ikea ID4 Direct PF32 | 40.18 | 34.05 | 6.39 | 33.93 | 25.35 |
| Ikea ID4 Payload-only Manual16 | 28.08 | 27.29 | 7.25 | 26.08 | 19.75 |
| Ikea ID4 Payload-only Loom16 | 35.13 | 32.99 | 8.58 | 31.53 | 24.80 |
| Ikea ID4 ID + payload Manual16 | 21.30 | 22.40 | 7.95 | 20.30 | 15.19 |
| Ikea ID4 ID + payload Loom16 | 23.38 | 24.54 | 10.08 | 21.68 | 17.51 |
| Abseil Direct PF0 | 24.97 | 25.26 | 16.52 | 21.73 | 20.56 |
| Abseil Direct PF32 | 31.24 | 31.45 | 19.42 | 28.64 | 26.48 |
| Abseil Payload-only Manual16 | 33.48 | 37.46 | 25.40 | 31.56 | 31.12 |
| Abseil Payload-only Loom16 | 40.43 | 41.63 | 28.29 | 35.34 | 34.95 |
| Boost Direct PF0 | 33.50 | 26.17 | 10.17 | 24.93 | 22.48 |
| Boost Direct PF32 | 33.37 | 26.31 | 10.51 | 24.59 | 22.27 |
| Boost Payload-only Manual16 | 38.35 | 31.74 | 12.50 | 27.27 | 25.34 |
| Boost Payload-only Loom16 | 48.35 | 41.22 | 17.24 | 35.47 | 32.30 |
| F14Fast Direct PF0 | 39.30 | 30.59 | 14.87 | 29.65 | 27.80 |
| F14Fast Direct PF32 | 32.53 | 24.79 | 7.53 | 25.64 | 21.25 |
| F14Fast Payload-only Manual16 | 38.64 | 34.60 | 9.05 | 32.06 | 25.38 |
| F14Fast Payload-only Loom16 | 48.44 | 40.31 | 10.39 | 43.22 | 30.62 |
| F14Value Direct PF0 | 34.30 | 27.66 | 13.23 | 28.17 | 23.84 |
| F14Value Direct PF32 | 23.91 | 19.44 | 8.04 | 19.12 | 16.62 |
| F14Value Payload-only Manual16 | 32.11 | 26.86 | 9.95 | 25.82 | 20.52 |
| F14Value Payload-only Loom16 | 38.57 | 35.32 | 11.04 | 36.41 | 25.49 |
| Abseil ID8 Direct PF0 | 41.92 | 32.84 | 15.17 | 33.31 | 29.54 |
| Abseil ID8 Direct PF32 | 45.95 | 40.29 | 17.58 | 41.72 | 33.72 |
| Abseil ID8 Payload-only Manual16 | 35.62 | 35.36 | 22.64 | 29.88 | 28.54 |
| Abseil ID8 Payload-only Loom16 | 40.58 | 39.09 | 23.94 | 33.90 | 32.71 |
| Abseil ID8 ID + payload Manual16 | 33.27 | 36.84 | 24.49 | 29.30 | 28.97 |
| Abseil ID8 ID + payload Loom16 | 34.64 | 37.58 | 25.92 | 31.49 | 30.14 |
| Boost ID8 Direct PF0 | 48.42 | 38.11 | 10.65 | 35.84 | 30.92 |
| Boost ID8 Direct PF32 | 50.23 | 37.36 | 9.91 | 35.11 | 31.04 |
| Boost ID8 Payload-only Manual16 | 37.81 | 34.00 | 13.49 | 27.95 | 26.16 |
| Boost ID8 Payload-only Loom16 | 52.38 | 45.81 | 18.74 | 37.61 | 37.14 |
| Boost ID8 ID + payload Manual16 | 42.02 | 37.94 | 14.81 | 34.05 | 28.76 |
| Boost ID8 ID + payload Loom16 | 50.70 | 44.33 | 17.36 | 38.43 | 35.87 |
| F14Fast ID8 Direct PF0 | 43.59 | 35.14 | 13.05 | 35.07 | 29.83 |
| F14Fast ID8 Direct PF32 | 42.89 | 32.09 | 5.92 | 30.78 | 24.63 |
| F14Fast ID8 Payload-only Manual16 | 29.84 | 26.37 | 6.68 | 24.16 | 18.94 |
| F14Fast ID8 Payload-only Loom16 | 37.59 | 32.54 | 7.87 | 32.36 | 23.44 |
| F14Fast ID8 ID + payload Manual16 | 33.70 | 30.31 | 8.04 | 27.52 | 21.03 |
| F14Fast ID8 ID + payload Loom16 | 37.71 | 34.98 | 8.85 | 33.04 | 24.92 |

## Proper ID-before-payload staging W32 / b18 / V64 / 2,673,868 entries

| Implementation / consumer | Hit | Random50 | Miss | Alternating50 | Runs64 |
| --- | --- | --- | --- | --- | --- |
| Ikea ID8 Direct PF0 | 38.37 | 34.31 | 13.20 | 33.08 | 26.60 |
| Ikea ID8 Direct PF32 | 41.26 | 33.09 | 4.72 | 31.99 | 23.78 |
| Ikea ID8 Payload-only Manual16 | 33.87 | 28.01 | 5.78 | 29.93 | 20.97 |
| Ikea ID8 Payload-only Loom16 | 42.39 | 37.14 | 6.45 | 37.50 | 25.52 |
| Ikea ID8 ID + payload Manual16 | 20.13 | 20.45 | 6.05 | 19.45 | 13.84 |
| Ikea ID8 ID + payload Loom16 | 21.78 | 22.50 | 7.32 | 20.28 | 15.03 |
| Ikea ID4 Direct PF0 | 39.57 | 37.37 | 15.22 | 36.71 | 29.91 |
| Ikea ID4 Direct PF32 | 43.37 | 38.49 | 5.09 | 38.05 | 26.07 |
| Ikea ID4 Payload-only Manual16 | 33.61 | 30.08 | 5.60 | 27.67 | 20.79 |
| Ikea ID4 Payload-only Loom16 | 40.85 | 36.32 | 6.54 | 38.09 | 25.50 |
| Ikea ID4 ID + payload Manual16 | 20.61 | 20.83 | 6.10 | 19.51 | 13.59 |
| Ikea ID4 ID + payload Loom16 | 21.08 | 21.68 | 7.23 | 20.15 | 15.04 |

## Hot W32 / V8 / 6,963 entries, 85% primary occupancy

| Implementation | Hit | Random50 | Miss | Alternating50 | Runs64 |
| --- | --- | --- | --- | --- | --- |
| Ikea getter | 1.95 | 6.74 | 3.61 | 2.60 | 2.92 |
| Borrowed inline | 1.96 | 6.86 | 3.39 | 2.57 | 2.78 |
| Ikea ID8 | 2.29 | 7.01 | 3.42 | 2.78 | 2.98 |
| Ikea ID4 | 2.33 | 5.59 | 3.71 | 2.72 | 3.01 |
| Abseil | 2.23 | 11.26 | 9.76 | 6.08 | 6.43 |
| Boost | 1.84 | 5.95 | 1.13 | 1.58 | 1.58 |
| F14Fast | 1.96 | 6.07 | 1.33 | 1.60 | 1.70 |
| F14Value | 1.92 | 5.96 | 1.33 | 1.59 | 1.71 |
| Abseil ID8 | 2.49 | 11.34 | 9.91 | 5.84 | 6.57 |
| Boost ID8 | 2.46 | 6.42 | 1.23 | 1.92 | 2.03 |

## Hot W32 / V16 / 6,963 entries, 85% primary occupancy

| Implementation | Hit | Random50 | Miss | Alternating50 | Runs64 |
| --- | --- | --- | --- | --- | --- |
| Ikea getter | 2.44 | 9.17 | 3.62 | 2.88 | 3.25 |
| Borrowed inline | 2.23 | 9.01 | 3.35 | 2.64 | 2.94 |
| Ikea ID8 | 2.54 | 7.40 | 3.51 | 2.93 | 3.16 |
| Ikea ID4 | 2.42 | 6.00 | 3.64 | 2.77 | 3.04 |
| Abseil | 2.31 | 11.12 | 9.78 | 5.89 | 6.47 |
| Boost | 2.09 | 6.35 | 1.16 | 1.69 | 1.76 |
| F14Fast | 2.03 | 6.50 | 1.35 | 1.70 | 1.79 |
| F14Value | 2.03 | 6.51 | 1.34 | 1.72 | 1.83 |
| Abseil ID8 | 2.68 | 11.31 | 9.89 | 5.85 | 6.60 |
| Boost ID8 | 2.62 | 6.59 | 1.21 | 2.06 | 2.15 |

## Hot W32 / V32 / 6,963 entries, 85% primary occupancy

| Implementation | Hit | Random50 | Miss | Alternating50 | Runs64 |
| --- | --- | --- | --- | --- | --- |
| Ikea getter | 2.70 | 9.22 | 3.67 | 2.98 | 3.24 |
| Borrowed inline | 2.35 | 5.77 | 3.48 | 2.79 | 3.06 |
| Ikea ID8 | 2.79 | 7.24 | 3.48 | 3.05 | 3.22 |
| Ikea ID4 | 2.66 | 5.86 | 3.43 | 2.91 | 3.10 |
| Abseil | 2.41 | 11.49 | 9.76 | 5.92 | 6.40 |
| Boost | 2.27 | 6.42 | 1.15 | 1.81 | 1.88 |
| F14Fast | 2.36 | 6.22 | 1.41 | 2.01 | 2.03 |
| F14Value | 2.29 | 6.49 | 1.40 | 1.84 | 1.92 |
| Abseil ID8 | 2.72 | 11.62 | 10.08 | 6.07 | 6.72 |
| Boost ID8 | 2.84 | 6.56 | 1.22 | 2.24 | 2.18 |

## Hot W32 / V64 / 6,963 entries, 85% primary occupancy

| Implementation | Hit | Random50 | Miss | Alternating50 | Runs64 |
| --- | --- | --- | --- | --- | --- |
| Ikea getter | 2.83 | 9.59 | 3.63 | 3.10 | 3.42 |
| Borrowed inline | 2.61 | 6.14 | 3.61 | 2.89 | 3.22 |
| Ikea ID8 | 3.03 | 7.70 | 3.75 | 3.19 | 3.41 |
| Ikea ID4 | 2.94 | 6.11 | 3.39 | 3.12 | 3.29 |
| Abseil | 2.83 | 11.83 | 9.89 | 6.08 | 6.75 |
| Boost | 2.70 | 6.67 | 1.21 | 2.19 | 2.14 |
| F14Fast | 3.02 | 6.70 | 1.53 | 2.38 | 2.42 |
| F14Value | 2.65 | 6.75 | 1.45 | 2.16 | 2.20 |
| Abseil ID8 | 3.17 | 11.66 | 9.94 | 6.24 | 6.81 |
| Boost ID8 | 3.22 | 6.72 | 1.21 | 2.48 | 2.41 |

## Hot W32 / V128 / 6,963 entries, 85% primary occupancy

| Implementation | Hit | Random50 | Miss | Alternating50 | Runs64 |
| --- | --- | --- | --- | --- | --- |
| Ikea getter | 3.42 | 9.79 | 4.10 | 3.53 | 3.82 |
| Borrowed inline | 3.11 | 6.35 | 3.48 | 3.21 | 3.42 |
| Ikea ID8 | 3.62 | 7.84 | 3.51 | 3.56 | 3.70 |
| Ikea ID4 | 3.35 | 6.42 | 3.35 | 3.33 | 3.56 |
| Abseil | 3.57 | 11.77 | 10.02 | 6.22 | 7.03 |
| Boost | 3.48 | 6.76 | 1.21 | 2.69 | 2.59 |
| F14Fast | 3.79 | 6.74 | 1.54 | 2.89 | 2.78 |
| F14Value | 3.22 | 6.82 | 1.45 | 2.48 | 2.49 |
| Abseil ID8 | 4.23 | 11.48 | 9.67 | 6.40 | 7.15 |
| Boost ID8 | 3.98 | 6.72 | 1.21 | 3.05 | 2.82 |

## Small-value reference W32 / V8 / 13,926 entries, 42.5% primary occupancy

| Implementation | Hit | Random50 | Miss | Alternating50 | Runs64 |
| --- | --- | --- | --- | --- | --- |
| Ikea getter p | 1.41 | 7.36 | 1.12 | 1.28 | 1.29 |
| Ikea getter u | 2.31 | 2.33 | 2.24 | 2.32 | 2.32 |
| Ikea getter auto | 1.46 | 3.61 | 1.17 | 1.35 | 1.34 |
| Borrowed inline p | 1.41 | 7.57 | 1.12 | 1.30 | 1.29 |
| Borrowed inline u | 2.35 | 2.36 | 2.28 | 2.37 | 2.36 |
| Borrowed inline auto | 1.47 | 3.69 | 1.16 | 1.34 | 1.38 |
| Abseil p | 2.27 | 11.20 | 9.67 | 5.83 | 6.32 |
| Boost p | 1.90 | 6.02 | 1.16 | 1.65 | 1.72 |
| F14Fast p | 1.94 | 6.12 | 1.30 | 1.61 | 1.76 |

## Cold full-window W32 / V64 / 2,673,868 entries

| Implementation / consumer | Hit | Random50 | Miss | Alternating50 | Runs64 |
| --- | --- | --- | --- | --- | --- |
| Ikea getter Direct PF32 | 22.78 | 21.26 | 6.49 | 20.23 | 14.98 |
| Ikea getter Manual16 PF32 | 29.52 | 27.59 | 7.65 | 27.61 | 19.04 |
| Ikea getter Loom16 PF32 | 40.30 | 35.86 | 9.15 | 32.28 | 25.61 |
| Borrowed inline Direct PF32 | 24.19 | 21.78 | 6.51 | 21.02 | 15.74 |
| Borrowed inline Manual16 PF32 | 12.61 | 12.29 | 7.76 | 11.28 | 10.42 |
| Borrowed inline Loom16 PF32 | 13.90 | 15.02 | 9.19 | 12.33 | 11.79 |
| Ikea ID8 Direct PF32 | 36.85 | 33.71 | 6.01 | 35.12 | 22.81 |
| Ikea ID8 Manual16 PF32 | 30.68 | 28.00 | 7.21 | 28.39 | 19.91 |
| Ikea ID8 Loom16 PF32 | 40.53 | 35.34 | 8.41 | 32.54 | 26.25 |
| Ikea ID4 Direct PF32 | 45.62 | 39.89 | 14.19 | 39.33 | 31.41 |
| Ikea ID4 Manual16 PF32 | 39.02 | 34.93 | 17.22 | 32.84 | 29.16 |
| Ikea ID4 Loom16 PF32 | 44.98 | 40.43 | 20.37 | 37.98 | 34.46 |
| Abseil Direct PF32 | 30.68 | 27.58 | 12.52 | 27.17 | 22.26 |
| Abseil Manual16 PF32 | 32.63 | 27.28 | 17.33 | 25.14 | 25.19 |
| Abseil Loom16 PF32 | 34.30 | 32.81 | 19.04 | 27.59 | 27.05 |
| Boost Direct PF32 | 31.41 | 24.95 | 10.37 | 23.52 | 21.74 |
| Boost Manual16 PF32 | 36.94 | 31.69 | 14.85 | 29.43 | 25.65 |
| Boost Loom16 PF32 | 51.70 | 41.38 | 16.61 | 35.04 | 33.24 |
| F14Fast Direct PF32 | 29.03 | 22.65 | 6.08 | 20.71 | 18.13 |
| F14Fast Manual16 PF32 | 36.11 | 29.25 | 7.13 | 29.16 | 22.21 |
| F14Fast Loom16 PF32 | 44.30 | 38.85 | 7.99 | 41.77 | 26.91 |
| F14Value Direct PF32 | 26.85 | 22.83 | 11.40 | 21.98 | 20.04 |
| F14Value Manual16 PF32 | 33.19 | 30.15 | 15.40 | 28.53 | 25.01 |
| F14Value Loom16 PF32 | 40.34 | 37.74 | 18.12 | 37.74 | 30.24 |
| Abseil ID8 Direct PF32 | 46.58 | 35.36 | 11.11 | 36.81 | 29.63 |
| Abseil ID8 Manual16 PF32 | 29.87 | 25.60 | 14.68 | 23.87 | 22.75 |
| Abseil ID8 Loom16 PF32 | 32.27 | 30.40 | 15.50 | 26.00 | 24.23 |
| Boost ID8 Direct PF32 | 47.18 | 36.61 | 10.16 | 33.94 | 29.90 |
| Boost ID8 Manual16 PF32 | 38.81 | 33.26 | 14.30 | 32.06 | 27.68 |
| Boost ID8 Loom16 PF32 | 50.70 | 42.72 | 19.37 | 35.47 | 35.81 |
| F14Fast ID8 Direct PF32 | 43.25 | 34.34 | 10.39 | 35.39 | 27.55 |
| F14Fast ID8 Manual16 PF32 | 32.56 | 29.03 | 13.53 | 27.49 | 24.18 |
| F14Fast ID8 Loom16 PF32 | 38.48 | 35.54 | 16.00 | 34.86 | 28.65 |

## Best measured read treatment per arm W32 / V64

| Implementation | Hit | Random50 | Miss |
| --- | --- | --- | --- |
| Ikea getter | 22.78 (Direct, PF32) | 21.26 (Direct, PF32) | 6.49 (Direct, PF32) |
| Borrowed inline | 12.61 (Manual16, PF32) | 12.29 (Manual16, PF32) | 6.51 (Direct, PF32) |
| Ikea ID8 | 30.68 (Manual16, PF32) | 28.00 (Manual16, PF32) | 6.01 (Direct, PF32) |
| Ikea ID4 | 33.57 (Manual16, PF0) | 34.53 (Manual16, PF0) | 14.19 (Direct, PF32) |
| Abseil | 26.21 (Direct, PF0) | 23.31 (Direct, PF0) | 11.93 (Direct, PF0) |
| Boost | 31.41 (Direct, PF32) | 24.95 (Direct, PF32) | 10.37 (Direct, PF32) |
| F14Fast | 29.03 (Direct, PF32) | 22.65 (Direct, PF32) | 6.08 (Direct, PF32) |
| F14Value | 26.85 (Direct, PF32) | 22.83 (Direct, PF32) | 11.40 (Direct, PF32) |
| Abseil ID8 | 29.87 (Manual16, PF32) | 25.60 (Manual16, PF32) | 10.42 (Direct, PF0) |
| Boost ID8 | 38.81 (Manual16, PF32) | 33.26 (Manual16, PF32) | 10.16 (Direct, PF32) |
| F14Fast ID8 | 32.56 (Manual16, PF32) | 29.03 (Manual16, PF32) | 10.39 (Direct, PF32) |

## Cold writes and growth W32 / V8

| Implementation | Replace | Increment8 | Insert | Upsert50 | Erase | Refill | Grow/entry |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Ikea getter | 33.10 | 40.67 | 59.68 | 59.46 | 45.78 | 59.22 | 12.85 |
| Borrowed inline | 33.09 | 37.35 | 60.44 | 59.09 | 45.13 | 59.44 | 13.05 |
| Ikea ID8 | 47.15 | 50.73 | 67.96 | 79.52 | 96.18 | 67.40 | 12.66 |
| Ikea ID4 | 43.58 | 48.95 | 66.95 | 72.07 | 79.87 | 66.75 | 10.87 |
| Abseil | 35.87 | 36.12 | 59.32 | 54.26 | 54.68 | 69.15 | 22.81 |
| Boost | 35.00 | 32.95 | 37.18 | 42.88 | 35.70 | 37.59 | 17.11 |
| F14Fast | 35.37 | 33.55 | 35.61 | 39.67 | 44.26 | 37.13 | 14.69 |
| F14Value | 35.36 | 33.05 | 36.29 | 39.92 | 43.41 | 35.86 | 14.44 |
| Abseil ID8 | 44.08 | 57.24 | 77.10 | 67.33 | 76.88 | 92.32 | 23.08 |
| Boost ID8 | 42.82 | 56.80 | 56.39 | 58.62 | 53.69 | 46.95 | 16.67 |

## Cold writes and growth W32 / V16

| Implementation | Replace | Increment8 | Insert | Upsert50 | Erase | Refill | Grow/entry |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Ikea getter | 33.30 | 41.59 | 61.84 | 60.95 | 45.74 | 63.51 | 16.72 |
| Borrowed inline | 33.41 | 37.47 | 62.11 | 61.16 | 46.09 | 62.95 | 16.77 |
| Ikea ID8 | 47.82 | 54.04 | 71.56 | 81.19 | 94.90 | 67.31 | 12.55 |
| Ikea ID4 | 43.35 | 49.95 | 68.72 | 76.26 | 79.26 | 67.15 | 10.61 |
| Abseil | 38.10 | 38.71 | 60.73 | 55.83 | 56.67 | 73.27 | 26.32 |
| Boost | 36.75 | 36.01 | 37.84 | 46.92 | 40.25 | 37.66 | 23.89 |
| F14Fast | 40.70 | 36.84 | 37.19 | 46.54 | 48.68 | 37.00 | 27.34 |
| F14Value | 40.64 | 36.89 | 37.28 | 46.15 | 48.40 | 37.24 | 27.39 |
| Abseil ID8 | 44.62 | 58.52 | 78.60 | 68.98 | 78.06 | 92.81 | 22.81 |
| Boost ID8 | 44.16 | 57.22 | 57.78 | 60.28 | 53.43 | 46.69 | 17.00 |

## Cold writes and growth W32 / V32

| Implementation | Replace | Increment8 | Insert | Upsert50 | Erase | Refill | Grow/entry |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Ikea getter | 29.72 | 36.01 | 42.09 | 40.74 | 27.36 | 41.23 | 33.82 |
| Borrowed inline | 30.08 | 36.50 | 41.72 | 40.73 | 25.93 | 40.78 | 33.43 |
| Ikea ID8 | 45.28 | 49.36 | 62.63 | 73.27 | 63.72 | 49.22 | 13.99 |
| Ikea ID4 | 37.98 | 45.06 | 66.08 | 70.57 | 52.20 | 52.58 | 10.82 |
| Abseil | 33.25 | 33.53 | 41.29 | 46.96 | 51.48 | 51.84 | 38.67 |
| Boost | 35.75 | 36.07 | 35.40 | 46.11 | 40.62 | 34.75 | 28.92 |
| F14Fast | 42.43 | 47.21 | 42.75 | 59.70 | 83.78 | 37.86 | 38.37 |
| F14Value | 44.93 | 39.93 | 49.21 | 53.93 | 55.63 | 47.98 | 37.63 |
| Abseil ID8 | 38.13 | 52.32 | 58.67 | 57.16 | 66.92 | 67.27 | 23.84 |
| Boost ID8 | 44.02 | 56.47 | 55.41 | 62.49 | 52.06 | 41.51 | 13.58 |
| F14Fast ID8 | 44.99 | 57.23 | 71.52 | 69.33 | 70.62 | 64.24 | 11.47 |

## Cold writes and growth W32 / V64

| Implementation | Replace | Increment8 | Insert | Upsert50 | Erase | Refill | Grow/entry |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Ikea getter | 35.24 | 37.44 | 48.30 | 47.61 | 26.83 | 47.65 | 55.04 |
| Borrowed inline | 35.00 | 37.29 | 48.82 | 46.76 | 26.48 | 47.48 | 54.94 |
| Ikea ID8 | 47.27 | 49.21 | 68.98 | 79.23 | 64.23 | 50.99 | 13.94 |
| Ikea ID4 | 38.91 | 44.93 | 70.23 | 69.10 | 53.46 | 53.77 | 10.60 |
| Abseil | 38.21 | 36.64 | 43.17 | 50.75 | 53.81 | 53.92 | 60.06 |
| Boost | 40.52 | 37.48 | 37.54 | 48.52 | 41.59 | 36.97 | 47.68 |
| F14Fast | 49.52 | 49.31 | 49.39 | 64.34 | 104.69 | 38.41 | 49.37 |
| F14Value | 46.88 | 46.38 | 52.49 | 57.59 | 63.12 | 50.98 | 57.06 |
| Abseil ID8 | 39.66 | 53.92 | 68.70 | 63.22 | 67.09 | 70.02 | 24.29 |
| Boost ID8 | 45.95 | 58.30 | 62.98 | 68.41 | 53.43 | 44.71 | 13.27 |
| F14Fast ID8 | 46.35 | 58.27 | 78.60 | 74.69 | 71.42 | 64.96 | 11.12 |

## Cold writes and growth W32 / V128

| Implementation | Replace | Increment8 | Insert | Upsert50 | Erase | Refill | Grow/entry |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Ikea getter | 44.17 | 36.20 | 90.06 | 55.51 | 26.00 | 57.10 | 63.61 |
| Borrowed inline | 44.44 | 34.66 | 87.99 | 55.62 | 25.63 | 55.94 | 64.03 |
| Ikea ID8 | 47.75 | 43.31 | 76.22 | 70.98 | 54.31 | 45.29 | 6.57 |
| Ikea ID4 | 34.12 | 37.34 | 77.63 | 71.95 | 40.98 | 49.63 | 4.59 |
| Abseil | 34.41 | 33.10 | 38.61 | 44.89 | 50.58 | 46.11 | 93.24 |
| Boost | 35.23 | 34.31 | 33.93 | 42.35 | 37.98 | 32.71 | 75.09 |
| F14Fast | 48.04 | 46.51 | 53.51 | 66.48 | 97.20 | 33.79 | 61.74 |
| F14Value | 45.73 | 46.36 | 49.49 | 57.03 | 60.83 | 46.89 | 79.72 |
| Abseil ID8 | 37.62 | 50.26 | 68.29 | 62.20 | 62.46 | 56.93 | 20.16 |
| Boost ID8 | 57.19 | 52.35 | 66.66 | 65.81 | 45.87 | 36.91 | 12.10 |
| F14Fast ID8 | 43.54 | 55.55 | 82.81 | 77.03 | 68.01 | 58.25 | 11.93 |

## V64 warm memory and growth (Ikea retains source; peer native rehash releases it)

| Implementation | Steady B/live | Grow ns/live |
| --- | --- | --- |
| Ikea getter | 85.08 | 21.92 |
| Borrowed inline | 85.08 | 21.37 |
| Ikea ID8 | 79.08 | 5.55 |
| Ikea ID4 | 74.08 | 4.47 |
| Abseil | 81.12 | 24.39 |
| Boost | 152.36 | 42.07 |
| F14Fast | 77.41 | 16.47 |
| F14Value | 143.53 | 38.23 |
| Abseil ID8 | 79.30 | 10.16 |
| Boost ID8 | 92.83 | 12.46 |

## V64 cold memory and growth (Ikea retains source; peer native rehash releases it)

| Implementation | Steady B/live | Grow ns/live |
| --- | --- | --- |
| Ikea getter | 81.81 | 55.04 |
| Borrowed inline | 81.81 | 54.94 |
| Ikea ID8 | 77.19 | 13.94 |
| Ikea ID4 | 72.28 | 10.60 |
| Abseil | 108.24 | 60.06 |
| Boost | 101.57 | 47.68 |
| F14Fast | 80.55 | 49.37 |
| F14Value | 95.69 | 57.06 |
| Abseil ID8 | 84.39 | 24.29 |
| Boost ID8 | 83.22 | 13.27 |
| F14Fast ID8 | 82.82 | 11.12 |

## Fixed-population index-capacity ablation V64 warm

| Implementation / index b / consumer | Hit | Random50 | Miss | B/live |
| --- | --- | --- | --- | --- |
| Ikea getter b9 Direct | 2.76 | 9.52 | 3.78 | 85.08 |
| Borrowed inline b9 Direct | 2.53 | 6.08 | 3.55 | 85.08 |
| Ikea ID8 b9 Direct | 2.91 | 7.49 | 3.58 | 79.08 |
| Ikea ID8 b10 Direct | 2.58 | 4.36 | 1.13 | 92.28 |
| Ikea ID4 b9 Direct | 2.76 | 5.84 | 3.35 | 74.08 |
| Ikea ID4 b10 Direct | 2.45 | 4.16 | 1.16 | 82.86 |
| Abseil b9 Direct | 2.94 | 11.84 | 10.19 | 81.12 |
| Boost b9 Direct | 2.64 | 6.60 | 1.20 | 152.36 |
| F14Fast b9 Direct | 2.98 | 6.62 | 1.51 | 77.41 |
| F14Value b9 Direct | 2.60 | 6.80 | 1.42 | 143.53 |
| Abseil ID8 b9 Direct | 3.24 | 11.68 | 10.26 | 79.30 |
| Boost ID8 b9 Direct | 3.11 | 6.98 | 1.22 | 92.83 |
| F14Fast ID8 b9 Direct | 2.96 | 6.47 | 1.42 | 92.25 |

## Index-capacity ablation V64 warm: writes and growth

| Implementation | Replace | Increment8 | Insert | Upsert50 | Erase | Refill | Grow/entry |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Ikea getter b9 | 4.71 | 2.75 | 7.93 | 6.98 | 3.17 | 7.70 | 20.94 |
| Borrowed inline b9 | 4.82 | 2.76 | 8.10 | 7.17 | 3.25 | 7.60 | 21.34 |
| Ikea ID8 b9 | 4.27 | 3.60 | 10.19 | 7.38 | 6.82 | 10.33 | 5.66 |
| Ikea ID8 b10 | 3.86 | 3.07 | 8.39 | 6.18 | 6.16 | 5.47 | 10.69 |
| Ikea ID4 b9 | 3.92 | 3.32 | 10.14 | 8.37 | 6.56 | 8.63 | 4.48 |
| Ikea ID4 b10 | 3.38 | 2.75 | 8.43 | 7.13 | 5.61 | 5.78 | 8.50 |
| Abseil b9 | 3.97 | 3.26 | 8.09 | 6.36 | 12.95 | 19.91 | 24.16 |
| Boost b9 | 3.12 | 2.41 | 4.13 | 3.87 | 2.75 | 4.07 | 39.98 |
| F14Fast b9 | 3.80 | 3.10 | 3.65 | 4.32 | 7.01 | 3.86 | 17.13 |
| F14Value b9 | 3.40 | 2.63 | 4.13 | 3.98 | 3.77 | 4.11 | 34.34 |
| Abseil ID8 b9 | 4.09 | 3.79 | 11.07 | 8.51 | 15.44 | 23.11 | 9.97 |
| Boost ID8 b9 | 3.66 | 2.89 | 5.55 | 4.93 | 5.13 | 4.94 | 12.12 |
| F14Fast ID8 b9 | 3.56 | 3.02 | 5.59 | 4.92 | 8.55 | 5.28 | 8.41 |

## Index-capacity ablation V64 cold: writes and growth

| Implementation | Replace | Increment8 | Insert | Upsert50 | Erase | Refill | Grow/entry |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Ikea getter b17 | 33.80 | 35.72 | 46.66 | 45.66 | 25.80 | 46.34 | 53.87 |
| Borrowed inline b17 | 33.75 | 35.99 | 46.58 | 45.63 | 25.84 | 46.21 | 53.74 |
| Ikea ID8 b17 | 44.84 | 48.16 | 67.82 | 74.15 | 62.32 | 49.63 | 13.54 |
| Ikea ID8 b18 | 38.89 | 45.60 | 70.35 | 78.84 | 75.83 | 57.00 | 21.11 |
| Ikea ID4 b17 | 37.70 | 43.89 | 70.01 | 68.37 | 51.79 | 52.69 | 10.40 |
| Ikea ID4 b18 | 34.40 | 40.57 | 69.53 | 61.01 | 61.87 | 56.59 | 15.71 |
| Abseil b17 | 37.68 | 35.46 | 42.86 | 49.82 | 53.11 | 53.22 | 57.75 |
| Boost b17 | 39.84 | 36.80 | 36.72 | 47.13 | 40.59 | 35.92 | 45.59 |
| F14Fast b17 | 48.01 | 47.07 | 50.14 | 62.08 | 101.07 | 39.00 | 46.85 |
| F14Value b17 | 46.24 | 44.95 | 51.61 | 54.40 | 60.77 | 49.46 | 55.15 |
| Abseil ID8 b17 | 38.72 | 52.07 | 66.92 | 61.44 | 66.88 | 66.33 | 22.91 |
| Boost ID8 b17 | 44.65 | 56.28 | 62.86 | 64.70 | 51.82 | 42.56 | 13.17 |
| F14Fast ID8 b17 | 45.86 | 57.59 | 78.40 | 75.21 | 70.94 | 64.89 | 11.58 |

## Cold W32 / V64 / first16 consumed, PF32

| Implementation / consumer | Hit | Random50 | Miss | Alternating50 | Runs64 |
| --- | --- | --- | --- | --- | --- |
| Ikea getter Direct | 22.92 | 21.14 | 6.49 | 19.82 | 14.97 |
| Ikea getter Manual16 | 31.28 | 27.56 | 7.56 | 27.56 | 19.54 |
| Ikea getter Loom16 | 34.42 | 32.81 | 8.48 | 30.77 | 21.98 |
| Borrowed inline Direct | 24.31 | 21.72 | 6.54 | 20.82 | 15.84 |
| Borrowed inline Manual16 | 12.58 | 12.11 | 7.59 | 11.08 | 10.29 |
| Borrowed inline Loom16 | 13.57 | 14.97 | 8.62 | 12.07 | 11.38 |
| Ikea ID8 Direct | 39.01 | 33.94 | 5.90 | 35.18 | 23.85 |
| Ikea ID8 Manual16 | 30.05 | 28.11 | 6.92 | 28.45 | 19.92 |
| Ikea ID8 Loom16 | 34.80 | 33.14 | 7.91 | 31.57 | 22.97 |
| Ikea ID4 Direct | 45.48 | 40.36 | 14.21 | 39.83 | 31.35 |
| Ikea ID4 Manual16 | 38.89 | 35.24 | 17.89 | 33.08 | 29.45 |
| Ikea ID4 Loom16 | 44.49 | 38.89 | 19.03 | 34.23 | 33.17 |
| Abseil Direct | 28.44 | 25.15 | 13.38 | 22.45 | 21.41 |
| Abseil Manual16 | 30.40 | 27.14 | 17.28 | 24.77 | 24.26 |
| Abseil Loom16 | 33.02 | 30.77 | 17.70 | 25.25 | 25.75 |
| Boost Direct | 29.59 | 24.33 | 10.32 | 22.65 | 20.79 |
| Boost Manual16 | 37.29 | 32.30 | 13.75 | 31.97 | 26.92 |
| Boost Loom16 | 43.83 | 38.30 | 15.08 | 33.78 | 28.91 |
| F14Fast Direct | 27.47 | 21.88 | 6.08 | 22.14 | 17.23 |
| F14Fast Manual16 | 36.17 | 30.83 | 7.21 | 29.40 | 22.21 |
| F14Fast Loom16 | 43.86 | 36.77 | 7.78 | 30.68 | 26.33 |
| F14Value Direct | 25.74 | 22.29 | 11.53 | 21.29 | 19.07 |
| F14Value Manual16 | 33.11 | 29.02 | 14.80 | 27.47 | 24.60 |
| F14Value Loom16 | 40.16 | 35.55 | 16.84 | 30.77 | 29.17 |
| Abseil ID8 Direct | 46.39 | 35.39 | 11.01 | 36.39 | 29.59 |
| Abseil ID8 Manual16 | 30.48 | 25.67 | 14.57 | 23.86 | 22.99 |
| Abseil ID8 Loom16 | 31.77 | 28.99 | 14.95 | 24.04 | 23.66 |
| Boost ID8 Direct | 48.19 | 37.14 | 10.17 | 33.91 | 30.49 |
| Boost ID8 Manual16 | 40.19 | 34.09 | 14.05 | 32.70 | 28.13 |
| Boost ID8 Loom16 | 43.40 | 40.37 | 14.74 | 34.86 | 30.68 |
| F14Fast ID8 Direct | 46.64 | 34.83 | 10.44 | 35.23 | 29.46 |
| F14Fast ID8 Manual16 | 31.85 | 28.85 | 13.53 | 27.39 | 23.95 |
| F14Fast ID8 Loom16 | 38.20 | 34.82 | 14.96 | 33.56 | 27.65 |

