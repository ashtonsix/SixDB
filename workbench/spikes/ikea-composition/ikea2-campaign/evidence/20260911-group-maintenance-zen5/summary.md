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
| decode/ikea2 vs ikea | 61 | 1.040 | 1.871 | 12 |
| decode/ikea2 vs prior | 15 | 1.106 | 1.326 | 0 |
| get16-ends/ikea2-range | 15 | 1.130 | 1.333 | 0 |
| get16-ends/ikea2-region | 15 | 0.950 | 1.044 | 0 |
| get16/ikea2-range | 15 | 1.017 | 1.108 | 0 |
| get16/ikea2-region | 15 | 0.999 | 1.018 | 0 |
| overwrite-all/ikea2 | 76 | 1.002 | 2.282 | 4 |
| overwrite/ikea2-coverage | 9 | 1.324 | 2.801 | 3 |
| overwrite/ikea2-raw | 9 | 1.017 | 2.285 | 1 |
| overwrite/ikea2-sum-coverage | 18 | 0.501 | 2.968 | 4 |
| pipeline-mutation/cps | 6 | 1.554 | 3.575 | 3 |
| pipeline-mutation/fused | 6 | 0.783 | 0.956 | 0 |
| pipeline-mutation/scratch-calls | 6 | 1.706 | 2.560 | 4 |
| point/ikea2 vs prior | 15 | 0.996 | 2.325 | 1 |
| sum/ikea2-native | 14 | 0.604 | 1.268 | 0 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.
