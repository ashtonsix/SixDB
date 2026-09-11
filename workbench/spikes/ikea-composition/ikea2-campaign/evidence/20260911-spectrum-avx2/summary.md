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
| decode/ikea2 vs ikea | 61 | 1.186 | 2.811 | 5 |
| decode/ikea2 vs prior | 15 | 1.015 | 1.250 | 0 |
| get16-ends/ikea2-range | 15 | 1.170 | 1.290 | 0 |
| get16-ends/ikea2-region | 15 | 0.998 | 1.535 | 1 |
| get16/ikea2-range | 15 | 1.044 | 1.122 | 0 |
| get16/ikea2-region | 15 | 0.997 | 1.234 | 0 |
| overwrite-all/ikea2 | 76 | 0.972 | 2.054 | 2 |
| overwrite/ikea2-coverage | 9 | 0.816 | 1.469 | 2 |
| overwrite/ikea2-raw | 9 | 0.815 | 1.307 | 0 |
| overwrite/ikea2-sum-coverage | 18 | 0.495 | 1.224 | 0 |
| pipeline-mutation/cps | 6 | 1.278 | 1.612 | 2 |
| pipeline-mutation/fused | 6 | 0.636 | 0.818 | 0 |
| pipeline-mutation/scratch-calls | 6 | 1.387 | 1.692 | 3 |
| point/ikea2 vs prior | 15 | 0.997 | 1.021 | 0 |
| sum/ikea2-native | 14 | 0.499 | 1.294 | 0 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.
