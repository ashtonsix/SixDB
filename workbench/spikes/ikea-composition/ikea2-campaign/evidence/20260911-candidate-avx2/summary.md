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
| decode/ikea2 vs ikea | 61 | 1.134 | 1.441 | 1 |
| decode/ikea2 vs prior | 15 | 1.010 | 1.090 | 0 |
| get16-ends/ikea2-range | 15 | 1.139 | 1.289 | 0 |
| get16-ends/ikea2-region | 15 | 0.997 | 1.523 | 1 |
| get16/ikea2-range | 15 | 1.039 | 1.130 | 0 |
| get16/ikea2-region | 15 | 0.981 | 1.228 | 0 |
| ordinary/bound | 12 | 1.007 | 3.342 | 4 |
| overwrite-all/ikea2 | 76 | 0.845 | 1.334 | 0 |
| overwrite/ikea2-coverage | 9 | 0.784 | 1.472 | 1 |
| overwrite/ikea2-raw | 9 | 0.786 | 1.322 | 0 |
| overwrite/ikea2-sum-coverage | 18 | 0.498 | 1.059 | 0 |
| pipeline-mutation/cps | 6 | 1.275 | 1.609 | 2 |
| pipeline-mutation/fused | 6 | 0.634 | 0.818 | 0 |
| pipeline-mutation/scratch-calls | 6 | 1.390 | 1.683 | 3 |
| pipeline-packet/cps | 15 | 1.117 | 1.290 | 0 |
| pipeline-packet/fused | 15 | 1.049 | 1.358 | 0 |
| placed/mutation-bound | 18 | 0.895 | 1.178 | 0 |
| placed/sum-native | 18 | 0.996 | 1.202 | 0 |
| point/ikea2 vs prior | 15 | 0.997 | 1.020 | 0 |
| sum/ikea2-native | 14 | 0.495 | 1.293 | 0 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.

## Best measured grain per recipe

CPS and inline may choose different grains from the measured set. This is selection
from these samples, not a held-out tuning result; see `pipeline-grains.json`.

| Recipe | Inline grain | CPS grain | CPS / inline |
| --- | --- | --- | ---: |
| source0-map0-filter0-sink0 | n64 | n32 | 1.043 |
| source0-map1-filter1-sink1 | n64 | n64 | 1.290 |
| source1-map0-filter0-sink1 | n64 | n64 | 1.099 |
| source1-map0-filter0-sink1-empty | n64 | n64 | 1.215 |
| source1-map2-filter2-sink0 | n64 | n64 | 1.252 |
