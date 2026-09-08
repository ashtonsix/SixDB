# Raw-string efficacy

RE2 Boolean search oracle. Counts of avoided calls and candidate bytes are not speedups.

| Dataset / group | Supported | Exact | Signature | Fallback | LIKE calls avoided | Rich IR calls avoided | Survivor bytes outside bounds |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| accidents / all | 4 | 0 | 4 | 0 | 96.654% | 96.654% | 0.009% |
| uap / all | 1270 | 199 | 906 | 165 | 77.284% | 78.029% | 18.530% |
| uap / device_parsers | 633 | 82 | 412 | 139 | 61.012% | 62.181% | 20.412% |
| uap / os_parsers | 204 | 52 | 146 | 6 | 94.889% | 95.183% | 12.026% |
| uap / user_agent_parsers | 433 | 65 | 348 | 20 | 92.777% | 93.115% | 5.109% |

## Switching, 256-row containers

Non-exact patterns only; source order. Four-container warmup. Every row contributes a verified result.

| Dataset | Arm | Policy | Filtered pairs | RE2 calls | Missed rejections | Queries switching |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| accidents | like | always | 262,144 | 8,772 | 0 | 0 |
| accidents | like | direct | 0 | 262,144 | 253,372 | 0 |
| accidents | like | periodic20 | 262,144 | 8,772 | 0 | 0 |
| accidents | like | prefix05 | 262,144 | 8,772 | 0 | 0 |
| accidents | like | prefix20 | 262,144 | 8,772 | 0 | 0 |
| accidents | like | prefix50 | 262,144 | 8,772 | 0 | 0 |
| accidents | rich | always | 262,144 | 8,772 | 0 | 0 |
| accidents | rich | direct | 0 | 262,144 | 253,372 | 0 |
| accidents | rich | periodic20 | 262,144 | 8,772 | 0 | 0 |
| accidents | rich | prefix05 | 262,144 | 8,772 | 0 | 0 |
| accidents | rich | prefix20 | 262,144 | 8,772 | 0 | 0 |
| accidents | rich | prefix50 | 262,144 | 8,772 | 0 | 0 |
| uap | like | always | 16,500,978 | 5,254,367 | 0 | 0 |
| uap | like | direct | 0 | 19,506,123 | 14,251,756 | 0 |
| uap | like | periodic20 | 14,469,059 | 5,956,879 | 702,512 | 198 |
| uap | like | prefix05 | 16,071,253 | 5,256,127 | 1,760 | 25 |
| uap | like | prefix20 | 15,847,796 | 5,285,200 | 30,833 | 38 |
| uap | like | prefix50 | 15,435,260 | 5,395,559 | 141,192 | 62 |
| uap | rich | always | 16,500,978 | 5,082,073 | 0 | 0 |
| uap | rich | direct | 0 | 19,506,123 | 14,424,050 | 0 |
| uap | rich | periodic20 | 14,678,146 | 5,723,334 | 641,261 | 181 |
| uap | rich | prefix05 | 16,140,009 | 5,082,073 | 0 | 21 |
| uap | rich | prefix20 | 15,985,308 | 5,099,179 | 17,106 | 30 |
| uap | rich | prefix50 | 15,589,961 | 5,202,339 | 120,266 | 53 |

Full per-query counters: `pattern_summary.csv`. All order, threshold, and container-size variants: `policy_summary.csv`.
Raw outcome flags and per-container/per-query policy traces are in the full run bundle.
