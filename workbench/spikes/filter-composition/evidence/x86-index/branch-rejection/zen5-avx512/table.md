## Varied hot queries: dependent latency / reciprocal throughput, ns per query

| K / N | Ikea indexof | Fused columns | Fused prepared first | Fused prepared full | Full filter + first bit |
| --- | --- | --- | --- | --- | --- |
| 12 / 32 | 10.32 / 1.47 | 7.51 / 4.00 | 14.31 / 9.94 | 9.81 / 2.68 | 10.12 / 1.44 |
| 14 / 32 | 11.35 / 2.30 | 7.72 / 5.00 | 13.51 / 10.32 | 10.21 / 3.10 | 11.16 / 2.22 |
| 30 / 16 | 8.91 / 6.22 | 9.63 / 4.43 | 8.62 / 6.10 | 9.57 / 2.19 | 9.95 / 1.70 |
| 38 / 16 | 8.68 / 5.64 | 11.04 / 4.48 | 8.31 / 5.63 | 9.44 / 2.20 | 11.22 / 2.54 |
