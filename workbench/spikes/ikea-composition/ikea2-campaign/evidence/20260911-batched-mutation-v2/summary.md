# Measured comparisons

Ratios use per-case medians of sequential, pinned repetitions. Lower is faster.
`get16` consumes all output lanes; `get16-ends` consumes endpoints, matching the earlier campaign.
Compare these harnesses separately. Sum controls include the strongest recorded grouped/deferred
implementation where present. Overwrite summary/coverage rows do additional work beyond encoding.
Partial writes have no equivalent prior control in this suite. These are warm microbenchmarks,
not measurements of Engine publication, Loom scheduling, cold memory or whole queries.

| Family / endpoint | Cases | Median ratio | Worst ratio | >1.4× |
| --- | ---: | ---: | ---: | ---: |
| decode/ikea2 vs ikea | 61 | 1.352 | 1.793 | 20 |
| decode/ikea2 vs prior | 15 | 1.006 | 1.644 | 2 |
| get16-ends/ikea2-range | 15 | 1.049 | 1.511 | 1 |
| get16-ends/ikea2-region | 15 | 0.954 | 1.194 | 0 |
| get16/ikea2-range | 15 | 0.993 | 1.180 | 0 |
| get16/ikea2-region | 15 | 0.975 | 1.070 | 0 |
| overwrite/ikea2-coverage | 9 | 1.747 | 2.915 | 6 |
| overwrite/ikea2-raw | 9 | 1.041 | 1.366 | 0 |
| overwrite/ikea2-sum-coverage | 9 | 3.231 | 6.092 | 9 |
| point/ikea2 vs prior | 15 | 0.998 | 1.110 | 0 |
| sum/ikea2-native | 14 | 0.659 | 1.653 | 2 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.
