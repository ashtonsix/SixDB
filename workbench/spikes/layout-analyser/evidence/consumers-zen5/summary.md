# Native consumer observations

Median elapsed ns per completed operation on the pinned CPU. Seeds and working sets remain separate. Repeated traces are warmed; allocation size does not establish a cache tier.

| Family / rows / seed | Cases | Minimum / median / maximum case median, ns |
| --- | ---: | ---: |
| bucket / 4096 / 712367 | 336 | 19.178 / 32.616 / 227.979 |
| bucket / 4096 / 918273 | 336 | 18.703 / 32.424 / 228.088 |
| bucket / 262144 / 712367 | 336 | 24.937 / 42.757 / 256.015 |
| bucket / 262144 / 918273 | 336 | 24.992 / 42.963 / 256.364 |
| prefix / 4096 / 712367 | 96 | 0.293 / 2.453 / 23.172 |
| prefix / 4096 / 918273 | 96 | 0.294 / 2.454 / 23.193 |
| prefix / 262144 / 712367 | 96 | 0.622 / 3.499 / 27.302 |
| prefix / 262144 / 918273 | 96 | 0.688 / 3.575 / 26.556 |

This coverage summary is not a ranking across different requested operations. Compare matching operations and logical data. Raw controls omit the ordinary API and its effects; raw-format fingerprint arms include layouts that SeriesPack cannot currently admit. See the study source, metadata and per-case rows.
