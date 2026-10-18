## Varied hot queries: dependent latency / reciprocal throughput, ns per query

| K / N | Ikea indexof | Fused columns | Fused prepared first | Fused prepared full | Full filter + first bit |
| --- | --- | --- | --- | --- | --- |
| 12 / 32 | 11.39 / 2.79 | 10.12 / 5.48 | 15.47 / 10.54 | 11.88 / 4.18 | 11.24 / 2.93 |
| 14 / 32 | 12.98 / 3.74 | 11.52 / 7.21 | 16.01 / 10.87 | 12.86 / 4.72 | 12.85 / 3.76 |
| 30 / 16 | 10.87 / 7.15 | 13.19 / 6.42 | 10.59 / 7.09 | 11.31 / 3.32 | 12.76 / 3.55 |
| 38 / 16 | 11.03 / 7.09 | 14.17 / 7.33 | 10.64 / 6.78 | 11.52 / 3.11 | 13.01 / 4.96 |
