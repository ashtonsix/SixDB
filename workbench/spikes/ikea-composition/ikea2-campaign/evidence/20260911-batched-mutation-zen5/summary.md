# Measured comparisons

Ratios use per-case medians of sequential, pinned repetitions. Lower is faster.
`get16` consumes all output lanes; `get16-ends` consumes endpoints, matching the earlier campaign.
Compare these harnesses separately. Sum controls include the strongest recorded grouped/deferred
implementation where present. Overwrite summary/coverage rows do additional work beyond encoding.
Partial writes have no equivalent prior control in this suite. These are warm microbenchmarks,
not measurements of Engine publication, Loom scheduling, cold memory or whole queries.

| Family / endpoint | Cases | Median ratio | Worst ratio | >1.4× |
| --- | ---: | ---: | ---: | ---: |
| decode/ikea2 vs ikea | 61 | 1.031 | 1.956 | 11 |
| decode/ikea2 vs prior | 15 | 1.237 | 1.974 | 4 |
| get16-ends/ikea2-range | 15 | 1.145 | 1.332 | 0 |
| get16-ends/ikea2-region | 15 | 0.972 | 1.046 | 0 |
| get16/ikea2-range | 15 | 1.005 | 1.111 | 0 |
| get16/ikea2-region | 15 | 1.000 | 1.020 | 0 |
| overwrite/ikea2-coverage | 9 | 2.471 | 3.321 | 8 |
| overwrite/ikea2-raw | 9 | 1.012 | 2.375 | 1 |
| overwrite/ikea2-sum-coverage | 9 | 5.244 | 22.029 | 9 |
| point/ikea2 vs prior | 15 | 0.997 | 1.313 | 0 |
| sum/ikea2-native | 14 | 0.600 | 1.294 | 0 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.
