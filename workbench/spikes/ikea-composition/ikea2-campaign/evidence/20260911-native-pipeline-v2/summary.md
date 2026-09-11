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
| decode/ikea2 vs ikea | 61 | 1.352 | 1.794 | 21 |
| decode/ikea2 vs prior | 15 | 1.018 | 1.633 | 2 |
| get16-ends/ikea2-range | 15 | 0.976 | 1.384 | 0 |
| get16-ends/ikea2-region | 15 | 0.950 | 1.171 | 0 |
| get16/ikea2-range | 15 | 0.993 | 1.145 | 0 |
| get16/ikea2-region | 15 | 0.964 | 1.051 | 0 |
| overwrite-all/ikea2 | 76 | 1.208 | 1.842 | 12 |
| overwrite/ikea2-coverage | 9 | 1.170 | 1.798 | 2 |
| overwrite/ikea2-raw | 9 | 1.042 | 1.382 | 0 |
| overwrite/ikea2-sum-coverage | 18 | 0.595 | 1.200 | 0 |
| pipeline-mutation/cps | 6 | 1.208 | 1.324 | 0 |
| pipeline-mutation/fused | 6 | 0.714 | 0.834 | 0 |
| pipeline-mutation/scratch-calls | 6 | 1.319 | 1.505 | 1 |
| point/ikea2 vs prior | 15 | 0.996 | 1.256 | 0 |
| sum/ikea2-native | 14 | 0.671 | 1.653 | 2 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.
