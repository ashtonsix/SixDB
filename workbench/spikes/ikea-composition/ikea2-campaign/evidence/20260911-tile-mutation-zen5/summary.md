# Measured comparisons

Ratios use per-case medians of sequential, pinned repetitions. Lower is faster.
`get16` consumes all output lanes; `get16-ends` consumes endpoints, matching the earlier campaign.
Compare these harnesses separately. Sum controls include the strongest recorded grouped/deferred
implementation where present. Overwrite summary/coverage rows do additional work beyond encoding.
Partial writes have no equivalent prior control in this suite. These are warm microbenchmarks,
not measurements of Engine publication, Loom scheduling, cold memory or whole queries.

| Family / endpoint | Cases | Median ratio | Worst ratio | >1.4× |
| --- | ---: | ---: | ---: | ---: |
| decode/ikea2 vs ikea | 61 | 1.037 | 1.952 | 11 |
| decode/ikea2 vs prior | 15 | 1.232 | 1.982 | 4 |
| get16-ends/ikea2-range | 15 | 1.145 | 1.316 | 0 |
| get16-ends/ikea2-region | 15 | 0.941 | 1.062 | 0 |
| get16/ikea2-range | 15 | 1.029 | 1.104 | 0 |
| get16/ikea2-region | 15 | 0.996 | 1.017 | 0 |
| overwrite/ikea2-coverage | 9 | 2.611 | 21.475 | 9 |
| overwrite/ikea2-raw | 9 | 2.312 | 8.043 | 7 |
| overwrite/ikea2-sum-coverage | 9 | 5.898 | 52.784 | 9 |
| point/ikea2 vs prior | 15 | 0.997 | 1.019 | 0 |
| sum/ikea2-native | 14 | 0.602 | 1.311 | 0 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.
