# Current head-to-head evidence

**Keep the hierarchy; do not claim broad superiority or a universal selected preset.** Cold Loom throughput and narrow logical memory are real strengths. Compact lookup, common warm reads, wide multimaps and some growth/merge histories remain losses. These are different measured configurations, not the combined performance of one final owner.

Every pair below is **ours / Abseil**, in absolute units. Serial latency means an answer-dependent chain with one request in flight. Reciprocal throughput is elapsed time per completed operation with independent work or the stated Ring concurrency. Memory is logical requested storage plus reported owner metadata; it is not per-container RSS, allocation backing or retained peak memory.

Native platforms are Zen5 AVX2, V2 NEON and Granite AVX-512. SVE2/V3 are not qualified by these results. Read all three objectives before interpreting a win.

## Warm single-writer comparison

Corrected 6,144-key singleton histories, medium 14-slot family, doubling/90%. These are warmed workloads, not separately established L1/L2 tiers. Independent reads measure reciprocal throughput; dependent reads measure serial latency. Full build/release and exact-width baseline rows are in [benchmark validation](BENCHMARK-VALIDATION.md#native-corrected-comparison). Earlier allocating-validator lifetime advantages are superseded.

| Host / case | Dependent hit ns | Independent hit ns | Logical B |
| --- | ---: | ---: | ---: |
| Zen5 / K40 set | 6.38 / 6.22 | 3.69 / 2.32 | 38,398 / 73,760 |
| Zen5 / K40 W2 map | 15.29 / 13.53 | 4.46 / 2.71 | 55,788 / 139,288 |
| Zen5 / K40 W2 multimap | 18.52 / 14.13 | 5.93 / 4.85 | 172,226 / 270,344 |
| Zen5 / K64 W64 map | 20.34 / 16.74 | 8.11 / 3.93 | 562,922 / 597,984 |
| Zen5 / K64 W64 multimap | 28.42 / 17.46 | 13.48 / 6.27 | 1,763,218 / 663,512 |
| V2 / K40 set | 8.11 / 7.83 | 4.46 / 2.75 | 38,398 / 73,752 |
| V2 / K40 W2 map | 21.82 / 17.62 | 5.54 / 3.22 | 55,788 / 139,280 |
| V2 / K40 W2 multimap | 26.68 / 20.82 | 6.48 / 5.95 | 172,226 / 270,336 |
| V2 / K64 W64 map | 29.35 / 22.45 | 9.80 / 5.05 | 562,922 / 597,976 |
| V2 / K64 W64 multimap | 40.70 / 24.44 | 17.61 / 6.76 | 1,763,218 / 663,504 |

The narrow logical-memory advantage also survives the exact-key comparison: K40 set 38,398 B versus 49,186/49,178 B (Zen/V2), K40/W2 map 55,788 B versus 65,568/65,560 B. The exact-key competitor can be faster or slower than ordinary Abseil depending on the operation; see the complete paired rows.

## Loom: serial latency and reciprocal throughput

1,572,864-key all-hit cohorts, medium family. Serial is the direct C1 path; throughput is C32 Ring with matched caller-input hints. Wide maps use a located-value stage; other rows use whole-bucket hints. Each Abseil throughput cell is the faster direct/prefetch median **in that same process**. No baseline or memory is borrowed from a different policy. These cohorts do not all exceed each host’s LLC.

| Host / case | Serial C1 ns | Ring C32 ns/query | Logical B |
| --- | ---: | ---: | ---: |
| Zen5 / K40 set | 15.60 / 27.12 | 11.00 / 7.17 | 7,381,908 / 18,874,360 |
| Zen5 / K40/W2 map | 29.68 / 116.08 | 7.87 / 28.08 | 11,803,696 / 35,651,528 |
| Zen5 / K64/W64 map | 257.33 / 175.83 | 17.26 / 41.15 | 142,508,588 / 153,091,704 |
| Zen5 / K40/W2 multimap | 199.44 / 178.52 | 16.74 / 37.92 | 47,479,762 / 72,744,200 |
| Zen5 / K64/W64 multimap | 474.24 / 237.83 | 46.89 / 87.01 | 639,591,656 / 337,546,312 |
| V2 / K40 set | 68.02 / 77.33 | 14.88 / 11.88 | 7,381,908 / 18,874,352 |
| V2 / K40/W2 map | 69.81 / 117.40 | 13.48 / 21.23 | 11,803,696 / 35,651,520 |
| V2 / K64/W64 map | 204.71 / 149.77 | 25.16 / 35.05 | 142,508,588 / 153,091,696 |
| V2 / K40/W2 multimap | 151.54 / 141.74 | 30.46 / 38.70 | 47,479,762 / 72,744,192 |
| V2 / K64/W64 multimap | 339.93 / 203.32 | 81.83 / 66.08 | 639,591,656 / 337,546,304 |
| Granite / K40 set | 54.75 / 58.11 | 13.32 / 10.56 | 7,381,908 / 18,874,360 |
| Granite / K40/W2 map | 109.99 / 168.13 | 10.40 / 24.46 | 11,803,696 / 35,651,528 |
| Granite / K64/W64 map | 317.79 / 195.34 | 25.03 / 39.97 | 142,508,588 / 153,091,704 |
| Granite / K40/W2 multimap | 100.47 / 147.49 | 22.37 / 37.63 | 47,479,762 / 72,744,200 |
| Granite / K64/W64 multimap | 498.37 / 267.97 | 101.83 / 78.85 | 639,591,656 / 337,546,312 |

The additional **Granite above-LLC K64/W64 map** has 12,582,912 keys, with a 1,040,187,392 B primary plus 94,371,840 B secondary allocation; primary alone exceeds the 480 MiB LLC. Its matched located-value process gives:

| Queries | Serial C1 ns | Ring C32 ns/query | Logical B |
| --- | ---: | ---: | ---: |
| Hits | 365.92 / 238.43 | 40.53 / 49.41 | 1,137,952,410 / 1,224,736,376 |
| Mixed | 326.10 / 278.25 | 44.53 / 51.35 | 1,137,952,410 / 1,224,736,376 |

These are a throughput/memory trade-off with a substantial serial loss. [Read-window evidence](READ-WINDOWS.md) includes all controls, raw data, physical receipts and the Granite timeout selection: only complete matched groups are retained. No per-request PMU evidence proves every access is served by DRAM.

## Actual Worker updates

Two overlapping owners, 1,024 keys, 128 updates/key. Cost includes intake and coordinated growth, in **reciprocal ns/update**, not serial latency. Narrow/set/MM rows use the incumbent geometry; wide maps use the 14-slot control. Each baseline is the matched process. Logical bytes cover both active owners, with runtime/queue/inactive state and Pool/backing reported separately in the [full format comparison](worker-epochs.md#solver-sized-formats-in-the-complete-worker-caller).

| Host / case | Update ns | Active logical B |
| --- | ---: | ---: |
| Zen5 / K40 set | 6.98 / 6.66 | 21,072 / 36,944 |
| Zen5 / K40/W2 map | 6.86 / 6.66 | 29,264 / 69,696 |
| Zen5 / K64/W64 map | 9.82 / 8.05 | 284,720 / 298,960 |
| Zen5 / K40/W2 multimap | 12.23 / 7.31 | 594,632 / 520,032 |
| Zen5 / K64/W64 multimap | 40.88 / 18.22 | 14,503,158 / 12,611,520 |
| V2 / K40 set | 13.83 / 12.65 | 21,072 / 36,928 |
| V2 / K40/W2 map | 14.56 / 13.05 | 29,264 / 69,680 |
| V2 / K64/W64 map | 19.92 / 15.02 | 284,720 / 298,944 |
| V2 / K40/W2 multimap | 24.03 / 16.53 | 595,170 / 519,952 |
| V2 / K64/W64 multimap | 63.39 / 25.57 | 14,416,784 / 12,625,840 |
| Granite / K40 set | 14.52 / 13.33 | 21,072 / 36,944 |
| Granite / K40/W2 map | 13.52 / 12.90 | 29,264 / 69,696 |
| Granite / K64/W64 map | 24.64 / 24.32 | 284,720 / 298,960 |
| Granite / K40/W2 multimap | 22.09 / 15.02 | 596,109 / 519,008 |
| Granite / K64/W64 multimap | 67.02 / 34.42 | 14,502,927 / 12,642,240 |

## Actual Worker folding

768 keys/source, 32 updates/key, **disjoint sources** folded into the existing destination. Sets/maps transfer one reduced record/key; multimaps transfer all 32 values/key. Cost is ns/transferred item, including serialized bounded handoff and destination growth. Memory describes the final destination. Full overlap, partial overlap, tiny tables, build/retirement and coexistence are retained in [Worker folding](worker-fold.md#completed-evidence). This is reciprocal transfer cost, not lookup latency.

| Host / case | Fold ns/item | Destination logical B |
| --- | ---: | ---: |
| Zen5 / K40 set | 43.22 / 25.92 | 20,776 / 18,472 |
| Zen5 / K40/W2 map | 27.14 / 27.43 | 14,862 / 34,848 |
| Zen5 / K64/W64 map | 77.66 / 43.05 | 143,698 / 149,480 |
| Zen5 / K40/W2 multimap | 24.95 / 15.56 | 251,120 / 165,904 |
| Zen5 / K64/W64 multimap | 65.57 / 37.02 | 5,944,690 / 3,311,584 |
| V2 / K40 set | 94.45 / 56.78 | 20,776 / 18,464 |
| V2 / K40/W2 map | 61.35 / 63.39 | 14,862 / 34,840 |
| V2 / K64/W64 map | 136.82 / 126.01 | 143,698 / 149,472 |
| V2 / K40/W2 multimap | 55.80 / 42.17 | 251,120 / 165,896 |
| V2 / K64/W64 multimap | 126.18 / 80.97 | 5,944,690 / 3,311,576 |
| Granite / K40 set | 82.51 / 56.38 | 20,776 / 18,472 |
| Granite / K40/W2 map | 60.86 / 56.78 | 14,862 / 34,848 |
| Granite / K64/W64 map | 148.87 / 103.76 | 143,698 / 149,480 |
| Granite / K40/W2 multimap | 57.44 / 43.85 | 251,120 / 165,904 |
| Granite / K64/W64 multimap | 127.05 / 88.06 | 5,944,690 / 3,311,584 |

All three native fold jobs completed their 102 timed processes and 34 checks, then failed final reporting because three CSV labels were omitted. The strict recovery report identifies that exact schema and reproduces summaries from unchanged retained measurements. Source/output provenance and the original failures remain preserved.

## How to use this evidence

The architecture is promising, but the multimap and lifetime gaps are not minor hot-latency costs. [Graduation assessment](GRADUATION.md) distinguishes resolved choices, implemented boundaries and remaining work. [Historical results](RESULTS-HISTORICAL.md) preserve attribution without serving as the current scorecard. Rebuild this page with `python3 workbench/spikes/hashtable/current-scorecard.py --output workbench/spikes/hashtable/RESULTS.md`; native fold reports have their own strict qualification/recovery recipes.
