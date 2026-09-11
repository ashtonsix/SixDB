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
| decode/ikea2 vs ikea | 61 | 1.350 | 1.776 | 20 |
| decode/ikea2 vs prior | 15 | 1.017 | 1.625 | 1 |
| get16-ends/ikea2-range | 15 | 1.044 | 1.320 | 0 |
| get16-ends/ikea2-region | 15 | 0.956 | 1.383 | 0 |
| get16/ikea2-range | 15 | 0.996 | 1.085 | 0 |
| get16/ikea2-region | 15 | 0.968 | 1.158 | 0 |
| overwrite-all/ikea2 | 76 | 1.204 | 1.842 | 12 |
| overwrite/ikea2-coverage | 9 | 1.185 | 1.784 | 2 |
| overwrite/ikea2-raw | 9 | 1.041 | 1.370 | 0 |
| overwrite/ikea2-sum-coverage | 18 | 0.595 | 1.201 | 0 |
| pipeline-mutation/cps | 6 | 1.205 | 1.323 | 0 |
| pipeline-mutation/fused | 6 | 0.714 | 0.860 | 0 |
| pipeline-mutation/scratch-calls | 6 | 1.313 | 1.505 | 1 |
| point/ikea2 vs prior | 15 | 0.998 | 1.459 | 1 |
| sum/ikea2-native | 14 | 0.669 | 1.654 | 2 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.
