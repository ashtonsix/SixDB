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
| decode/ikea2 vs ikea | 61 | 1.134 | 1.430 | 1 |
| decode/ikea2 vs prior | 15 | 1.013 | 1.107 | 0 |
| get16-ends/ikea2-range | 15 | 1.116 | 1.289 | 0 |
| get16-ends/ikea2-region | 15 | 0.987 | 1.105 | 0 |
| get16/ikea2-range | 15 | 1.032 | 1.119 | 0 |
| get16/ikea2-region | 15 | 0.980 | 1.032 | 0 |
| overwrite-all/ikea2 | 76 | 0.809 | 2.552 | 1 |
| overwrite/ikea2-coverage | 9 | 0.811 | 1.739 | 2 |
| overwrite/ikea2-raw | 9 | 0.810 | 1.291 | 0 |
| overwrite/ikea2-sum-coverage | 18 | 0.516 | 1.228 | 0 |
| pipeline-mutation/cps | 6 | 1.278 | 1.614 | 2 |
| pipeline-mutation/fused | 6 | 0.672 | 0.884 | 0 |
| pipeline-mutation/scratch-calls | 6 | 1.388 | 1.695 | 3 |
| pipeline-packet/cps | 15 | 1.112 | 12.685 | 1 |
| pipeline-packet/fused | 15 | 1.001 | 1.362 | 0 |
| point/ikea2 vs prior | 15 | 1.001 | 1.379 | 0 |
| sum/ikea2-native | 14 | 0.499 | 1.292 | 0 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.
