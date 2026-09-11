# Measured comparisons

Ratios use per-case medians of sequential, pinned repetitions. Lower is faster.
`get16` consumes all output lanes; `get16-ends` consumes endpoints, matching the earlier campaign.
Compare these harnesses separately. Sum controls include the strongest recorded grouped/deferred
implementation where present. Overwrite summary/coverage rows do additional work beyond encoding.
Partial writes have no equivalent prior control in this suite. These are warm microbenchmarks,
not measurements of Engine publication, Loom scheduling, cold memory or whole queries.

| Family / endpoint | Cases | Median ratio | Worst ratio | >1.4× |
| --- | ---: | ---: | ---: | ---: |
| decode/ikea2 vs ikea | 61 | 1.502 | 2.159 | 32 |
| decode/ikea2 vs prior | 15 | 1.018 | 1.433 | 1 |
| get16-ends/ikea2-range | 15 | 1.071 | 1.303 | 0 |
| get16-ends/ikea2-region | 15 | 1.062 | 1.254 | 0 |
| get16/ikea2-range | 15 | 0.964 | 1.107 | 0 |
| get16/ikea2-region | 15 | 0.955 | 1.073 | 0 |
| overwrite-all/ikea2 | 76 | 1.210 | 10.448 | 13 |
| overwrite/ikea2-coverage | 9 | 1.204 | 1.775 | 2 |
| overwrite/ikea2-raw | 9 | 1.042 | 1.366 | 0 |
| overwrite/ikea2-sum-coverage | 18 | 0.606 | 1.266 | 0 |
| point/ikea2 vs prior | 15 | 0.993 | 1.011 | 0 |
| sum/ikea2-native | 14 | 0.668 | 1.814 | 2 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.
