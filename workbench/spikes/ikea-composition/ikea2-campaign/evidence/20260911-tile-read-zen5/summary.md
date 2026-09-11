# Measured comparisons

Ratios use per-case medians of sequential, pinned repetitions. Lower is faster.
`get16` consumes all output lanes; `get16-ends` consumes endpoints, matching the earlier campaign.
Compare these harnesses separately. Sum controls include the strongest recorded grouped/deferred
implementation where present. Overwrite summary/coverage rows do additional work beyond encoding.
Partial writes have no equivalent prior control in this suite. These are warm microbenchmarks,
not measurements of Engine publication, Loom scheduling, cold memory or whole queries.

| Family / endpoint | Cases | Median ratio | Worst ratio | >1.4× |
| --- | ---: | ---: | ---: | ---: |
| decode/ikea2 vs ikea | 61 | 1.044 | 1.969 | 11 |
| decode/ikea2 vs prior | 15 | 1.341 | 2.075 | 7 |
| get16-ends/ikea2-range | 15 | 1.154 | 1.324 | 0 |
| get16-ends/ikea2-region | 15 | 0.944 | 1.027 | 0 |
| get16/ikea2-range | 15 | 1.017 | 1.108 | 0 |
| get16/ikea2-region | 15 | 0.999 | 1.072 | 0 |
| point/ikea2 vs prior | 15 | 0.997 | 2.265 | 1 |
| sum/ikea2-native | 14 | 0.600 | 1.276 | 0 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.
