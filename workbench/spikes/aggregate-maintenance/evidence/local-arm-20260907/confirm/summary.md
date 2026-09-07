# Aggregate delta probe: measured cycle costs

Generated from accounting.csv and all recorded benchmark repetitions. Times are median wall time for apply + queries + folds + final drain, divided by mutations; empty-base setup and trace/oracle generation are excluded.

These are resident-memory algorithm measurements on the recorded host. Base updates are logical Value-cell adjustments, not storage writes. No durability, historical-reader, or cache-residence claim is made.

| Case | Eager ns/mutation | Batched / eager | Ancestor runs / eager | Location runs / eager | Location ancestor updates / eager |
| --- | ---: | ---: | ---: | ---: | ---: |
| uniform | 6.3 | 22.84 | 25.83 | 24.33 | 0.289 |
| hot | 9.3 | 4.35 | 5.05 | 4.10 | 0.002 |
| single | 11.0 | 0.70 | 0.76 | 0.75 | 0.001 |
| read_heavy | 37.0 | 4.88 | 7.50 | 5.38 | 0.289 |
| many_partitions | 6.2 | 23.12 | 22.22 | 20.73 | 0.333 |

Ratios below 1 mean less cost than eager in that case. Inspect summary.csv for repetition spread, sorting work, pending payload, and query probes. Different query counts change the work per mutation.
