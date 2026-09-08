# Raw-string efficacy

RE2 Boolean search oracle. Counts of avoided calls and candidate bytes are not speedups.

| Dataset / group | Supported | Exact | Signature | Fallback | LIKE calls avoided | Rich IR calls avoided | Survivor bytes outside bounds |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| accidents / all | 4 | 0 | 4 | 0 | 96.654% | 96.654% | 0.009% |
| uap / all | 1270 | 231 | 963 | 76 | 88.504% | 88.907% | 9.720% |
| uap / device_parsers | 633 | 114 | 457 | 62 | 79.990% | 80.619% | 9.273% |
| uap / os_parsers | 204 | 52 | 149 | 3 | 97.716% | 97.977% | 17.379% |
| uap / user_agent_parsers | 433 | 65 | 357 | 11 | 96.611% | 96.751% | 11.074% |

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
| uap | like | always | 17,539,119 | 2,659,053 | 0 | 0 |
| uap | like | direct | 0 | 18,923,307 | 16,264,254 | 0 |
| uap | like | periodic20 | 16,381,229 | 2,892,900 | 233,847 | 93 |
| uap | like | prefix05 | 17,126,583 | 2,659,053 | 0 | 24 |
| uap | like | prefix20 | 16,937,504 | 2,675,946 | 16,893 | 35 |
| uap | like | prefix50 | 16,490,590 | 2,797,311 | 138,258 | 61 |
| uap | rich | always | 17,539,119 | 2,565,826 | 0 | 0 |
| uap | rich | direct | 0 | 18,923,307 | 16,357,481 | 0 |
| uap | rich | periodic20 | 16,477,343 | 2,784,394 | 218,568 | 87 |
| uap | rich | prefix05 | 17,195,339 | 2,565,826 | 0 | 20 |
| uap | rich | prefix20 | 17,023,449 | 2,577,065 | 11,239 | 30 |
| uap | rich | prefix50 | 16,610,913 | 2,685,301 | 119,475 | 54 |

Full per-query counters: `pattern_summary.csv`. All order, threshold, and container-size variants: `policy_summary.csv`.
Raw outcome flags and per-container/per-query policy traces are in the full run bundle.
