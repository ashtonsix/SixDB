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
| decode/ikea2 vs ikea | 61 | 1.012 | 1.307 | 0 |
| decode/ikea2 vs prior | 15 | 1.017 | 1.608 | 2 |
| get16-ends/ikea2-range | 15 | 1.046 | 1.524 | 1 |
| get16-ends/ikea2-region | 15 | 0.953 | 1.094 | 0 |
| get16/ikea2-range | 15 | 0.991 | 1.213 | 0 |
| get16/ikea2-region | 15 | 0.962 | 1.024 | 0 |
| ordinary/bound | 12 | 1.003 | 1.658 | 1 |
| overwrite-all/ikea2 | 76 | 1.159 | 1.509 | 4 |
| overwrite/ikea2-coverage | 9 | 1.042 | 1.591 | 1 |
| overwrite/ikea2-raw | 9 | 1.041 | 1.382 | 0 |
| overwrite/ikea2-sum-coverage | 18 | 0.578 | 0.959 | 0 |
| pipeline-mutation/cps | 6 | 1.195 | 1.323 | 0 |
| pipeline-mutation/fused | 6 | 0.634 | 0.788 | 0 |
| pipeline-mutation/scratch-calls | 6 | 1.312 | 1.469 | 1 |
| pipeline-packet/cps | 10 | 1.373 | 1.564 | 4 |
| pipeline-packet/fused | 10 | 1.038 | 1.299 | 0 |
| placed/mutation-bound | 18 | 0.879 | 1.038 | 0 |
| placed/sum-native | 18 | 0.955 | 1.443 | 1 |
| point/ikea2 vs prior | 15 | 0.996 | 1.196 | 0 |
| sum/ikea2-native | 14 | 0.555 | 1.034 | 0 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.

## Best measured grain per recipe

CPS and inline may choose different grains from the measured set. This is selection
from these samples, not a held-out tuning result; see `pipeline-grains.json`.

| Recipe | Inline grain | CPS grain | CPS / inline |
| --- | --- | --- | ---: |
| source0-map0-filter0-sink0 | n32 | n32 | 1.564 |
| source0-map1-filter1-sink1 | n32 | n32 | 1.361 |
| source1-map0-filter0-sink1 | n32 | n32 | 1.287 |
| source1-map0-filter0-sink1-empty | n32 | n32 | 1.431 |
| source1-map2-filter2-sink0 | n32 | n32 | 1.294 |
