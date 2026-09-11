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
| decode/ikea2 vs ikea | 61 | 1.014 | 1.493 | 1 |
| decode/ikea2 vs prior | 15 | 1.018 | 1.451 | 1 |
| get16-ends/ikea2-range | 15 | 1.077 | 1.465 | 1 |
| get16-ends/ikea2-region | 15 | 0.959 | 1.116 | 0 |
| get16/ikea2-range | 15 | 0.999 | 1.196 | 0 |
| get16/ikea2-region | 15 | 0.974 | 1.096 | 0 |
| overwrite-all/ikea2 | 76 | 1.160 | 1.508 | 4 |
| overwrite/ikea2-coverage | 9 | 1.174 | 1.791 | 2 |
| overwrite/ikea2-raw | 9 | 1.041 | 1.379 | 0 |
| overwrite/ikea2-sum-coverage | 18 | 0.558 | 0.970 | 0 |
| pipeline-mutation/cps | 6 | 1.198 | 1.322 | 0 |
| pipeline-mutation/fused | 6 | 0.634 | 0.789 | 0 |
| pipeline-mutation/scratch-calls | 6 | 1.318 | 1.502 | 1 |
| point/ikea2 vs prior | 15 | 0.998 | 1.077 | 0 |
| sum/ikea2-native | 14 | 0.558 | 1.060 | 0 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.
