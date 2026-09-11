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
| decode/ikea2 vs ikea | 61 | 1.152 | 1.427 | 1 |
| decode/ikea2 vs prior | 15 | 1.013 | 1.207 | 0 |
| get16-ends/ikea2-range | 15 | 1.113 | 1.288 | 0 |
| get16-ends/ikea2-region | 15 | 0.985 | 1.098 | 0 |
| get16/ikea2-range | 15 | 1.034 | 1.120 | 0 |
| get16/ikea2-region | 15 | 0.980 | 1.036 | 0 |
| overwrite-all/ikea2 | 76 | 0.842 | 1.309 | 0 |
| overwrite/ikea2-coverage | 9 | 0.819 | 1.496 | 2 |
| overwrite/ikea2-raw | 9 | 0.816 | 1.296 | 0 |
| overwrite/ikea2-sum-coverage | 18 | 0.464 | 1.052 | 0 |
| pipeline-mutation/cps | 6 | 1.279 | 1.614 | 2 |
| pipeline-mutation/fused | 6 | 0.636 | 0.818 | 0 |
| pipeline-mutation/scratch-calls | 6 | 1.383 | 1.692 | 3 |
| pipeline-packet/cps | 15 | 1.112 | 1.282 | 0 |
| pipeline-packet/fused | 15 | 1.052 | 1.363 | 0 |
| placed/mutation-bound | 18 | 0.790 | 1.308 | 0 |
| placed/sum-native | 18 | 0.994 | 1.209 | 0 |
| point/ikea2 vs prior | 15 | 0.998 | 1.024 | 0 |
| sum/ikea2-native | 14 | 0.501 | 1.290 | 0 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.
