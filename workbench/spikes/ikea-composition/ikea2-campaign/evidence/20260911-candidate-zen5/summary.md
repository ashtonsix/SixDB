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
| decode/ikea2 vs ikea | 61 | 1.016 | 1.266 | 0 |
| decode/ikea2 vs prior | 15 | 1.028 | 1.287 | 0 |
| get16-ends/ikea2-range | 15 | 1.155 | 1.324 | 0 |
| get16-ends/ikea2-region | 15 | 0.918 | 0.993 | 0 |
| get16/ikea2-range | 15 | 1.017 | 1.058 | 0 |
| get16/ikea2-region | 15 | 0.998 | 1.034 | 0 |
| ordinary/bound | 12 | 1.017 | 2.855 | 4 |
| overwrite-all/ikea2 | 76 | 1.021 | 1.390 | 0 |
| overwrite/ikea2-coverage | 9 | 1.079 | 1.509 | 1 |
| overwrite/ikea2-raw | 9 | 1.052 | 1.403 | 1 |
| overwrite/ikea2-sum-coverage | 18 | 0.303 | 1.181 | 0 |
| pipeline-mutation/cps | 6 | 1.550 | 3.594 | 3 |
| pipeline-mutation/fused | 6 | 0.683 | 0.902 | 0 |
| pipeline-mutation/scratch-calls | 6 | 1.719 | 2.524 | 4 |
| pipeline-packet/cps | 15 | 1.214 | 2.038 | 3 |
| pipeline-packet/fused | 15 | 1.004 | 1.273 | 0 |
| placed/mutation-bound | 18 | 1.033 | 1.503 | 2 |
| placed/sum-native | 18 | 0.890 | 0.957 | 0 |
| point/ikea2 vs prior | 15 | 0.996 | 1.023 | 0 |
| sum/ikea2-native | 14 | 0.562 | 1.182 | 0 |

See `comparisons.csv` for every case and its actual control, and `samples.csv` for repetitions.
`provenance.json`, `host.json`, checks and the recoverable artifact identify the measured source and conditions.

## Best measured grain per recipe

CPS and inline may choose different grains from the measured set. This is selection
from these samples, not a held-out tuning result; see `pipeline-grains.json`.

| Recipe | Inline grain | CPS grain | CPS / inline |
| --- | --- | --- | ---: |
| source0-map0-filter0-sink0 | n64 | n32 | 1.145 |
| source0-map1-filter1-sink1 | n64 | n64 | 1.209 |
| source1-map0-filter0-sink1 | n64 | n64 | 1.233 |
| source1-map0-filter0-sink1-empty | n64 | n64 | 1.388 |
| source1-map2-filter2-sink0 | n64 | n64 | 1.216 |
