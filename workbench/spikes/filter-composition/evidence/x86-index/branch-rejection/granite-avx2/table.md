## Varied hot queries: dependent latency / reciprocal throughput, ns per query

| K / N | Ikea indexof | Fused columns | Fused prepared first | Fused prepared full | Full filter + first bit |
| --- | --- | --- | --- | --- | --- |
| 12 / 32 | 13.21 / 10.10 | 9.86 / 5.47 | 12.92 / 10.15 | 10.18 / 4.71 | 10.55 / 7.08 |
| 14 / 32 | 13.81 / 10.59 | 10.80 / 7.25 | 13.56 / 10.46 | 11.85 / 5.96 | 11.78 / 8.39 |
| 30 / 16 | 9.51 / 7.44 | 11.80 / 6.79 | 9.11 / 7.49 | 9.52 / 3.70 | 9.96 / 4.93 |
| 38 / 16 | 9.66 / 7.33 | 12.67 / 7.65 | 9.30 / 7.28 | 9.42 / 3.55 | 9.99 / 4.78 |
