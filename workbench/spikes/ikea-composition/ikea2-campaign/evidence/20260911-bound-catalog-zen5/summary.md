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
| decode/ikea2 vs ikea | 61 | 1.038 | 1.330 | 0 |
| decode/ikea2 vs prior | 15 | 1.161 | 1.383 | 0 |
| get16-ends/ikea2-range | 15 | 1.140 | 1.332 | 0 |
| get16-ends/ikea2-region | 15 | 0.959 | 1.242 | 0 |
| get16/ikea2-range | 15 | 1.016 | 1.209 | 0 |
| get16/ikea2-region | 15 | 1.001 | 1.113 | 0 |
| overwrite-all/ikea2 | 76 | 0.997 | 1.393 | 0 |
| overwrite/ikea2-coverage | 9 | 1.032 | 1.374 | 0 |
| overwrite/ikea2-raw | 9 | 1.000 | 1.342 | 0 |
| overwrite/ikea2-sum-coverage | 18 | 0.317 | 1.192 | 0 |
| pipeline-mutation/cps | 6 | 1.547 | 3.546 | 3 |
| pipeline-mutation/fused | 6 | 0.680 | 0.916 | 0 |
| pipeline-mutation/scratch-calls | 6 | 1.714 | 2.518 | 4 |
| pipeline-packet/cps | 15 | 1.190 | 2.037 | 2 |
| pipeline-packet/fused | 15 | 1.022 | 1.274 | 0 |
| placed/mutation-bound | 18 | 0.818 | 1.226 | 0 |
| placed/sum-native | 18 | 0.715 | 0.884 | 0 |
| point/ikea2 vs prior | 15 | 0.998 | 1.020 | 0 |
| sum/ikea2-native | 14 | 0.550 | 1.152 | 0 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.
