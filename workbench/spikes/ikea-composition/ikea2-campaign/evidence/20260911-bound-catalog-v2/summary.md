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
| decode/ikea2 vs ikea | 61 | 1.008 | 1.462 | 1 |
| decode/ikea2 vs prior | 15 | 1.018 | 1.408 | 1 |
| get16-ends/ikea2-range | 15 | 1.046 | 1.285 | 0 |
| get16-ends/ikea2-region | 15 | 0.957 | 1.096 | 0 |
| get16/ikea2-range | 15 | 1.007 | 1.172 | 0 |
| get16/ikea2-region | 15 | 0.977 | 1.085 | 0 |
| overwrite-all/ikea2 | 76 | 1.159 | 1.505 | 4 |
| overwrite/ikea2-coverage | 9 | 1.042 | 1.569 | 2 |
| overwrite/ikea2-raw | 9 | 1.041 | 1.372 | 0 |
| overwrite/ikea2-sum-coverage | 18 | 0.569 | 0.971 | 0 |
| pipeline-mutation/cps | 6 | 1.197 | 1.323 | 0 |
| pipeline-mutation/fused | 6 | 0.634 | 0.790 | 0 |
| pipeline-mutation/scratch-calls | 6 | 1.319 | 1.505 | 1 |
| pipeline-packet/cps | 10 | 1.381 | 1.565 | 4 |
| pipeline-packet/fused | 10 | 1.039 | 1.300 | 0 |
| placed/mutation-bound | 18 | 0.708 | 1.029 | 0 |
| placed/sum-native | 18 | 0.991 | 1.656 | 3 |
| point/ikea2 vs prior | 15 | 0.998 | 1.133 | 0 |
| sum/ikea2-native | 14 | 0.551 | 1.062 | 0 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.
