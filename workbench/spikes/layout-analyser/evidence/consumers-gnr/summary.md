# Native consumer observations

Median elapsed ns per completed operation on the pinned CPU. Seeds and working sets remain separate. Repeated traces are warmed; allocation size does not establish a cache tier.

| Family / rows / seed | Cases | Minimum / median / maximum case median, ns |
| --- | ---: | ---: |
| bucket / 4096 / 712367 | 336 | 22.915 / 40.443 / 288.160 |
| bucket / 4096 / 918273 | 336 | 22.774 / 40.362 / 287.102 |
| bucket / 262144 / 712367 | 336 | 35.814 / 69.281 / 330.607 |
| bucket / 262144 / 918273 | 336 | 35.723 / 69.809 / 314.616 |
| prefix / 4096 / 712367 | 96 | 0.404 / 3.097 / 17.370 |
| prefix / 4096 / 918273 | 96 | 0.403 / 3.107 / 17.445 |
| prefix / 262144 / 712367 | 96 | 2.572 / 7.521 / 61.578 |
| prefix / 262144 / 918273 | 96 | 2.461 / 8.864 / 60.936 |

This coverage summary is not a ranking across different requested operations. Compare matching operations and logical data. Raw controls omit the ordinary API and its effects; raw-format fingerprint arms include layouts that SeriesPack cannot currently admit. See the study source, metadata and per-case rows.
