# Measured comparisons

Ratios use per-case medians of sequential, pinned repetitions. Lower is faster.
`get16` consumes all output lanes; `get16-ends` consumes endpoints, matching the earlier campaign.
Compare these harnesses separately. Sum controls include the strongest recorded grouped/deferred
implementation where present. Overwrite summary/coverage rows do additional work beyond encoding.
Partial writes have no equivalent prior control in this suite. These are warm microbenchmarks,
not measurements of Engine publication, Loom scheduling, cold memory or whole queries.

| Family / endpoint | Cases | Median ratio | Worst ratio | >1.4× |
| --- | ---: | ---: | ---: | ---: |
| decode/ikea2 vs ikea | 61 | 1.045 | 1.896 | 12 |
| decode/ikea2 vs prior | 15 | 1.065 | 1.291 | 0 |
| get16-ends/ikea2-range | 15 | 1.141 | 1.331 | 0 |
| get16-ends/ikea2-region | 15 | 0.969 | 1.269 | 0 |
| get16/ikea2-range | 15 | 1.015 | 1.105 | 0 |
| get16/ikea2-region | 15 | 0.999 | 1.078 | 0 |
| overwrite-all/ikea2 | 76 | 1.005 | 2.525 | 5 |
| overwrite/ikea2-coverage | 9 | 1.281 | 2.692 | 3 |
| overwrite/ikea2-raw | 9 | 0.999 | 2.185 | 1 |
| overwrite/ikea2-sum-coverage | 18 | 0.555 | 3.120 | 5 |
| point/ikea2 vs prior | 15 | 0.998 | 1.023 | 0 |
| sum/ikea2-native | 14 | 0.608 | 1.284 | 0 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.
