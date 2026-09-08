# Factored-filter efficacy

All arms use the same 64-branch exact route. Residual metrics concern non-exact queries. Counts are not timings.

| Dataset | Arm | Potential RE2 calls | Avoided | Simple LIKE evaluations | Position searches | Rich IR evaluations |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| accidents | like64 | 8,772 | 96.654% | 262,144 | 0 | 0 |
| accidents | rich64 | 8,772 | 96.654% | 262,144 | 0 | 8,772 |
| accidents | literal_dag | 8,953 | 96.585% | 355,042 | 0 | 0 |
| accidents | like_dag | 8,772 | 96.654% | 262,144 | 0 | 0 |
| accidents | like_dag_trained | 8,772 | 96.654% | 262,144 | 0 | 0 |
| accidents | like_dag_then_like64 | 8,772 | 96.654% | 270,916 | 0 | 0 |
| accidents | like64_then_like_dag | 8,772 | 96.654% | 270,916 | 0 | 0 |
| accidents | like_dag_then_rich64 | 8,772 | 96.654% | 270,916 | 0 | 8,772 |
| accidents | ordered_dag | 8,772 | 96.654% | 262,144 | 8,772 | 0 |
| accidents | ordered_then_like64 | 8,772 | 96.654% | 270,916 | 8,772 | 0 |
| accidents | ordered_then_rich64 | 8,772 | 96.654% | 270,916 | 8,772 | 8,772 |
| uap | like64 | 2,659,053 | 88.504% | 117,142,458 | 0 | 0 |
| uap | rich64 | 2,565,826 | 88.907% | 117,142,458 | 0 | 2,659,053 |
| uap | literal_dag | 1,654,385 | 92.848% | 35,426,403 | 0 | 0 |
| uap | like_dag | 1,631,817 | 92.945% | 33,200,289 | 0 | 0 |
| uap | like_dag_trained | 1,631,817 | 92.945% | 33,113,068 | 0 | 0 |
| uap | like_dag_then_like64 | 1,484,280 | 93.583% | 39,101,584 | 0 | 0 |
| uap | like64_then_like_dag | 1,484,280 | 93.583% | 128,083,444 | 0 | 0 |
| uap | like_dag_then_rich64 | 1,424,309 | 93.842% | 39,101,584 | 0 | 1,484,280 |
| uap | ordered_dag | 1,502,883 | 93.503% | 33,200,289 | 4,220,879 | 0 |
| uap | ordered_then_like64 | 1,407,416 | 93.915% | 37,239,899 | 4,220,879 | 0 |
| uap | ordered_then_rich64 | 1,347,980 | 94.172% | 37,239,899 | 4,220,879 | 1,407,416 |

The ordered arm pays for its unordered LIKE gate and additional position searches. Position searches use a separate untimed DP reference; these operation columns must not be added as equal-cost units.

`factor_summary.csv` retains every query. `structure.csv` retains representation, ordering, and paid-training counters. `examples.csv` contains selected full plans.
