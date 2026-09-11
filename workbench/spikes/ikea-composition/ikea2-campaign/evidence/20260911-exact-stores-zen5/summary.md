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
| decode/ikea2 vs ikea | 61 | 1.031 | 1.592 | 7 |
| decode/ikea2 vs prior | 15 | 1.169 | 1.382 | 0 |
| get16-ends/ikea2-range | 15 | 1.147 | 1.333 | 0 |
| get16-ends/ikea2-region | 15 | 0.937 | 1.082 | 0 |
| get16/ikea2-range | 15 | 1.008 | 1.139 | 0 |
| get16/ikea2-region | 15 | 0.999 | 1.092 | 0 |
| overwrite-all/ikea2 | 76 | 1.017 | 1.444 | 1 |
| overwrite/ikea2-coverage | 9 | 1.341 | 2.333 | 4 |
| overwrite/ikea2-raw | 9 | 1.019 | 1.442 | 1 |
| overwrite/ikea2-sum-coverage | 18 | 0.298 | 1.194 | 0 |
| pipeline-mutation/cps | 6 | 1.562 | 3.557 | 3 |
| pipeline-mutation/fused | 6 | 0.686 | 0.901 | 0 |
| pipeline-mutation/scratch-calls | 6 | 1.702 | 2.517 | 4 |
| point/ikea2 vs prior | 15 | 0.997 | 1.022 | 0 |
| sum/ikea2-native | 14 | 0.602 | 1.181 | 0 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.
