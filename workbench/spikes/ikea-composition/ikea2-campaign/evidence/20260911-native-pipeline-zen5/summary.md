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
| decode/ikea2 vs ikea | 61 | 1.039 | 1.906 | 12 |
| decode/ikea2 vs prior | 15 | 1.152 | 1.290 | 0 |
| get16-ends/ikea2-range | 15 | 1.056 | 1.416 | 1 |
| get16-ends/ikea2-region | 15 | 0.919 | 1.029 | 0 |
| get16/ikea2-range | 15 | 1.013 | 1.108 | 0 |
| get16/ikea2-region | 15 | 0.999 | 1.018 | 0 |
| overwrite-all/ikea2 | 76 | 1.004 | 2.413 | 4 |
| overwrite/ikea2-coverage | 9 | 1.332 | 2.769 | 3 |
| overwrite/ikea2-raw | 9 | 1.021 | 2.414 | 1 |
| overwrite/ikea2-sum-coverage | 18 | 0.553 | 3.125 | 4 |
| pipeline-mutation/cps | 6 | 1.461 | 2.349 | 4 |
| pipeline-mutation/fused | 6 | 0.708 | 0.998 | 0 |
| pipeline-mutation/scratch-calls | 6 | 1.496 | 1.823 | 4 |
| point/ikea2 vs prior | 15 | 0.998 | 1.016 | 0 |
| sum/ikea2-native | 14 | 0.604 | 1.296 | 0 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.
