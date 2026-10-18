## Varied hot queries: dependent latency / reciprocal throughput, ns per query

| K / N | Ikea indexof | Fused columns | Fused prepared first | Fused prepared full | Full filter + first bit |
| --- | --- | --- | --- | --- | --- |
| 12 / 32 | 10.07 / 9.40 | 6.94 / 3.83 | 10.58 / 9.32 | 7.19 / 3.57 | 7.39 / 4.73 |
| 14 / 32 | 11.03 / 9.86 | 7.90 / 5.14 | 11.17 / 9.72 | 7.72 / 4.34 | 8.57 / 6.32 |
| 30 / 16 | 7.15 / 6.13 | 8.74 / 4.34 | 7.22 / 5.89 | 7.48 / 2.82 | 7.47 / 3.70 |
| 38 / 16 | 7.05 / 5.81 | 9.91 / 4.58 | 6.75 / 5.71 | 7.01 / 2.83 | 7.00 / 3.75 |
