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
| decode/ikea2 vs ikea | 61 | 1.020 | 1.267 | 0 |
| decode/ikea2 vs prior | 15 | 1.144 | 1.385 | 0 |
| get16-ends/ikea2-range | 15 | 1.108 | 1.383 | 0 |
| get16-ends/ikea2-region | 15 | 0.910 | 0.996 | 0 |
| get16/ikea2-range | 15 | 1.016 | 1.060 | 0 |
| get16/ikea2-region | 15 | 0.998 | 1.016 | 0 |
| overwrite-all/ikea2 | 76 | 1.012 | 1.343 | 0 |
| overwrite/ikea2-coverage | 9 | 1.340 | 2.365 | 4 |
| overwrite/ikea2-raw | 9 | 1.029 | 1.346 | 0 |
| overwrite/ikea2-sum-coverage | 18 | 0.299 | 1.211 | 0 |
| pipeline-mutation/cps | 6 | 1.549 | 3.547 | 3 |
| pipeline-mutation/fused | 6 | 0.680 | 0.905 | 0 |
| pipeline-mutation/scratch-calls | 6 | 1.705 | 2.533 | 4 |
| point/ikea2 vs prior | 15 | 0.997 | 1.014 | 0 |
| sum/ikea2-native | 14 | 0.554 | 1.178 | 0 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.
