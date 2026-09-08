# Row signature measurements

Pinned sequential manual wall-clock timings. Query ns/input row includes full mask output and exact residual checks.
Build ns/input row includes allocation/construction; replace ns/write includes the primary AoS field and metadata.
Work counters are logical accesses, not hardware traffic. Prefix planes are alternate value storage; other stored bytes are auxiliary.

| Case | Method | Phase | ns/op | Min–max | True | Candidates | Groups by plane | Skipped blocks | Stored B/row |
| --- | --- | --- | ---: | --- | ---: | ---: | --- | --- | ---: |
| absent | soa | q0 | 0.499 | 0.495–0.507 | 0 | 0 | 0/0/0/0 | 0/0 | 0.000 |
| absent | soa | q1 | 0.501 | 0.500–0.826 | 0 | 0 | 0/0/0/0 | 0/0 | 0.000 |
| absent | soa | q2 | 0.494 | 0.493–0.498 | 0 | 0 | 0/0/0/0 | 0/0 | 0.000 |
| absent | soa | q3 | 0.495 | 0.493–0.503 | 0 | 0 | 0/0/0/0 | 0/0 | 0.000 |
| absent | tag8 | q0 | 0.573 | 0.560–0.613 | 0 | 4152 | 65536/0/0/0 | 0/0 | 4.000 |
| absent | tag8 | q1 | 0.586 | 0.556–0.613 | 0 | 4117 | 65536/0/0/0 | 0/0 | 4.000 |
| absent | tag8 | q2 | 0.580 | 0.570–0.584 | 0 | 4048 | 65536/0/0/0 | 0/0 | 4.000 |
| absent | tag8 | q3 | 0.563 | 0.558–0.573 | 0 | 4027 | 65536/0/0/0 | 0/0 | 4.000 |
| absent | joint64_planar | q0 | 0.323 | 0.316–0.325 | 0 | 4152 | 14600/0/0/0 | 12734/16384 | 6.000 |
| absent | joint64_planar | q1 | 0.324 | 0.316–0.329 | 0 | 4117 | 14684/0/0/0 | 12713/16384 | 6.000 |
| absent | joint64_planar | q2 | 0.312 | 0.309–0.324 | 0 | 4048 | 14444/0/0/0 | 12773/16384 | 6.000 |
| absent | joint64_planar | q3 | 0.318 | 0.314–0.323 | 0 | 4027 | 14256/0/0/0 | 12820/16384 | 6.000 |
| or | soa | q0 | 0.667 | 0.664–0.674 | 2588 | 2588 | 0/0/0/0 | 0/0 | 0.000 |
| or | tag32 | q0 | 3.760 | 3.320–4.062 | 2588 | 71490 | 65536/0/0/0 | 0/0 | 4.000 |
| or | tag16 | q0 | 4.019 | 3.760–4.124 | 2588 | 71490 | 65536/0/0/0 | 0/0 | 4.000 |
| or | tag8 | q0 | 4.145 | 3.937–4.281 | 2588 | 71490 | 65536/65536/0/0 | 0/0 | 4.000 |
| or | tag8_eager | q0 | 3.996 | 3.828–4.357 | 2588 | 71490 | 65536/65536/0/0 | 0/0 | 4.000 |
| or | joint64_planar | q0 | 4.323 | 3.790–4.776 | 2588 | 71490 | 65536/65536/0/0 | 0/16384 | 6.000 |
| or_broad | soa | q0 | 1.572 | 1.564–1.582 | 83876 | 83876 | 0/0/0/0 | 0/0 | 0.000 |
| or_broad | tag32 | q0 | 3.178 | 3.125–3.234 | 83876 | 137234 | 65536/0/0/0 | 0/0 | 4.000 |
| or_broad | tag16 | q0 | 3.222 | 3.147–3.936 | 83876 | 137234 | 65536/0/0/0 | 0/0 | 4.000 |
| or_broad | tag8 | q0 | 3.436 | 3.354–3.753 | 83876 | 137234 | 65536/65536/0/0 | 0/0 | 4.000 |
| or_broad | tag8_eager | q0 | 3.493 | 3.427–4.328 | 83876 | 137234 | 65536/65536/0/0 | 0/0 | 4.000 |
| or_broad | joint64_planar | q0 | 3.582 | 3.539–3.742 | 83876 | 137234 | 65536/65536/0/0 | 0/16384 | 6.000 |
