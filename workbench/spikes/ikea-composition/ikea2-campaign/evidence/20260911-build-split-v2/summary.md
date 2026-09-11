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
| decode/ikea2 vs ikea | 61 | 1.013 | 1.496 | 1 |
| decode/ikea2 vs prior | 15 | 1.017 | 1.611 | 2 |
| get16-ends/ikea2-range | 15 | 1.040 | 1.323 | 0 |
| get16-ends/ikea2-region | 15 | 0.951 | 1.267 | 0 |
| get16/ikea2-range | 15 | 0.992 | 1.062 | 0 |
| get16/ikea2-region | 15 | 0.963 | 1.053 | 0 |
| overwrite-all/ikea2 | 76 | 1.208 | 1.842 | 12 |
| overwrite/ikea2-coverage | 9 | 1.179 | 1.784 | 2 |
| overwrite/ikea2-raw | 9 | 1.042 | 1.372 | 0 |
| overwrite/ikea2-sum-coverage | 18 | 0.558 | 0.971 | 0 |
| pipeline-mutation/cps | 6 | 1.199 | 1.321 | 0 |
| pipeline-mutation/fused | 6 | 0.634 | 0.788 | 0 |
| pipeline-mutation/scratch-calls | 6 | 1.319 | 1.503 | 1 |
| point/ikea2 vs prior | 15 | 0.996 | 1.025 | 0 |
| sum/ikea2-native | 14 | 0.574 | 1.061 | 0 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.
