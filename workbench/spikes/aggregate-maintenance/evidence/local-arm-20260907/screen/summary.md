# Aggregate delta probe: measured cycle costs

Generated from accounting.csv and all recorded benchmark repetitions. Times are median wall time for apply + queries + folds + final drain, divided by mutations; empty-base setup and trace/oracle generation are excluded.

These are resident-memory algorithm measurements on the recorded host. Base updates are logical Value-cell adjustments, not storage writes. No durability, historical-reader, or cache-residence claim is made.

| Case | Eager ns/mutation | Batched / eager | Ancestor runs / eager | Location runs / eager | Location ancestor updates / eager |
| --- | ---: | ---: | ---: | ---: | ---: |
| uniform | 6.3 | 22.97 | 25.88 | 24.58 | 0.289 |
| hot | 8.9 | 4.55 | 5.26 | 4.28 | 0.002 |
| single | 11.1 | 0.74 | 0.77 | 0.79 | 0.001 |
| monotonic | 9.2 | 5.57 | 6.90 | 9.44 | 0.017 |
| root | 4.8 | 30.20 | 31.75 | 31.91 | 0.289 |
| narrow | 6.4 | 22.28 | 24.32 | 23.85 | 0.289 |
| read_heavy | 38.3 | 4.69 | 7.37 | 5.14 | 0.289 |
| one_partition | 6.2 | 23.48 | 49.99 | 31.91 | 0.286 |
| many_partitions | 7.1 | 23.17 | 22.17 | 21.36 | 0.333 |
| deep | 27.9 | 23.81 | 23.20 | 19.40 | 0.385 |
| small_batch | 43.0 | 2.35 | 4.75 | 3.48 | 0.539 |
| large_batch | 4.7 | 35.41 | 39.19 | 35.83 | 0.175 |
| long_buffer | 6.2 | 23.13 | 33.67 | 28.43 | 0.175 |
| large_base | 12.5 | 17.68 | 17.23 | 16.84 | 0.432 |

Ratios below 1 mean less cost than eager in that case. Inspect summary.csv for repetition spread, sorting work, pending payload, and query probes. Different query counts change the work per mutation.
