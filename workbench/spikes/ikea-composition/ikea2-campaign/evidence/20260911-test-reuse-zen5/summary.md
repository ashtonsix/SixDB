# Measured comparisons

Ratios use per-case medians of sequential, pinned repetitions. Lower is faster.
`get16` consumes all output lanes; `get16-ends` consumes endpoints, matching the earlier campaign.
Compare these harnesses separately. Sum controls include the strongest recorded grouped/deferred
implementation where present. Overwrite summary/coverage rows do additional work beyond encoding.
Maintenance rows use an equivalent materialized control where present; inspect each named control.
Pipeline ratios compare execution styles sharing stage bodies; scratch calls also materialize lanes.
These are warm microbenchmarks,
not measurements of Engine publication, Loom scheduling, cold memory or whole queries.

| Family / endpoint | Cases | Median ratio | Worst ratio | >1.4× |
| --- | ---: | ---: | ---: | ---: |
| decode/ikea2 vs ikea | 61 | 1.032 | 1.592 | 7 |
| decode/ikea2 vs prior | 15 | 1.173 | 1.386 | 0 |
| get16-ends/ikea2-range | 15 | 1.145 | 1.332 | 0 |
| get16-ends/ikea2-region | 15 | 0.918 | 1.086 | 0 |
| get16/ikea2-range | 15 | 1.005 | 1.153 | 0 |
| get16/ikea2-region | 15 | 0.998 | 1.084 | 0 |
| overwrite-all/ikea2 | 76 | 1.016 | 1.341 | 0 |
| overwrite/ikea2-coverage | 9 | 1.341 | 2.331 | 3 |
| overwrite/ikea2-raw | 9 | 1.027 | 1.340 | 0 |
| overwrite/ikea2-sum-coverage | 18 | 0.301 | 1.199 | 0 |
| pipeline-mutation/cps | 6 | 1.569 | 3.550 | 3 |
| pipeline-mutation/fused | 6 | 0.691 | 0.907 | 0 |
| pipeline-mutation/scratch-calls | 6 | 1.710 | 2.510 | 4 |
| point/ikea2 vs prior | 15 | 0.998 | 1.303 | 0 |
| sum/ikea2-native | 14 | 0.601 | 1.149 | 0 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.
