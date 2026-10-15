# Historical scorecards — superseded entry point

Retained for attribution and recovery. Start with [current results](RESULTS.md).
Do not use older phase-lifetime rankings affected by the
[validation-allocation correction](BENCHMARK-VALIDATION.md). This file combines
several dated configurations; it is not the performance of one selected owner.

# Head-to-head status

> Some phase-based lifetime results below require corrected reruns: [validation allocation perturbed malloc state](BENCHMARK-VALIDATION.md). This does not invalidate separate cold Loom or allocation-free repeated-lifetime studies.

15 October update: the [generic staged-reader comparison](STAGED.md#first-native-result-neoverse-v2)
now carries sets, opaque maps and mixed bags through direct/manual/Loom execution,
with a paired word-reader control. V2 and Zen 5 preserve cold map and multimap
throughput advantages; hot/serial losses remain. Matched V2 and Zen 5 caller-input
hint controls also improve C32 mixed throughput across all three kinds, without
changing the table or adding Ring visits. Its Abseil arms receive the same hint.
Its graph differs from the 14 October cohort below, which remains intact for
comparison. [Coupled capacity](COUPLED.md) separately investigates the wide-record
overflow/memory cliff rather than optimizing the crowded path in isolation.

**The spike has not achieved broad superiority over Abseil.** It has useful
cold-throughput, density and mutation results, but common hits, small-table
lookup and wide-bag growth/lifetime remain behind. There is no single
integrated, selected configuration demonstrating all of the strengths below.
Each comparison identifies its measured configuration; results from different
binaries must not be combined into one current default.

**Every paired cell is two absolute measurements: ours / Abseil, not a ratio.**
Timings are medians rounded to two decimal places, taken directly from the
matching contender rows. Memory is in bytes. K is key bits; W is value bytes.
Native targets are Zen5 AVX2, Granite Rapids AVX-512 and Neoverse V2 NEON.
These comparisons do not qualify SVE2 or V3. Historical experiment notes after
the current scorecard retain their original reporting conventions.

## The three objectives, kept separate

**Serial latency** is elapsed ns/query along an answer-dependent request chain,
with one request in flight. It includes the measured consumer and scheduling
work; it is not a bare load latency or a p99 response-time measurement.
**Reciprocal throughput** is elapsed time divided by completed requests when
requests can overlap. Independent loops and explicit C32 Ring execution are
different concurrency regimes and are labeled separately. A C32 result of
7 ns/query does not mean a request has seven-nanosecond latency.
**Memory** retains requested bytes, metadata, accessible backing, resident
pages and retained-version coexistence separately. Logical memory means
requested storage plus owner metadata. Mutation lifecycle is a fourth useful
accounting view; it substitutes for none of the three objectives.

## Loom: serial latency and reciprocal throughput

K40 word maps, fuller layout. Abseil is the faster median of its ordinary and
matched-prefetch arms at that concurrency. Every pair is **ours / Abseil**,
in ns/completed query. C1 is one serial chain; C32 overlaps 32 chains.
Mixed graphs contain about one-third misses.

| Host / table | Queries | Serial latency C1, ns | Reciprocal throughput C32, ns |
| --- | ---: | ---: | ---: |
| Zen5, 3.6M keys | Hits | 113.13 / 108.05 | 7.15 / 17.81 |
| Zen5, 3.6M keys | Mixed | 99.26 / 79.52 | 8.70 / 19.96 |
| V2, 3.6M keys | Hits | 137.78 / 136.32 | 16.09 / 19.32 |
| V2, 3.6M keys | Mixed | 118.74 / 96.52 | 15.48 / 24.18 |
| Granite, 3.6M keys, within LLC | Hits | 55.31 / 55.44 | 7.63 / 10.20 |
| Granite, 3.6M keys, within LLC | Mixed | 48.37 / 53.98 | 9.99 / 15.89 |
| Granite, 57.6M keys, above LLC | Hits | 205.04 / 217.31 | 22.03 / 31.04 |
| Granite, 57.6M keys, above LLC | Mixed | 175.22 / 181.92 | 22.16 / 34.79 |

The 3.6M-key panels have seven repetitions; the above-LLC Granite panel has one.
The smaller table exceeds the reported V2/Zen5 LLC but fits Granite's LLC.
The larger Granite table exceeds its reported 480 MiB LLC. No workload PMU
miss measurements establish every request's service level. Hints and sufficient
concurrency matter; compact C8 loses in that large Granite run.

Memory for these same fuller hit panels, **ours / Abseil**, in bytes:

| Host / table | Requested storage, B | Owned capacity, B | Owner metadata, B |
| --- | ---: | ---: | ---: |
| Zen5, 3.6M keys | 71,305,028 / 71,303,096 | 100,665,344 / unmeasured | 400 / unreported |
| V2, 3.6M keys | 71,305,028 / 71,303,088 | 100,665,344 / unmeasured | 400 / unreported |
| Granite, 3.6M keys, within LLC | 71,305,028 / 71,303,096 | 100,665,344 / unmeasured | 400 / unreported |
| Granite, 57.6M keys, above LLC | 1,140,858,128 / 1,140,850,616 | 1,610,620,928 / unmeasured | 568 / unreported |

C32 additionally uses 2,368 Ring bytes and 1,792 job bytes. Configuration and
close are reported separately from query timing. Abseil backing/metadata fields
absent from this receipt remain unmeasured/unreported, not zero. Orbital reuse
can retain larger backing than requested; the large Granite receipt confirms
some excess backing is resident. There is no demonstrated per-container RSS win.
The separate compact above-LLC hit panel requests 1,075,080,216 / 1,140,850,616 bytes
(ours / Abseil), with different timing trade-offs. Other dense layouts' memory
savings cannot be attached to the fuller Ring timings.

Sources: [Zen5 Ring rows](evidence/ring-zen5-avx2/large.csv),
[V2 Ring rows](evidence/ring-neon/large.csv),
[Granite Ring rows](evidence/ring-granite-avx512/large.csv), and
[above-LLC Granite report](evidence/dram-granite-avx512/dram-summary.txt).
The detailed earlier sections retain residency, ownership and codegen limits.

## Single-writer sets and maps

Four fixed configurations use the same 6,144-key histories and the original
`fold` selector in the qualified selector-mix cohort. Each geometry is held
fixed across hosts and operations. The K16 map uses A9/U64, K40 set A8/U128,
K40 map A9/U128 and K64 map A10/U192. A is log2(primary bucket count); U is
bucket bytes. The cohort includes x86 scan32 and the full-count mask control.
These are informative configurations, not selected production presets.

**Read timings, ns/query; ours / ordinary Abseil.** Independent hit/miss rows
measure reciprocal throughput; dependent rows measure serial latency.

| Case / operation | Zen5: ours / Abseil | Granite: ours / Abseil | V2: ours / Abseil |
| --- | ---: | ---: | ---: |
| K16/W2 map, independent hit | 1.74 / 1.63 | 2.09 / 2.02 | 3.80 / 2.92 |
| K16/W2 map, independent miss | 1.58 / 1.62 | 2.02 / 4.18 | 3.49 / 6.51 |
| K16/W2 map, dependent read | 9.59 / 8.44 | 12.43 / 11.69 | 15.53 / 13.37 |
| K40 set, independent hit | 2.60 / 1.64 | 3.55 / 2.57 | 4.60 / 2.86 |
| K40 set, independent miss | 2.38 / 2.24 | 3.84 / 6.80 | 4.70 / 9.43 |
| K40/W2 map, independent hit | 2.98 / 2.08 | 3.71 / 3.20 | 4.91 / 3.55 |
| K40/W2 map, independent miss | 2.34 / 2.18 | 3.24 / 4.66 | 3.67 / 9.03 |
| K40/W2 map, dependent read | 10.93 / 9.53 | 14.65 / 13.59 | 15.33 / 13.85 |
| K64/W16 map, independent hit | 3.72 / 2.03 | 4.71 / 2.45 | 6.84 / 3.16 |
| K64/W16 map, independent miss | 2.84 / 1.82 | 4.33 / 4.44 | 4.61 / 6.08 |
| K64/W16 map, dependent read | 13.34 / 10.00 | 18.70 / 14.17 | 20.59 / 14.44 |

**Mutation timings, ns/operation; ours / ordinary Abseil.** Lifecycle is
normalized per initial key and sums construct/build/update/erase/reuse/release;
it excludes queries, verification and Pool setup/drain. Replacement and erase
are reciprocal costs of independent mutation loops, not serial read latency.

| Case / operation | Zen5: ours / Abseil | Granite: ours / Abseil | V2: ours / Abseil |
| --- | ---: | ---: | ---: |
| K16/W2 map, replacement | 2.46 / 2.05 | 2.78 / 2.69 | 5.53 / 3.84 |
| K16/W2 map, erase | 5.12 / 8.82 | 6.23 / 10.37 | 7.79 / 14.04 |
| K16/W2 map, mutation lifecycle | 12.61 / 16.18 | 17.34 / 19.69 | 24.28 / 27.02 |
| K40 set, existing-key set | 3.35 / 2.73 | 4.04 / 3.33 | 7.14 / 4.51 |
| K40 set, erase | 5.71 / 11.08 | 6.79 / 12.79 | 10.04 / 13.88 |
| K40 set, mutation lifecycle | 14.98 / 36.66 | 20.61 / 43.87 | 30.03 / 50.97 |
| K40/W2 map, replacement | 3.61 / 3.13 | 4.46 / 4.00 | 6.35 / 5.30 |
| K40/W2 map, erase | 6.68 / 10.77 | 7.73 / 12.28 | 9.46 / 12.91 |
| K40/W2 map, mutation lifecycle | 17.17 / 36.51 | 24.05 / 43.35 | 27.88 / 50.22 |
| K64/W16 map, replacement | 4.60 / 2.61 | 6.31 / 4.20 | 9.82 / 6.30 |
| K64/W16 map, erase | 9.48 / 9.61 | 10.96 / 11.12 | 13.60 / 12.66 |
| K64/W16 map, mutation lifecycle | 28.56 / 32.69 | 41.06 / 41.51 | 47.71 / 51.45 |

**Initial logical memory, bytes; ours / ordinary Abseil.** These are requested
storage plus owner metadata, not RSS or retained-version peaks.

| Case | Zen5: ours / Abseil | Granite: ours / Abseil | V2: ours / Abseil |
| --- | ---: | ---: | ---: |
| K16/W2 map | 35,112 / 40,976 | 35,112 / 40,976 | 35,112 / 40,968 |
| K40 set | 35,112 / 73,720 | 35,112 / 73,720 | 35,112 / 73,712 |
| K40/W2 map | 69,928 / 139,208 | 69,928 / 139,208 | 69,928 / 139,200 |
| K64/W16 map | 210,170 / 204,696 | 210,170 / 204,696 | 210,170 / 204,688 |

**Exact-width competitor check, independent-hit ns/query; ours / exact-key
Abseil.** These use the same candidate rows as above, with a different baseline.

| Case / operation | Zen5: ours / exact | Granite: ours / exact | V2: ours / exact |
| --- | ---: | ---: | ---: |
| K40 set, independent hit | 2.60 / 2.33 | 3.55 / 3.17 | 4.60 / 3.62 |
| K40/W2 map, independent hit | 2.98 / 2.17 | 3.71 / 3.23 | 4.91 / 3.70 |

These are finite warmed histories, not separately established L1/L2/LLC tiers.
Set ordered traversal is not a qualified strict-serial latency measurement.
Sources: [Zen5](evidence/selector-mix-zen5/summary.csv),
[Granite](evidence/selector-mix-granite/summary.csv),
[V2](evidence/selector-mix-neon/summary.csv), and
[qualification/disposition](selector-mix.md). Only `fold` rows are used here;
the multiply-add alternative was rejected as a general default.

## Multimaps

All rows have K40 and 6,144 distinct keys. The mixed history starts with
approximately 75% singletons, 15% pairs, 6% four-value, 3% sixteen-value and
1% sixty-four-value bags, then appends one value per key and erases half the
keys. Abseil uses typed InlinedVector bags with actual inline capacity accounted
for. W2/8/16 use B16/U192 and 256-link blocks from the coupled policy cohort.
W32/64 use B64/U320 and 256-link blocks from the combined external/first-spill
patch cohort. B is inline payload bytes. These are different measured policies.

**Read timings, ns/query; ours / ordinary Abseil.** Every returned value is
consumed. Independent rows measure reciprocal throughput; dependent rows measure
serial latency. These mixed histories have the initial fanout distribution above.

| Operation | Zen5: ours / Abseil | Granite: ours / Abseil | V2: ours / Abseil |
| --- | ---: | ---: | ---: |
| W2, independent hit | 5.26 / 3.80 | 7.90 / 8.18 | 7.53 / 7.31 |
| W2, independent miss | 2.71 / 1.58 | 4.12 / 4.13 | 3.85 / 6.78 |
| W2, dependent read | 14.81 / 10.77 | 20.76 / 16.20 | 23.74 / 19.30 |
| W8, independent hit | 5.87 / 4.24 | 10.08 / 8.51 | 8.72 / 7.81 |
| W8, independent miss | 3.14 / 1.55 | 4.48 / 3.25 | 3.70 / 7.66 |
| W8, dependent read | 14.59 / 11.48 | 21.19 / 16.93 | 24.35 / 18.93 |
| W16, independent hit | 6.74 / 4.47 | 11.11 / 8.18 | 10.64 / 8.23 |
| W16, independent miss | 3.33 / 1.63 | 4.46 / 3.94 | 3.63 / 7.97 |
| W16, dependent read | 16.46 / 12.25 | 23.50 / 18.80 | 26.85 / 19.71 |
| W32, independent hit | 8.70 / 5.99 | 11.40 / 10.64 | 12.39 / 9.56 |
| W32, independent miss | 3.50 / 2.37 | 7.70 / 6.27 | 5.11 / 10.43 |
| W32, dependent read | 17.79 / 13.78 | 26.54 / 20.81 | 29.62 / 21.90 |
| W64, independent hit | 10.54 / 7.72 | 16.66 / 12.01 | 17.80 / 12.68 |
| W64, independent miss | 3.38 / 2.34 | 6.70 / 5.90 | 4.70 / 9.56 |
| W64, dependent read | 21.64 / 17.13 | 32.53 / 24.38 | 33.85 / 25.49 |

**Mutation timings, ns/append or ns/initial key for lifecycle; ours / ordinary
Abseil.** W2/8/16 lifecycle sums construct/build/update/erase/release. W32/64
also include reuse. Neither sum includes reads, verification or Pool setup/drain;
cross-width lifecycle rows are therefore different histories.

| Operation | Zen5: ours / Abseil | Granite: ours / Abseil | V2: ours / Abseil |
| --- | ---: | ---: | ---: |
| W2, mixed append | 10.47 / 6.60 | 13.66 / 8.08 | 16.24 / 12.05 |
| W2, mixed mutation lifecycle | 67.16 / 59.11 | 91.78 / 75.15 | 117.82 / 90.62 |
| W8, mixed append | 18.18 / 35.93 | 22.72 / 51.27 | 28.77 / 62.32 |
| W8, mixed mutation lifecycle | 96.64 / 74.11 | 122.74 / 102.35 | 166.03 / 119.05 |
| W16, mixed append | 65.52 / 50.95 | 102.09 / 69.46 | 142.37 / 94.67 |
| W16, mixed mutation lifecycle | 150.67 / 98.25 | 227.95 / 137.28 | 312.20 / 182.55 |
| W32, mixed append | 21.71 / 31.51 | 29.71 / 38.70 | 44.06 / 72.31 |
| W32, mixed mutation lifecycle | 132.02 / 97.95 | 197.94 / 129.82 | 267.85 / 250.91 |
| W64, mixed append | 71.03 / 36.04 | 109.49 / 50.40 | 176.71 / 111.21 |
| W64, mixed mutation lifecycle | 216.36 / 130.21 | 331.55 / 169.13 | 514.38 / 386.11 |
| W32, singleton-to-pair, inline | 7.72 / 26.25 | 10.96 / 45.95 | 21.72 / 91.64 |
| W32, pair-to-triple, spill | 80.19 / 52.59 | 110.08 / 91.13 | 192.06 / 153.00 |
| W64, singleton-to-pair, spill | 94.28 / 22.80 | 148.38 / 34.09 | 267.05 / 91.88 |
| W64, append to eight-value bag | 34.86 / 81.86 | 32.92 / 111.22 | 54.03 / 237.05 |

**Initial mixed-history logical memory, bytes; ours / ordinary Abseil.**

| Value width | Zen5: ours / Abseil | Granite: ours / Abseil | V2: ours / Abseil |
| --- | ---: | ---: | ---: |
| W2 | 246,482 / 285,192 | 246,482 / 285,192 | 246,482 / 285,184 |
| W8 | 390,172 / 341,256 | 390,172 / 341,256 | 390,172 / 341,248 |
| W16 | 623,906 / 440,840 | 623,906 / 440,840 | 623,906 / 440,832 |
| W32 | 1,410,956 / 742,472 | 1,410,956 / 742,472 | 1,410,956 / 742,464 |
| W64 | 2,329,738 / 1,345,736 | 2,329,738 / 1,345,736 | 2,329,738 / 1,345,728 |

Larger blocks reduce allocation events while increasing slack and coarse damage
extents. Grown buffers have not provided a general lifecycle/memory solution.
Sources: [coupled policy findings](bag-policies.md),
[Zen5 narrow-value rows](evidence/bag-policy-zen5/summary.csv),
[Granite narrow-value rows](evidence/bag-policy-granite/summary.csv),
[V2 narrow-value rows](evidence/bag-policy-neon/summary.csv), and
[wide-value cohort/recovery](evidence/spill-native/cohort.json).

## Compact lists

K64 word maps, 256 entries, scan64. **Timings are ns/query, ours / ordinary
Abseil from the matching scan64 trial**; baseline medians can differ from the
separate scan16 trial quoted in the historical notes.

| Operation | Zen5: ours / Abseil | Granite: ours / Abseil | V2: ours / Abseil |
| --- | ---: | ---: | ---: |
| Independent hit: reciprocal throughput | 4.54 / 1.72 | 4.87 / 2.07 | 8.09 / 2.37 |
| Dependent read: serial latency | 7.16 / 6.05 | 8.94 / 8.77 | 12.40 / 9.52 |

Initial logical memory, bytes, ours / Abseil:

| Case | Zen5: ours / Abseil | Granite: ours / Abseil | V2: ours / Abseil |
| --- | ---: | ---: | ---: |
| K64/W8 map, 256 entries | 4,184 / 8,728 | 4,184 / 8,728 | 4,184 / 8,720 |

The scan16 alternative is slower in this case. This particular list is compact;
tiny-owner overhead still prevents a memory-win claim for all small cardinalities.
Sources: [Zen5](evidence/surface-zen5-avx2/context.json),
[Granite](evidence/surface-granite-avx512/context.json),
[V2](evidence/surface-neon/context.json). This is the earlier native integrated
surface, with the source and baseline qualifications retained in those receipts.

## Two writers

K40 modular-sum aggregation, 110,000 keys and 1,760,000 inputs, uniform repeated
history. **Complete lifecycle ns/input**, with two pinned writers and charged
merge. These are aggregate reciprocal costs, not single-operation latency.

| Host | Ours | Private Abseil + merge | Shared Boost | Shared GTL |
| --- | ---: | ---: | ---: | ---: |
| Zen5 | 3.73 | 2.90 | 12.00 | 19.14 |
| Granite | 6.28 | 4.02 | 22.85 | 53.68 |
| V2 | 5.71 | 4.40 | 51.20 | 51.85 |

Sampled requested storage plus reported owner/routing metadata, **bytes,
ours / private Abseil**. Coexistence is sampled, not a proven transient peak;
allocator backing, runtime memory and shared-baseline allocations are not
established by these numbers.

| Host | Coexisting logical storage, B | Final logical storage, B |
| --- | ---: | ---: |
| Zen5 | 4,460,520 / 4,456,336 | 2,230,028 / 2,228,168 |
| Granite | 4,460,520 / 4,456,336 | 2,230,028 / 2,228,168 |
| V2 | 4,460,520 / 4,456,320 | 2,230,028 / 2,228,160 |

Shared concurrent-reader semantics, coordinated online growth and occupancy
balancing are not established. Sources: [Zen5](evidence/concurrent-workers-zen5/large-summary.json),
[Granite](evidence/concurrent-workers-granite/large-summary.json),
[V2](evidence/concurrent-workers-neon/large-summary.json).

## Explicit SIMD packets: local diagnostic only

The broader hierarchy packet panel remains ARM/OrbStack diagnostic evidence.
N4 improves hits in 11/99 cells and updates in 9/99; N8 in 29/99 and 10/99.
Those counts describe coverage, not a workload-weighted score. For a concrete
same-history comparison, these are K40/W2, 192-key cases, **ns/operation,
ours / ordinary Abseil using the same N**. All are reciprocal throughput;
this panel does not measure serial latency.

| Operation | Scalar: ours / Abseil | N4: ours / Abseil | N8: ours / Abseil |
| --- | ---: | ---: | ---: |
| Map, hit | 3.65 / 2.25 | 4.01 / 2.34 | 3.56 / 2.12 |
| Map, update | 10.92 / 9.68 | 11.90 / 7.69 | 10.17 / 7.19 |
| Map, mutation lifecycle per initial key | 68.25 / 26.48 | 67.82 / 24.31 | 57.40 / 23.87 |
| Multimap, hit | 3.71 / 3.60 | 4.85 / 3.50 | 4.65 / 3.29 |
| Multimap, update | 10.67 / 11.15 | 16.12 / 9.93 | 15.38 / 8.93 |
| Multimap, mutation lifecycle per initial key | 70.75 / 30.06 | 77.91 / 28.43 | 81.71 / 27.67 |

Initial logical memory for those scalar histories, bytes, ours / Abseil:

| Case | Logical storage, B |
| --- | ---: |
| Map | 9,000 / 4,288 |
| Multimap | 9,008 / 8,288 |

Fixed hierarchy geometry here is a diagnostic, not a chosen small-table regime.
The compact-fact rerun did not reproduce its early apparent gains; there is no
selected public N or native batch policy. Source:
[full absolute summary](evidence/hierarchy-packet-surface-local/summary.csv),
[scope](evidence/hierarchy-packet-surface-local/context.json), and
[packet assessment](hierarchy-batches.md).

## Coverage and current disposition

All eight key domains and all three kinds have native integrated evidence,
but not one up-to-date combined full-surface comparison. Recent common-read
tuning covers five representative kind/key/value cases; the newest spill
change is K40/W32/W64. Other awkward widths have correctness coverage without
equivalent performance breadth. Current native wider-value panels include
K16/K40/K64. Large residence and worker results remain mainly K40 word maps.
The full latest design has not demonstrated L1/L2 serial parity, broad LLC/DRAM
operation coverage, scaling, long churn/compaction, or competitive retained-reader
cost. Logical byte wins do not establish RSS wins; the large Granite allocation
history exposed retained resident backing, reported to Orbital.

Explicit NEON/AVX2/AVX-512 kernels and multiple physical formats have been
measured. A final platform-informed layout/growth policy is still unselected.
Several admitted identity field transfers compile identically to direct
access; nonidentity composition and owner/geometry costs retain separate
questions. The present evidence does not support blaming all deficits on the
packing primitives, nor does it establish that composition is cost-free.

The implementation recommendation remains **do not graduate yet**. This is
a substantial implementation with useful measured components, not a nearly
finished broadly winning container. Common reads, compact lists and multimap
storage/lifetime need architectural answers; cold Ring wins alone do not
discharge the original targets. Future progress should be judged against this
head-to-head picture, rather than the number of controls or timing rows produced.

## Earlier complete comparisons

The fresh implementation has not met the broad performance target. Compact
primary buckets save little requested storage over fuller ones in these shapes,
while adding downstream work. Fuller buckets usually improve reads, but ordinary
Abseil still leads independent lookup throughput. This is evidence about the
initial executable grammar, not a verdict on every layout in the requested space.

## What was measured

The first receipt compares ordinary Abseil with 64-byte primary / 128-byte
secondary units and 128-byte units in both tiers. Slot counts follow the bytes
left after metadata, selectors, suffixes and 64-bit values. Secondary capacity is
half the primary bucket count for compact, one sixteenth for fuller. These ratios
are experimental inputs, not selected defaults. Real StreamPack overflow links,
Orbital allocations and SeriesPack/TuplePack operations execute in both candidates.

The density panel adds neighboring populations, including both sides of Abseil
capacity changes, and paired downstream summaries for K40. Each case has 12
rotated repetitions and construction, reads, replacement, quarter-population
turnover and owner release timings. Turnover cycles between two fixed key sets;
it is not indefinite insertion of previously unseen keys. `owner_release` excludes
the later Orbital pool drain. The panel retains all 4,032 timing rows per target.

| Target | First job | Density / summary job |
| --- | --- | --- |
| Neoverse V2, NEON | `20261013T110047Z-e61cace7` | `20261013T111536Z-df5f949b` |
| Zen5, AVX2 | `20261013T110057Z-32c22741` | `20261013T111542Z-3da3a239` |
| Granite Rapids, AVX-512 | `20261013T110104Z-76b51a05` | `20261013T111548Z-5f4c33df` |

The panel source SHA-256 is
`c865190449fe675452b431a3a14717a1e956351bdb9f2c01ff1d5d44427e728e`.
Selected receipts are under `evidence/first-*` and `evidence/panel-*`; each has
source/host recovery in its `artifact.json`. For example:

```sh
python3 workbench/spikes/hashtable/report.py \
  workbench/spikes/hashtable/evidence/panel-neon/operations.csv --panel --filters
```

These are small working sets, not a cold-memory comparison. K40 uses ordinary
`uint64_t` Abseil keys; the exact-five-byte competitor is still owed. No cold
throughput, online growth, multiwriter or multimap conclusion follows here.

## Lookup and composition

At K40 / 1,800 entries, median independent-hit ns/operation were:

| Target | Abseil | Compact | Fuller |
| --- | ---: | ---: | ---: |
| V2 | 2.00 | 5.39 | 4.53 |
| Zen5 AVX2 | 1.52 | 3.08 | 2.60 |
| Granite AVX-512 | 1.86 | 4.11 | 3.38 |

The [assembly review](codegen.md) qualifies serial comparisons: the initial x86
Abseil dependent consumer outlines `find`, unlike the candidate. A forwarding-only
inlining correction did not fix that. The later packet capture's correction on
the actual lookup fuses point, cohort and independent consumers on all three
targets. On Granite, Abseil K16/270 dependent lookup improves from about 9.9 ns to
8.82 ns, alongside fuller 8.88 ns; K16/5000 is Abseil 9.65 ns and fuller 10.06 ns.
The apparent x86 architectural latency advantage does not survive that correction.
ARM's baseline was already fused.

Full projection-identity admission removes hot TuplePack control interpretation.
For K16/A9, composed and direct dependent consumers have identical instruction
sequences on all three targets. The independent bodies also match apart from
equivalent cold-link relocations. Granite's measured mixed-read difference is
therefore not evidence of additional primitive instructions. Its cause remains
unresolved. This direct arm shares routing and addresses; an independent fused
organization remains a separate comparison.

## Downstream summaries survive the first comparison

K40 recipes have four unused bytes at the end of each primary unit. A 32-bit
summary uses them without changing slots, field offsets or allocation sizes.
Insertion sets the bit selected by the low five fingerprint bits. Deletion clears
the summary when the last descendant disappears; otherwise stale bits remain.
Checks reconstruct the necessary bits from actual downstream entries, including
forced overflow, deletion, reuse and random histories.

At K40 / 2,100 entries, median 50%-miss ns/operation were:

| Target | Abseil | Compact | Compact + summary | Fuller | Fuller + summary |
| --- | ---: | ---: | ---: | ---: | ---: |
| V2 | 1.68 | 8.96 | 5.48 | 7.60 | 4.63 |
| Zen5 AVX2 | 1.24 | 5.12 | 3.13 | 4.37 | 2.59 |
| Granite AVX-512 | 1.63 | 7.08 | 4.18 | 5.58 | 3.46 |

This is roughly 38–42% less time than the paired unfiltered candidate, while still
well behind Abseil. At lower pressure the benefit is smaller; V2 fuller at 1,600
entries becomes slightly slower with the summary. Primary-hit throughput can also
regress. The summary is consequently not a universal default.

For compact / 2,100, the median summary initially has 1,010.5 set bits across the
table and avoids 1,658 of the 2,100 known-absent downstream visits. After the
measured turnover it has 1,364.5 bits, of which 278.5 are stale, and avoids 1,358.5
visits. The retained counters report the fuller case too. This is a real aging
cost; a longer fresh-key history remains necessary before choosing maintenance.

## Requested bytes are not the full memory result

For K40 / 1,600, Abseil requests about 34.7 KB, compact about 33.2 KB and fuller
34.8 KB. At 1,800, Abseil has grown to about 69.6 KB while compact/fuller's requests
remain near 34–35 KB. A claim of roughly half the memory based only on the latter
case would hide the capacity cliff. Candidate owner metadata is also larger and
shared preparation is not free.

The shared Orbital pool introduces another effect. K16/A5 fuller always requests
4,352 bytes, but an early case owns 4,352 bytes of accessible capacity and a later
case owns 65,792. Orbital's prepared-window reuse can satisfy a 4,096-byte request
from a released 65,536-byte window. It does not shrink that backing on reuse.
The panel's compact/full owned-capacity difference consequently includes prior
allocation history, not just intrinsic geometry. Orbital confirmed this as a
current policy trade-off and legitimate allocator friction.

`capacity()` is accessible backing owned by each allocation, not RSS or the
pool's total charge. Overflow accounting includes retained empty blocks. Idle
pool inventory, shared finite-group overhead and allocator metadata require pool
statistics as well. Abseil's backing value of zero in these CSVs means unmeasured.
A dedicated `allocation` driver compares fresh small allocations, same-size reuse
and large-to-small history with owned capacity and pool statistics side by side.
No replacement allocator or private reuse layer is introduced.

The [local allocation-policy receipt](evidence/allocation-history/allocation.csv)
repeats that sequence in 12 independent pools. Every repetition gives:

| Small-table history | Requested | Owned capacity | Pool mapped | Pool idle |
| --- | ---: | ---: | ---: | ---: |
| Fresh | 4,352 | 4,352 | 8,192 | 0 |
| Same-size reuse | 4,352 | 4,352 | 8,192 | 0 |
| After the larger table | 4,352 | 65,792 | 73,728 | 4,096 |

The larger table requests 69,632 bytes. Releasing the final small table leaves
73,728 mapped and idle bytes. These are Linux OrbStack accounting observations,
not native-target allocation latency measurements. Orbital owns the follow-up.
Its native selector comparison found that tighter selection can reduce mapped
capacity, but wider search can materially increase selection time; no replacement
policy has been promoted. Accessible virtual capacity is not resident memory.
Its ongoing work includes residency and page reclamation, rather than assuming
an oversized virtual window is itself an RSS loss.

## Corrected packet comparison

The corrected packet comparison binds immutable read facts once per cohort
and staged `get4/get8`, with matched unrolled Abseil consumers. Hashes are still
scalar in this scheduling control; explicit SIMD hashing is not claimed. Oracle
checks include duplicate packet keys, unchanged miss outputs, output guards and
forced secondary/chain routes. Neither the public batch width nor a layout preset
is selected by the first measurements.

The first packet capture exposed an implementation error before selection: only
the outer unroll helper was forced inline. Abseil outlined one callback per packet
and the staged candidate outlined up to five. Those timings include avoidable
materialization and are not a packet-width result. Both expansion and callback
now use SeriesPack's existing explicit-inline pattern. `check_codegen.py` accepts
the corrected local binary and rejects the captured failing binary; native runs
apply it before timing. It checks fusion, not spills or general code quality.

All three corrected native panels pass that check and retain 6,552 timing rows
each, including construction, churn and the density/filter cases. Source SHA-256:
`6d73bce2708954a99bcb7e081cfb10086af8ec6f34cd08a4a44389fb84f3d3ff`.

| Target | Job | Retained comparison |
| --- | --- | --- |
| V2 | `20261013T114613Z-4f3a6ca7` | [packet-neon](evidence/packet-neon/artifact.json) |
| Zen5 AVX2 | `20261013T114620Z-4806037f` | [packet-zen5-avx2](evidence/packet-zen5-avx2/artifact.json) |
| Granite AVX-512 | `20261013T114627Z-be8d8dfc` | [packet-granite-avx512](evidence/packet-granite-avx512/artifact.json) |

At K40 / 1,800, median ns per operation are shown as **Abseil / fuller**:

| Target | Serial | Independent hits | Packet4 hits | Packet8 hits |
| --- | ---: | ---: | ---: | ---: |
| V2 | 9.48 / 12.35 | 2.00 / 4.63 | 2.01 / 4.15 | 1.97 / 4.11 |
| Zen5 | 6.06 / 6.77 | 1.47 / 2.73 | 1.35 / 2.36 | 1.28 / 2.32 |
| Granite | 8.83 / 9.15 | 1.84 / 3.37 | 1.67 / 2.90 | 1.61 / 2.67 |

Staging helps fuller in these shapes, but the baseline also benefits. Binding
once around the cohort does not close the serial gap. Eight is not universally
better than four, and compact's extra downstream work often costs more than the
staging recovers. These results do not choose a public packet width. For example,
K16/5,000 has weaker hit-throughput differences and meaningful fuller turnover
wins; keep the complete case rather than extrapolating this K40 table.

## Larger working sets before software prefetch

The next panel compares 110,000 and 3,600,000 K40 entries, five repetitions and
the same thirteen operation phases. Every target passes the final-binary fusion
checks. Source SHA-256:
`434a0418ffe3873bc7427317b8d9707f6ceba91fbaa0e9d56a84b9af336488e3`.

| Target | Job | Reported L1d / L2 / LLC | Retained comparison |
| --- | --- | --- | --- |
| V2 | `20261013T115518Z-27d9bee9` | 64 KiB / 2 MiB / 36 MiB | [residence-neon](evidence/residence-neon/artifact.json) |
| Zen5 | `20261013T115525Z-06af327b` | 48 KiB / 1 MiB / 4 MiB | [residence-zen5-avx2](evidence/residence-zen5-avx2/artifact.json) |
| Granite | `20261013T115531Z-20c1f185` | 48 KiB / 2 MiB / 480 MiB | [residence-granite-avx512](evidence/residence-granite-avx512/artifact.json) |

The larger table requests approximately 64–68 MiB, exceeding V2/Zen5's reported
LLC but fitting within Granite's. The smaller table requests about 2.0–2.1 MiB.
These are footprint qualifications, not PMU proof of each access's service level.
The same cardinality must not be called DRAM-resident on all three machines.

At 3,600,000, median ns per operation, **Abseil / fuller**:

| Target | Serial | Independent hits | Packet4 hits | Packet8 hits | Turnover |
| --- | ---: | ---: | ---: | ---: | ---: |
| V2 | 146.09 / 146.67 | 17.65 / 33.76 | 17.61 / 28.06 | 17.45 / 29.23 | 45.34 / 40.00 |
| Zen5 | 108.65 / 132.58 | 17.57 / 34.91 | 15.63 / 30.89 | 15.52 / 32.59 | 40.67 / 38.25 |
| Granite | 73.82 / 69.42 | 12.16 / 14.47 | 11.69 / 13.86 | 12.95 / 14.09 | 33.08 / 21.49 |

Bare packets leave a large cold hit-throughput deficit. Four beats eight for
fuller here, unlike several hot cases. Compact sends about 1.14 million entries
to secondary storage, versus 94 thousand for fuller; chain populations are only
about 2.1 thousand and 35 respectively. This panel does not choose a chain policy.
At 110,000, fuller still loses serial and independent-hit performance on all
three targets, while several mixed-miss and turnover outcomes differ. Neither
hot throughput nor cold latency alone predicts the useful regime.

Fresh Abseil/fuller's requested bytes are both about 71.3 MB at the larger size;
compact requests about 67.2 MB. Abseil grows during the turnover history to about
142.6 MB, while the candidates remain about 67.9–71.5 MB. That is a measured
history/capacity difference, not a general twofold memory win. Candidate owned
capacity also carries shared-pool history; baseline backing remains unmeasured.

Reproduce either retained report with `report.py --packets`, adding
`--panel --filters` for `packet-*` or `--residence` for `residence-*`.

## Actual Loom execution

`ring.cpp` uses the same table's immutable reader to separate primary, secondary,
chain-directory and chain-payload visits. Secondary hashing is deferred work;
its address is computable from the key. Chain resolution is the genuine address
dependency. Direct calls, manual staging and actual `loom::Ring` consume the
same answer-dependent graph at concurrency 1/8/32. Abseil has both its ordinary
lookup and its public prefetch operation under the same graph/concurrency.

Mixed graphs insert guaranteed-absent high-bit mates before selected present
keys: roughly one third of requests miss, not one half. Each row reports its
actual request/hit/miss count. Values supply the next request; there is no timed
external trace load. Small cases repeat whole graphs to reach at least one
million requests. Initialization, admission and draining are inside operation
timing; Ring configuration, bank/job storage and close are reported separately.

Independent checks validate the complete sum, hits and terminal keys, emitted
windows within owner storage, and actual Ring visits. Stage counts are logical
candidate visits, not baseline work, issued hints or memory traffic. Whole-link
hints may cover lines that an early hit never loads. The current low-pressure
returned hint is a measured policy input, not a tuned optimum.

The owner remains alive and immutable without reentry or physical polling until
every job drains. Loom confirmed that this synchronous scope needs no additional
source Holds; retained or escaping reads remain a separate lifetime case. Local
Release and ASan checks pass, including forced chains and uneven streams.

All three native jobs completed seven rotated repetitions at all three sizes.
Every final binary has 28 ready callbacks without ordinary calls or stack
references, and six fused ordinary Abseil lookup/prefetch consumers. Source SHA-256:
`aafa5a315d52c661d31ec8bcc94085216a8d8224c165f4c050a0f0e0b4e8c9d0`.

| Target | Job | Retained comparison |
| --- | --- | --- |
| V2 | `20261013T123020Z-1f4ae28f` | [ring-neon](evidence/ring-neon/artifact.json) |
| Zen5 AVX2 | `20261013T123026Z-67f485d6` | [ring-zen5-avx2](evidence/ring-zen5-avx2/artifact.json) |
| Granite AVX-512 | `20261013T123032Z-ca4aaec0` | [ring-granite-avx512](evidence/ring-granite-avx512/artifact.json) |

At 3,600,000 entries and C32, median ns/request for **fuller**:

| Target | Best ordinary Abseil, hits | Ring + hints, hits | Best ordinary Abseil, mixed | Ring + hints, mixed |
| --- | ---: | ---: | ---: | ---: |
| V2 | 19.32 | 16.10 | 24.18 | 15.48 |
| Zen5 | 17.81 | 7.15 | 19.96 | 8.70 |
| Granite | 10.20 | 7.63 | 15.89 | 9.99 |

These are useful throughput wins: approximately 1.2–1.6× on V2, 2.3–2.5× on
Zen5 and 1.3–1.6× on Granite. Granite's table still fits its reported LLC.
The best baseline is selected between its ordinary and public-prefetch arms in
the same layout trial. The report also retains paired ratios by repetition.
Do not compare absolute times directly with the preceding packet panel: this
driver has a different key/next-request graph and miss fraction.

Manual staging plus the same hints gives 16.20/14.96 ns (hits/mixed) on V2,
6.91/8.13 on Zen5 and 7.42/9.08 on Granite. Thus actual Ring captures much of the
opportunity, with some scheduling overhead. Unhinted Ring is substantially slower.
At C8, these wins shrink or disappear; concurrency and hints both matter.
Compact C32 hit throughput is 15.65 ns on V2, slightly faster than fuller in this
receipt, but is 11.43 ns on Zen5 and 11.49 on Granite. The memory/work division
must be selected with execution regime, not a universal larger-bucket rule.

The important losses remain visible. For fuller C1 at the large size, Ring+hints
versus the better Abseil arm are 137.78/136.32 ns for V2 hits, 113.13/108.05 for
Zen5 hits, and 55.31/55.44 for Granite hits. On mixed C1 they are 118.74/96.52,
99.26/79.53 and 48.37/53.98 respectively. ARM/AMD's mixed serial gap is too large
to call the requested small latency trade-off met. Fresh V2 manual+hints is
118.20 ns on this mixed graph, only 0.55 ns from Ring. Loom's assembly audit
confirms that both fresh paths already avoid the earlier body's compiler-induced
next-key dependency. Preserve that boundary: the residual gap is against Abseil,
not evidence that the scheduler costs another twenty nanoseconds.

At 110,000 entries, fuller Ring+hints still loses hit throughput to Abseil on
all three targets; hot 1,800-entry throughput loses broadly too. Staging is not
the ordinary hot path. No heat oracle or public packet-width decision follows.

Fuller contains 3,505,565 primary, 94,402 secondary and only 33 chained keys in
the large Ring case. Compact contains 2,455,319 / 1,142,602 / 2,079. Mixed graphs
make 5,399,754 requests, including 1,799,754 misses. C32 charges 2,368 Ring bytes
and 1,792 job bytes separately from the table. Those counts are stable across
targets; the returned policy revision and hint remain in each context row.
Fresh immutable graphs with so few chains still cannot choose overflow policy,
demonstrate retained-reader publication, or establish aging behavior.

Reproduce a retained Ring report with:

```sh
python3 workbench/spikes/hashtable/ring-report.py \
  workbench/spikes/hashtable/evidence/ring-neon/large.csv --mode large
```

## One-search accumulation

`aggregate.cpp` adds an immediate, infallible existing-value combiner to the same
mutation body used by assignment. The baseline uses ordinary `try_emplace` and
updates its returned value. The candidate's separate get/set arm is an attribution
control, not the baseline. Every trial verifies unsigned word sums independently,
charges construction/reserve and owner release, and reads the finished map.

The final binaries fuse both implementations' hot search and existing-value update.
Clang's ordinary budget left Abseil's private insertion search outlined; a common
`-inline-threshold=1000` applied to every arm fixes that. Public callsite `flatten`
alone did not. Abseil still calls `PrepareInsertLarge` on new-key insertion; this
is not merely a rare growth call. The larger budget also expands candidate cold
chain/format code. These are measured complete consumers, not equal instruction
footprints or an isolated combiner comparison. See [the binary audit](codegen.md).

Source SHA-256: `8cd931e84ad5445796fbf8d0fabfe7de0176cd2888e2e3f9682bfd90c94ee451`.
All jobs completed nine rotated repetitions, three histories and four arms at
each size (108 rows per panel), with all-width mutation/refusal and forced-chain
aggregate checks before timing.

| Target | Job | Retained comparison |
| --- | --- | --- |
| V2 | `20261013T130015Z-7338bda5` | [aggregate-neon](evidence/aggregate-neon/artifact.json) |
| Zen5 AVX2 | `20261013T130021Z-9d051c57` | [aggregate-zen5-avx2](evidence/aggregate-zen5-avx2/artifact.json) |
| Granite AVX-512 | `20261013T130028Z-ef1323ec` | [aggregate-granite-avx512](evidence/aggregate-granite-avx512/artifact.json) |

At 110,000 distinct keys, median ingestion ns/input row; each cell is
**Abseil / fuller one-search / fuller two-search**:

| Target | Distinct once | Uniform repeated, 16× rows | Skewed, 16× rows |
| --- | ---: | ---: | ---: |
| V2 | 8.62 / 11.39 / 11.80 | 6.99 / 9.27 / 14.47 | 5.04 / 7.36 / 11.80 |
| Zen5 | 5.65 / 4.81 / 7.96 | 4.00 / 5.10 / 20.77 | 3.26 / 4.13 / 16.34 |
| Granite | 6.45 / 7.89 / 11.54 | 6.34 / 9.75 / 37.72 | 4.62 / 6.92 / 24.80 |

One-search accumulation is materially better than searching again to store the
result. It still loses repeated/skewed ingestion on every native target. At 1,800
keys, fuller distinct insertion narrowly wins on V2/Granite and narrowly loses
on Zen5; repeated updates lose throughout. Compact is substantially slower in all
these histories. These results favor fuller for this consumer without selecting
a universal organization.

The 110,000-key indexes request about 2.1–2.2 MB; the repeated input stream is
about 28 MB. A large input stream does not make the table DRAM-resident. The hot
candidate requests about 35 KB versus Abseil's roughly 70 KB native-key layout,
so equal cardinality is not equal cache residence. Result reads are warmed after
the independent oracle and a full lookup pass. This is known-cardinality scratch
aggregation, with no online growth, worker merge or MVCC conclusion. The upcoming
worker comparison must keep these ingestion losses visible alongside merge costs.

## Two private writers: adoption and same-layout merge

`workers.cpp` measures two physical cores with separate Orbital Lanes/Pools.
Each table is constructed, mutated, merged and released on its original owner.
Readers borrow the result only between synchronization barriers. Partitioned
workers queue intake mismatches and retain their disjoint tables behind a router;
same-key workers combine into their largest existing local. Ordinary Abseil gets
the same choices and receives actual local cardinality for reservation.

Candidate merge reuses matching primary encodings. A primary vacancy cannot skip
downstream duplicate checks; secondary and chained source entries reconstruct keys
and use ordinary upsert. Source references never transfer. The reconstruction arm
is a control through the same geometry and source order. Allocation refusal returns
an unconsumed cursor, tested after an already-combined downstream duplicate so a
retry cannot silently double its sum. Release/ASan checks and every native oracle
pass, including result reads after source retirement.

Corrected source SHA-256:
`a7069bf6134aaae26cf0519753cf795b0d1d99e0b897a51f4865660d4e24f1fe`.
The initial worker receipt included memory census in outer phase timers and is
superseded. These runs put census in separate untimed synchronized phases.
All three corrected jobs completed nine rotated repetitions, six arms and three
histories at both sizes (162 rows per panel), with final-binary fusion guards.
Each topology receipt confirms two distinct physical cores on one socket.

| Target | Corrected job | Retained comparison |
| --- | --- | --- |
| V2 | `20261013T132919Z-9d45a20c` | [workers-neon](evidence/workers-neon/artifact.json) |
| Zen5 AVX2 | `20261013T132925Z-5e74034c` | [workers-zen5-avx2](evidence/workers-zen5-avx2/artifact.json) |
| Granite AVX-512 | `20261013T132932Z-27f80b71` | [workers-granite-avx512](evidence/workers-granite-avx512/artifact.json) |

At 110,000 distinct inputs, median **complete table lifecycle ns/input row**:

| Target | Abseil partitions | Candidate partitions | Abseil merge | Candidate local-sized merge | Candidate roomy merge |
| --- | ---: | ---: | ---: | ---: | ---: |
| V2 | 6.25 | 8.06 | 16.92 | 54.40 | 16.10 |
| Zen5 | 6.59 | 6.03 | 14.43 | 36.18 | 11.28 |
| Granite | 7.60 | 8.04 | 16.69 | 45.73 | 18.85 |

Lifecycle includes construction/reserve, intake/routing, exception consumption or
merge, source/routing-buffer retirement and final owner release. It includes their
barriers, but excludes oracle validation, census and post-result reads. Reusable
worker/Pool creation and the coordinator's post-release join wait are separate;
the latter is not the complete physical Pool drain duration. The coordinator
shares the allowed cores. Small-table wall timings therefore contain material
scheduling overhead; per-worker phase timings remain available.

The local-sized distinct merge retains its original geometry and pays actual
chain expansion: median chained population is 32,973. Reconstructed merge is
similarly poor, so encoded primary transfer is not the missing capacity policy.
Its result also reads much more slowly: about 34–44 ns/key, versus 2.6–5.6 for
the merged baseline. Roomy locals use twice the primary bucket count from the
start; they leave only tens of keys chained. Their distinct merge phase is faster
than Abseil on all three targets, yet the complete lifecycle wins only on V2/Zen5.
This is not an online-growth or cross-geometry merge result.

Roominess is charged: for this distinct history the two candidate sources plus
receiver storage coexist at about 4.46 MB requested, versus 3.34 MB for consuming
Abseil. Their final maps both request about 2.23 MB. Local-sized candidate merge
retains about 1.82 MB, but its extra chains make that saving a poor trade here.
These are sampled requested bytes, not transient relocation peaks or RSS.

For repeated inputs, matching encodings reduce candidate merge-phase time versus
reconstruction by about 40% on V2, 24% on Zen5 and 21% on Granite. Complete
same-key lifecycles still lose: candidate/baseline ns/input row are 5.85/4.38,
3.59/3.03 and 6.22/4.47 respectively. Skewed histories also lose. At 1,800 keys,
the candidate does not establish a broad complete-lifecycle win. A cheap merge
does not compensate for the ordinary per-row update gap.

This first routing driver materializes exceptions until its exchange barrier.
With 50% misroutes, its two vectors retain about 16.8 MB capacity, taking sampled
table-plus-routing coexistence to about 19.0 MB, versus about 4.46 MB for merge.
The repeated input stream has 1.76 million 16-byte records. That cost applies to
both routed contenders; it is not an intrinsic requirement of partitioning.
Bounded streaming exchange, backpressure and actual Loom intake scheduling remain
open. Same-key locals also need coordinated growth or an efficient geometry
transition before selecting them for mostly-distinct input. The point-update
gap, queue regime and capacity policy deserve separate experiments; none is
evidence for dropping a requested ownership mode.

Reproduce, including strict coverage and population checks:

```sh
python3 workbench/spikes/hashtable/workers-report.py \
  workbench/spikes/hashtable/evidence/workers-neon/large.csv --n 110000
```

## Bound mutation is not a general improvement

The same-placement experiment deliberately moves a primary miss behind a call,
while copying immutable primary context once. Native inspection confirms that
V2 removes owner reloads and x86 removes per-row spill stores, although three
x86 seed reads still use the stack. Existing-key Abseil search/update is fused.
The continuation never repeats primary hashing or lookup.

Captured source: `8bc6f8dcde84403bae8379838fbda8e72f227716557dd7f395466d49d425fe05`.
Each completed target has nine rotated repetitions, three histories and three
arms at both sizes (81 rows/panel), with equal candidate tier populations and
requested bytes. Full oracle and refusal checks passed.

| Target | Job | Comparison |
| --- | --- | --- |
| V2 | `20261013T134927Z-c0706ef6` | [mutation-neon](evidence/mutation-neon/artifact.json) |
| Zen5 AVX2 | `20261013T134934Z-64aabcbd` | [mutation-zen5-avx2](evidence/mutation-zen5-avx2/artifact.json) |
| Granite AVX-512 | `20261013T134940Z-d46c1243` | [mutation-granite-avx512](evidence/mutation-granite-avx512/artifact.json) |

At 110,000 distinct keys, ingestion ns/input; each cell is
**Abseil / ordinary candidate / bound candidate**:

| Target | Distinct once | Repeated 16× | Skewed 16× |
| --- | ---: | ---: | ---: |
| V2 | 8.30 / 11.29 / 15.14 | 6.63 / 9.17 / 8.84 | 4.65 / 7.16 / 6.81 |
| Zen5 | 4.81 / 5.06 / 21.31 | 3.98 / 5.25 / 6.06 | 3.15 / 4.15 / 4.73 |
| Granite | 7.36 / 7.89 / 32.51 | 6.80 / 9.98 / 11.51 | 5.37 / 7.76 / 9.00 |

The 1,800-key panel has the same direction: slight repeated/skewed improvement
on V2, losses on x86, and insertion losses throughout. Removing visible spills
is not enough to improve a complete operation. Every insertion/downstream hit
pays the new continuation boundary and argument packaging. The experiment rejects
this boundary as a general accumulation choice; it does not identify the entire
remaining cost of hashing or prove that all cohort binding is unhelpful.

These are fixed-geometry scratch owners. The captured all-width check used low
keys; the subsequent live check additionally exercises high-bit keys and domain
extremes, with Release/ASan success. Those stronger local checks do not change
the identity of the native timed source.

## Coordinated geometry transition control

Two local owners are constructed and filled, then one receiver can grow before
merging. Both contenders receive the exact union cardinality in this diagnostic.
The candidate's provisional trigger is 7/8 primary capacity. A replacement uses
new caller-provided entropy and reencodes keys. `unique` migration uses source
uniqueness; `rebuild` performs ordinary insertion. No reference crosses owners.
These choices are controls, not a selected distributed growth policy.

Captured source `dc84c981fcec782cd8fc2d52297fbd06af0d5f5c2678117f1d1daf0285288a04`:
V2 job `20261013T141419Z-e8c80bfd`, Zen5 AVX2 `20261013T141426Z-98e60ba7`,
Granite AVX-512 `20261013T141433Z-f5191f23`. Each target completed nine repetitions,
four arms and three overlap histories at two sizes, with final-binary checks.
Receipts are in `evidence/transition-{neon,zen5-avx2,granite-avx512}`.

At 55,000 keys per local owner, **construction + transition + release ns/input**
(read timing and subsequent pool draining excluded); cells are
**Abseil natural / Abseil reserve / candidate unique**:

| Target | No overlap | 50% overlap | 90% overlap |
| --- | ---: | ---: | ---: |
| V2 | 18.22 / 19.77 / 27.75 | 21.03 / 21.15 / 31.44 | 18.03 / 18.16 / 13.15 |
| Zen5 | 12.49 / 14.15 / 14.43 | 13.04 / 15.35 / 17.25 | 10.62 / 12.78 / 8.21 |
| Granite | 14.94 / 17.46 / 22.90 | 16.25 / 18.88 / 26.73 | 13.42 / 15.75 / 11.75 |

Growth removes the heavy chain population from the fixed-local experiment, but
its complete large-table cost still loses. Avoiding unnecessary growth at 90%
overlap wins that lifecycle and retains 1.12MB versus Abseil's 2.23MB. Final point
reads still lose: candidate/Abseil ns at 90% overlap are V2 7.42/5.18, Zen5
4.61/1.95 and Granite 5.57/3.30. Hot lifecycles win across these histories, while
their final reads also lose. This does not establish broad competitiveness.

During growth, sampled candidate coexistence is 4.46MB, similar to Abseil's
**actual allocation high-water** of 4.46MB. The previous workers panel sampled
Abseil outside rehash and missed that transient. Candidate snapshots still omit
internal chain-growth transients and must not be called exact peaks. Hot geometry
changes both bucket count and slots: A7/N8 to A8/N9 is 2.25x primary capacity;
the large A13/N9 to A14/N9 transition is 2x. The `unique` arm reduces migration
work, but secondary-miss insertion still repeats some secondary hashing.

## First Granite run above LLC capacity

The 57.6M-key Ring panel completed on Granite AVX-512, job
`20261013T154306Z-9435c493`, source
`9ba78fa56585d679eae54371830b1505d0aca856f8c4f9d7e3ea80bc99950dde`.
[Retained results](evidence/dram-granite-avx512/dram-summary.txt) contain every arm,
not only the successful ones. This is **one repetition**, with matched full
request graphs; it establishes a first native observation, not run-to-run
confidence. Independent generator/inverse, graph, ownership and final-binary
checks passed. No per-key input vector or oracle map accompanies the timing.

Each table owns roughly 1 GiB of initialized payload, above the reported 480 MiB
LLC. Hit graphs cover all 57.6M keys; mixed graphs add 28.80M absent requests.
Resident mappings are recorded, with no huge pages or swaps observed and no major
faults in the timed process. PMU access works, but this run only probed availability:
no workload miss counters establish what fraction of requests reached DRAM.

Nanoseconds/request; `best Abseil` is the faster of ordinary and matched
prefetched execution at that concurrency:

| Layout / requests | C1 best Abseil / Ring+hints | C8 best Abseil / Ring+hints | C32 best Abseil / Ring+hints |
| --- | ---: | ---: | ---: |
| Compact / hits | 215.86 / 216.88 | 39.11 / 41.72 | 30.67 / 21.61 |
| Compact / mixed | 178.28 / 159.11 | 40.13 / 42.65 | 33.55 / 24.17 |
| Fuller / hits | 217.31 / 205.04 | 39.58 / 32.16 | 31.05 / 22.03 |
| Fuller / mixed | 181.92 / 175.22 | 41.40 / 31.34 | 34.79 / 22.16 |

At C32, hinted Ring is 1.39–1.57× faster than the best baseline in these cases.
Without the hints it loses substantially. Manual hinted staging is slightly
faster than Ring here; the gain belongs to effective latency hiding, not a claim
that Ring itself improves the leaf operation. Compact C8 still loses. These K40
word-map read results cannot establish all-width/set/multimap or mutation strength.

Memory has an important qualification. Compact requests 1.075 GB versus Abseil's
1.141 GB (5.8% less); fuller requests essentially the same as Abseil. Accessible
Orbital capacity is 1.075 GB in the first compact panel, but 1.612 GB in the later
compact panel and 1.611 GB for fuller. Pool reuse spans panels and can retain a
larger allocation than requested. Owner inventories preserve this difference;
requested-byte savings are not a measured RSS saving. This observation has been
reported to Orbital; no replacement allocation layer was introduced.

Orbital's subsequent mapping review confirms excess **resident backing** in this
specific receipt: the compact mixed primary reused a 1 GiB region for a 512 MiB
request, and fuller's 64 MiB secondary reused 512 MiB. Candidate allocation
intervals are disjoint; containing 4 KiB VMAs report RSS equal to size, without
THP. Whole-process RSS also includes baseline and idle inventory. There is no
strict-fit allocation flag: requested growth headroom is a minimum, not a capacity
ceiling. Fresh-Pool controls isolate geometry; they do not repair mixed-history
live retention. That workload remains with Orbital's page-retention investigation.

## Integrated compact-list diagnostics

The local all-width/all-kind comparison still falls short. At 256 entries,
16-byte-window lists lose independent hit throughput by roughly 3–5× against the
better ordinary/exact Abseil arm on the local ARM host. Owner metadata also defeats
a blanket memory-win claim for tiny tables. These measurements motivate real
list/promotion and owner-state changes, not an underfilled hierarchy as a small
case replacement.

A 64-byte selector window is an explicit NEON/AVX2/AVX-512 experiment, currently
default-off. Six local repetitions show about 1.23× faster 256-entry hits across
kinds but slower 64-entry hits; absent reads improve more. It does not establish
hot serial parity or close the baseline gap. The native campaign below compares
both window sizes from one capture. The local comparison is diagnostic: other
compile activity and unchanged 16-entry variation rule out interpreting thin gains.

A missed inline annotation in the multimap packet caller was corrected and
verified in disassembly. The wrapper call disappears; each chain consumer is
fused within its link traversal. A matched K40 before/after test shows no convincing
complete-lifecycle gain from that change alone. Faster-looking assembly is not
accepted as a performance result.

The independent bag lifecycle comparison exposed a larger composition mistake:
StreamPack directory/layout resolution was repeated for every value. Binding once
per link reduces read-phase cost about 2.2–2.7× locally, yet complete lifecycle
improves only 10–17%, because append and logical clone dominate. At 64 values/bag,
linked/buffer/ordinary InlinedVector lifecycles remain about 16.45/2.23/1.37 ms for
2048 bags; at 512 values they are 180.98/16.06/11.57 ms. That control used logical
replay for the link clone, not StreamPack's intrinsic byte-copy cost. A separate
owner-held insertion hint uses the spare four bytes alongside the head ref to
avoid walking the chain on every append. It materially reduces long-bag mutation
cost but still loses complete lifecycle to ordinary InlinedVector and buffers.

## Native integrated surface and platform differences

The bounded list, packet, publication, list-to-hierarchy and rich-composition
campaign completed on V2 (`20261013T160647Z-0a520773`), Zen5 AVX2
(`20261013T160641Z-55ee1d32`) and Granite AVX-512
(`20261013T160653Z-c37d4147`). All eight key widths and all three container kinds
are present in the integrated table consumers. Release oracles, guarded windows,
final-binary witnesses and CSV coverage pass, including the native AVX-512
exhaustive hash-packet oracle. This is single-writer evidence; it does not extend
the earlier worker controls or establish large-table residence coverage.

All hosts share content-manifest digest
`6f8ec283e3ba847f9de7deb937f40d388ecb5c11f64d4fecd5f463ab3ea88042`;
the compressed source archive hash is
`710b9a8465d0e1d18a45a0bd0fcc69bc9ab1246743af9f10a7230779be746de8`.
The capture precedes the later link traversal/hint/count changes and private
physical clones. Publication rebuilt both bodies through insertion, with a
candidate count reservation but no equivalent Abseil reserve/native-copy arm.
Those results describe that integration control, not strongest copy performance.
Key-table counters also enlarge the timed Abseil wrapper's owner state while
reported owner metadata excludes instrumentation. Slots and nested bag ABI stay
ordinary; this is not an uninstrumented array-of-small-owners comparison. The
publication baseline also owns its counted wrapper through a heap allocation;
its native-copy successor still charges that extra allocation and indirection.
Selected reports and complete recovery references are retained for
[V2](evidence/surface-neon/artifact.json),
[Zen5](evidence/surface-zen5-avx2/artifact.json) and
[Granite](evidence/surface-granite-avx512/artifact.json).

For K64 maps with 256 entries, median ns/query (scan16 / scan64 / ordinary Abseil):

| Target | Independent hits | Answer-dependent serial hits |
| --- | ---: | ---: |
| V2 NEON | 10.546 / 8.087 / 2.402 | 13.386 / 12.401 / 9.527 |
| Zen5 AVX2 | 6.177 / 4.541 / 1.742 | 7.787 / 7.159 / 6.166 |
| Granite AVX-512 | 8.531 / 4.872 / 2.039 | 10.556 / 8.941 / 8.652 |

Wider scans help this geometry, but they do not close independent throughput.
V2's 64-entry hits regress from 4.887 to 5.345 ns. The selector-window choice
therefore remains tied to complete geometry/operation, not simply ISA width.

Packets also need a complete-history decision. At K64/map/128 initial keys, Zen5
scalar/4/8 lifecycles are 5390/5320/5280 ns versus Abseil 5511/5570/5630; at 240
keys they become 13921/14720/15701 versus 9040/8790/8480. V2 at 128 keys is
12188/15221/15749 versus 7197/7314/6906; Granite is 12565/12918/12940 versus
7528/7480/6988. SIMD hashing alone does not establish batch-write superiority.

Rich packed fields show a substantial **feature-dependent** reversal. With 16
active rows per bucket across 256 buckets with six-byte rows, stronger existing SP/TP composition versus
independent raw fused hit/update times are Zen5 6.926/5.197 and 9.040/7.550 ns,
but Granite 8.079/11.796 and 8.643/11.300. Granite's composed predicate uses GFNI
and masked loads; the AVX2-only Zen5 build uses scalar transpose stages. This is
not an isolated AMD-versus-Intel microarchitecture comparison. The matched-feature
AVX2 / AVX2+GFNI / native study below separates that effect before choosing a lowering.
The 6/10-byte layouts are rich composition controls, not a completed full-solver
format selection. In particular, F7+C13 is deliberately a non-identity primitive
stress case: under the requested clamp-derived partition, Kb below eight implies
Kc zero. It cannot establish a GFNI gain for ordinary F8 K40 tables. The canonical
F8+C13 companion is necessary to isolate useful TuplePack packing costs.

The broad result remains negative. For example, Zen5 has 287 of 384 candidate
batch lifecycle cells more than 5% slower than ordinary Abseil. These are
unweighted diagnostic cells, not a workload score. Narrow merge, Ring or SIMD
wins cannot select a representation while mutation, hot reads and retained
multimap versions still have these gaps.

## Bag mutation and private copying

The [count-reuse comparison](evidence/bag-count-reuse-local/artifact.json) carries
the successful link count from reservation admission into commit preparation,
instead of resolving the same StreamPack slot again. This also lets Clang fuse
reservation into append: the existing-room path has two slot resolutions instead
of three. No cached pointer or wider descriptor was introduced. Paired local
512-value lifecycles fall from 50.95 to 39.38 ms; read time is essentially unchanged.
Smaller controls drift enough that thin changes are not persuasive.

The [physical-copy comparison](evidence/bag-physical-clone-local/artifact.json)
then keeps logical replay and adds an independent copy of assigned StreamPack
blocks plus local ref metadata. Both use insertion hints and the same descriptor
size. Requested reservations are preserved; source and destination own distinct
Orbital allocations. Release and ASan/UBSan checks cover ordered duplicate values,
source retirement, free-list reuse and refusal. It remains a whole-private-version
control, not COW.

Same-run medians for 2048 bags, complete lifecycle in ms:

| Initial values/bag | Hinted replay clone | Hinted physical clone | Orbital buffer | Ordinary InlinedVector |
| --- | ---: | ---: | ---: | ---: |
| 16 | 2.38 | 1.31 | 1.50 | 0.60 |
| 64 | 11.69 | 4.09 | 2.61 | 1.66 |
| 512 | 55.28 | 33.40 | 18.10 | 14.99 |
| Skewed | 0.843 | 0.562 | 0.993 | 0.298 |

At 64 values the clone phase falls from 6.01 to 0.443 ms; at 512 it falls from
23.01 to 5.46 ms. Copying reserved slots still costs memory traffic, and long-bag
complete lifecycle remains behind both alternatives. These are local ARM/OrbStack
diagnostics with concurrent compilation recorded, not native platform selection;
do not interpret thin buffer differences as established wins.

Fresh-Pool inventories isolate geometry: at 512 values the linked old+private
copy requests/owns approximately 33.62/39.05 MB, while buffers request/own
25.17/25.17 MB. The [earlier shared-Pool census](evidence/bag-shared-pool-history-local/artifact.json)
retains buffer backing near 73 MB under a different allocation history. It is an
untimed capacity observation, without address/RSS receipts, and must not be
presented as intrinsic buffer geometry or timed MVCC RSS.

## Matched ISA features in the packed-predicate control

The unchanged F7 rich-composition source ran with AVX2, AVX2+GFNI and native
features on each x86 host. The first two feature sets match across hosts;
microarchitecture tuning remains appropriate to each machine. Each host passed
the complete correctness/guard/codegen checks, then three rotated process passes
with eight internal repetitions per operation. Actual feature macros accompany
the [Zen5](evidence/rich-features-zen5/artifact.json) and
[Granite](evidence/rich-features-granite/artifact.json) receipts.

Six-byte rows, 16 active slots per bucket, 256 buckets; SP/TP split composition
ns/operation, **AVX2 / AVX2+GFNI / native**:

| Target | Independent hit | Serial hit | Changed update |
| --- | ---: | ---: | ---: |
| Zen5 | 6.857 / 3.818 / 3.583 | 19.345 / 16.672 / 16.184 | 8.860 / 5.597 / 5.305 |
| Granite | 13.486 / 5.303 / 8.054 | 24.667 / 20.810 / 21.279 | 11.438 / 11.047 / 8.571 |

GFNI removes and reverses the earlier Zen5 composition gap in this control: its
raw fused hit is approximately 5.14 ns. This is not Intel-specific. Native features
are not uniformly fastest: Granite favors AVX2+GFNI reads but native updates;
Zen5's ten-byte hit favors AVX2+GFNI too (3.647 versus 3.936 ns). The final binaries
show GFNI in both enabled paths, while the native path adds masked loads and mask
register comparisons. Those differences establish available execution shapes;
instruction counts alone do not explain every timing reversal.

This remains the deliberately noncanonical F7+C13 stress case, excludes hashing,
and does not rescue ordinary F8 table performance. A canonical F8+C13 companion
keeps nonidentity TuplePack rows while removing packed-fingerprint transposition;
the completed companion below distinguishes row-format costs from this selector gain.

## Canonical eight-bit fingerprints: composition and platform differences

The F8/C13 companion completed on V2, Zen5 and Granite, with the same source
manifest `e289c214d8540989592b13487de04236d401865ec62d2b3e899c05b402fceed2`.
This is legal K40/A19 bucket geometry. Six/ten-byte TuplePack rows remain packed;
the fingerprints now use the ordinary explicit SIMD byte comparison. Hashing,
routing between tiers and allocation are outside this control. Each full
16-slot bucket occupies 112/176 payload bytes, and the 2216-byte prepared object
is shared per format. No cache-residence claim follows from these footprints.

All 256 fingerprint symbols, exact guarded windows and full-value histories
passed. Three process passes each contain eight internal repetitions. The
[V2](evidence/rich-f8-neon/context.json),
[Zen5](evidence/rich-f8-zen5/context.json) and
[Granite](evidence/rich-f8-granite/context.json) evidence retains every arm,
operation and tail, including inactive cases and process variability.

Strongest composed/raw ratios across all nonempty tails and both footprints;
lower is better. The x86 rows below use matched AVX2 features:

| Target | Row | Independent hits | Serial reads | Changed updates |
| --- | ---: | ---: | ---: | ---: |
| V2 NEON | 6 B | 0.886–1.106 | 0.972–0.988 | 0.863–0.922 |
| V2 NEON | 10 B | 0.977–1.004 | 0.950–0.979 | 0.983–1.006 |
| Zen5 | 6 B | 0.967–0.999 | 0.941–0.959 | 0.863–0.888 |
| Zen5 | 10 B | 0.990–1.003 | 0.944–0.964 | 0.982–1.002 |
| Granite | 6 B | 0.912–0.984 | 0.939–0.979 | 0.749–0.951 |
| Granite | 10 B | 0.981–1.345 | 0.894–0.969 | 0.974–1.211 |

GFNI emits no instructions in this F8 control. All 20 complete strongest/raw
timing loops normalize identically between AVX2 and AVX2+GFNI on each x86 host.
The F7 selector gain therefore does not carry into this canonical geometry.
Native features are not uniformly better either: Granite native six-byte
full-bucket hits lose about 25% to raw. Zen5 six-byte fingerprint misses retain
a 5–7% gap despite better hits/updates. Point kernels and complete loops contain
no calls; the complete independent loops do spill registers. The Granite
throughput gap remains mechanistically unresolved. These are useful constraints
on lowering and format selection, not evidence that integrated tables meet their
targets or that the experimental binding is a finished public API.

### Granite crossover: throughput depends on more than the kernel body

The [mask-width crossover](evidence/rich-f8-mask-granite/mask-audit.json) ran all
four composed/raw 32/64-bit candidate-mask combinations under the same AVX2
features. This is a deliberately collision-heavy input: only four fingerprint
symbols occur per bucket, even in F8. At 15/16 active slots, hits average
2.413/2.531 suffix candidates. It must not stand in for uniform F8 collisions.

Changing the *other* implementation moved an unchanged loop within the binary.
All 40 unchanged-body comparisons were verified in native disassembly. For the
10-byte, 16-active-slot, 256-bucket case, identical raw32 loops changed independent
hit time from 4.656 to 6.881 ns while serial time stayed 19.367/19.320 ns. An
identical composed64 loop changed hits from 5.698 to 4.791 ns, with serial
18.681/18.743 ns. Instructions, spills and register allocation did not change.
Three process-median ranges separate; no lowering or mask-width choice is adopted.

This rejects a simple mask-width or intrinsic TuplePack-cost explanation. It
implicates placement-sensitive frontend/branch behaviour, but no PMU or
allocation-address census establishes the exact cause. The
[static/llvm-mca audit](evidence/rich-f8-mechanism/context.json) generally predicts
composed paths equal or faster and fails to reproduce the timing gap. Its model
does not cover all prediction/cache effects. This AVX2 experiment also does not
directly explain the separate AVX-512 six-byte result. Format and platform
selection need consumers robust to these effects, alongside ordinary-distribution
and adversarial-collision histories.

## Resumable hierarchy migration and concurrent baseline support

`migration.cpp` covers all eight key widths and all three kinds through changed
geometry and fresh seeds, with 16-byte opaque map values checked separately.
Release and ASan/UBSan checks pass: every source row can refuse once, budget-one
empty scans terminate, allocation refusal preserves a copied prefix, cancellation
preserves the old table, and transferred bags survive source retirement, append,
erase and reuse. An independently retained owner's full values remain unchanged.
Preparation keeps the source Pool; it is not cross-worker adoption or COW.

The first performance driver includes ordinary uninstrumented Abseil and
exact-width keys from the start. It measures grain 8/64/uninterrupted migration,
continued inserts, independent/serial reads and release, with full-result oracles
and complete width/kind/phase checks. Private construction now uses the scan's
uniqueness proof instead of searching again. [Local AArch64 diagnostics](evidence/migration-local/context.json) reduce the
64-work-unit K40 migration phase from 14.06 to 7.40 ns/key for sets, 16.83 to
9.14 for maps and 16.78 to 9.40 for multimaps. These are separate three-repeat VM
runs, with concurrent compilation and changed executable placement; they are
not native tuning selections. Large Abseil build/free timings and multimap
lifecycle variability make the local total-lifecycle comparison inconclusive.

The serial graph covers the original half after growth; independent queries
consume the full grown table. Coexistence is a sampled inventory, with unknown
baseline backing and no Abseil transient rehash peak. Summed phases have untimed
inventories between them. Candidate explicit doubling and Abseil natural growth
compare policies. Native runs on [V2](evidence/migration-neon/context.json),
[Zen5](evidence/migration-zen5/context.json) and
[Granite](evidence/migration-granite/context.json) completed all 2,250 cells with
nine repetitions from one frozen source. Independent hits lose to ordinary
Abseil in every one of the 45 cases on every host. For K40/A9, migrate64 time
divided by ordinary Abseil time is:

| Platform | Map hit | Map serial | Multimap hit | Multimap serial |
| --- | ---: | ---: | ---: | ---: |
| V2 NEON | 1.40 | 1.37 | 1.55 | 1.49 |
| Zen5 AVX2 | 1.48 | 1.20 | 1.36 | 1.60 |
| Granite AVX-512 | 1.29 | 1.25 | 1.51 | 1.55 |

These are single-writer exclusive-owner results, without established cache
residence. The complete read-loop audit found fused lookup and value consumption,
including multimap chains, with no per-lookup or indirect calls. This rules out
that wrapper explanation; it does not establish efficient instructions or layout.
Set/map phase-summed lifecycle wins remain qualified by upfront capacity versus
natural growth. Multimap initial build already differs before migration grain is
used: cyclic arm rotation balances positions but not predecessor/history effects.
The retained per-repetition controls therefore prohibit selecting grain from
lifecycle medians. For K40/A9, final map requested bytes plus metadata are
144,484 versus Abseil's 139,288; the format does not win that memory comparison.
The retained multimap serial driver follows the first returned value; its current
append-only bags preserve insertion order, but that benchmark lacks an independent
full-cycle assertion. It cannot be reused unchanged to compare reordered bags.
The subsequent format driver uses order-independent XOR successors and proves the
entire cycle before timing. This is a migration foundation, not coordinated worker
growth or balancing.

[Concurrent competitor support](evidence/concurrent-baselines-local/RECEIPT.md)
passes 96 Release controls with four writers (two libraries × eight widths ×
three kinds × ordinary/exact keys), plus K40 ASan/UBSan cases. The optional
`hashtable_design_concurrent_workers` bridge now has a
[verified native receipt](evidence/concurrent-workers-neon/RECEIPT.md) on V2,
[Zen5](evidence/concurrent-workers-zen5/RECEIPT.md) and
[Granite](evidence/concurrent-workers-granite/RECEIPT.md): 24 check rows and
216 timing rows per cardinality on each target, covering the same six private
arms plus ordinary Boost/GTL shared owners, all three K40 modular-sum histories
and nine repetitions. The workers use two distinct physical cores on the same
socket/node. Every private hot search/aggregate boundary fuses; the shared
adapters fuse too. Boost retains one outlined library combined-operation helper
with one search/protected update, while GTL's mutex/search/update path fuses.
V2 uses native LSE atomics. Final binaries and these helpers were inspected on
all three targets before interpreting timings.

For 110,000 keys and 1,760,000 inputs in the uniform repeated history, median
complete-table lifecycle ns/input is:

| Target | Private Abseil merge | Candidate same-seed merge | Boost shared | GTL shared |
| --- | ---: | ---: | ---: | ---: |
| V2 | 4.40 | 5.71 | 51.20 | 51.85 |
| Zen5 AVX2 | 2.90 | 3.73 | 12.00 | 19.14 |
| Granite AVX-512 | 4.02 | 6.28 | 22.85 | 53.68 |

Private aggregation avoids substantial shared protection costs in these
histories, but the candidate still loses to the stronger private Abseil control.
The retained panels include all arms, negative results, phase components and
repetition ranges. Tight distinct-key merge still chains a median 32,973 of
110,000 keys. Roomier geometry reduces this to 38 and wins distinct lifecycle
on Zen5 (10.58 versus Abseil's 13.87 ns/input), reaches parity on V2 and loses on
Granite, while spending about 33% more sampled coexisting requested storage than
Abseil merge. The thin Zen5 partitioned distinct result (5.90 versus 6.13) has
overlapping repetition ranges. Repeated/skewed histories and warmed final reads
still favor private Abseil; the small-cardinality panels do not reverse that
assessment.

Shared owners reserve known union cardinality and accept both unchanged intake
streams directly, with no artificial routing/merge charge. Lifecycle includes
setup, intake, finish, source retirement and release with barriers; runtime
creation, warmed final reads and post-release join are separate. The coordinator
is an additional OS thread. Unknown shared allocation totals stay null, so no
shared memory advantage is established. Quiescent Boost reads retain `cvisit`;
GTL uses its unlocked quiescent API. The cardinality labels do not establish
cache residency. This K40 map bridge does not provide broad width/kind worker
coverage, coordinated online growth/balancing, shared concurrent reader
visibility or arbitrary overlapping overwrite/delete semantics.


## Executable formats: density improves, broad read parity remains missing

The native finite comparison is complete on [Zen5 AVX2](evidence/formats-zen5/context.json),
[Granite AVX-512](evidence/formats-granite/context.json) and
[V2 NEON](evidence/formats-neon/context.json). Every host passed complete owner and
independent physical/guard oracles, then produced 58,320 measurements: sixteen arms,
45 cases, nine phases and nine repetitions. All arms and repetitions are retained,
including rejected layouts and ordinary/exact-key Abseil. Group
`20261013T180701Z-3219fd67` used source manifest
`58ca2ea54a6419c37610162759e880fd17a316066d9f580c419babee71402bd9`
and source archive `fe56a8a6ca2e900fb1b62315b1a28539ed7c09fd37f66afe7f9d2408f6a2fe3e`;
per-host receipts recover the frozen sources, binaries and complete disassembly.

A static recipe view now executes arbitrary bucket capacities, exact or padded
selector windows, row/plane bodies, separate byte/bit multimap tags and value
alignment. Whole-table list growth remains independent. The cases cover K8..64,
set/map/multimap, A6/A10 (A6 only for K8), 128-byte units and a common 16-selector
window. Map/multimap values are eight bytes. Local ASan/UBSan physical checks also
cover guarded exact tails, independent byte oracles, wide values, neighbour
preservation and mutation damage/refusal. This is an executable composition
comparison, not yet a selection of platform-optimal formats.

Exact capacity reduces downstream traffic. At K40/A10, the build populations
are identical across hosts:

| Kind and format | Primary/secondary slots per bucket | Primary / secondary / overflow entries |
| --- | ---: | ---: |
| Set, legacy | 16 / 16 | 13,572 / 749 / 15 |
| Set, exact | 30 / 23 | 14,336 / 0 / 0 |
| Map, legacy | 8 / 8 | 6,496 / 497 / 175 |
| Map, exact | 10 / 9 | 6,948 / 220 / 0 |
| Multimap, exact byte tags | 9 / 8 | 6,771 / 373 / 24 |
| Multimap, bit tags | 10 / 9 | 6,948 / 220 / 0 |

These hold content, seeds, bucket counts and unit sizes constant. Cardinality
comes from legacy capacity, so denser recipes run at a lower **own load factor**.
They have not been compared at equal relative occupancy. Requested bytes plus
owner metadata for the K40 map fall from 144,264 to 139,552; ordinary Abseil is
139,288 on x86 and 139,280 on V2. Across the fifteen cases of each kind, exact
sets use less logical memory than ordinary Abseil in ten, exact maps in none.
The fixed K8 hierarchy bypasses the compact-list policy and is particularly
wasteful. These are logical inventories, not RSS or transient peak measurements;
baseline backing capacity is unknown, not zero measured memory.

Density does not close the main read gap. The fixed exact arm loses independent
hits in all 45 cases on each host, and loses all thirty map/multimap serial
comparisons. K40/A10 maps illustrate the tradeoff below; each cell is exact /
ordinary Abseil ns per operation:

| Platform | Independent hit | Independent miss | Serial value cycle |
| --- | ---: | ---: | ---: |
| Zen5 AVX2 | 4.012 / 2.041 | 2.729 / 4.460 | 10.853 / 7.324 |
| Granite AVX-512 | 5.203 / 2.944 | 3.642 / 12.335 | 15.276 / 11.022 |
| V2 NEON | 6.340 / 3.451 | 4.259 / 16.155 | 17.275 / 11.062 |

Exact misses beat ordinary Abseil in 17/45 Zen5, 25/45 Granite and 29/45 V2
cases. Exact set/map erase beats it in all thirty x86 cases, but only ten of
thirty V2 cases. These counts describe measured medians, not significance tests.
Updates lose all 45 cases on every host; multimap append remains 3.67–7.83 times
ordinary Abseil across the three targets. The multimap baseline uses
`InlinedVector<word,1>`, which actually holds two words inline in this ABI, and
consumes all bag values. Thus two-thirds of these updates remain inline in Abseil
but create a candidate bag; the remaining third has the opposite allocation
pressure. The [captured update-path audit](evidence/formats-native/mm-update-notes.md)
retains this footprint tradeoff, mutation/proposal outlining, repeated resolution
and checked StreamPack admission. It establishes concrete profile targets, not
cycle attribution or a missing primitive API. Seven large set cases and
seven large map cases per host have lower phase-summed lifecycle cost; no exact
multimap case does. Lifecycle charges construction/reserve, build, update, erase
and release, excluding queries, inventories and Pool setup/drain. It cannot stand
in for individual read/write competitiveness.

The [complete timed-region audit](evidence/formats-native/codegen-audit.py) covers
2,880 hit/miss/serial/erase intervals per host. Actual Abseil find and value
consumers fuse: all 270 baseline read intervals per host contain no calls.
Candidate lookup and complete consumption fuse within the timed interval or one
whole-read-loop helper; only cold format initialization and exception calls remain
inside those helpers. There is no per-key lookup or per-value callback boundary
in the warmed read path. Candidate erase still calls `record_table::erase` per
key, with further multimap preflight/bag-release outlining; baseline erase retains
its metadata helper and external-bag deallocation calls. Fusion does not itself
prove an efficient dependency chain or identify the source of a timing gap.

Layout labels are not reliable rankings. K40 set exact/rows/tag-plane/tag-bits/
aligned have identical physical bytes and identical complete read-helper
instructions after address normalization on all three hosts. Yet Granite miss
medians span 4.934–5.576 ns. Those are placement/process controls, not five storage
formats. K40 map rows also show large independent-hit losses on x86: composed
rows are 8.128 ns on Zen5 and 11.448 on Granite versus exact planes at 4.012 and
5.203. The raw row control is 3.959 and 10.732 respectively; V2 composed rows and
planes are close at 6.354 and 6.340. These survive the lookup-consumer fusion audit,
but neither instruction counts nor this experiment establish a causal mechanism.
Direct arms replace bucket fields only; hashing, routing, headers and chains stay
common, while legacy/pow2-direct additionally changes view implementation.

One fresh process runs each shuffled case/arm; each repetition creates a fresh
Pool, and each timed read has a full matching untimed warmup. Independent values
and the entire serial graph are checked, including XOR of every multimap value.
Process placement, temporal drift and Abseil seed differences remain uncontrolled.
The tests do not establish cache residence, DRAM throughput, spatial-prefetch
behaviour or an ISA-only advantage. Staged memory and build populations are kept
separate in the receipts. The earlier [local diagnostic](evidence/formats-local/context.json)
remains an explicitly non-native smoke result; native evidence supersedes it for
these formats. No preset is selected from per-case minima.

## Admitted StreamPack bag append

The [native append receipt](evidence/append-neon/RECEIPT.md) compares checked and
owner-admitted append on V2, Zen5 AVX2 and Granite AVX-512. All six arms, 45
width/kind/geometry cases, nine phases and nine repetitions are retained:
21,870 rows per host. The four candidate arms have identical requested/backing
bytes, metadata and tier populations in every phase/repetition. Native complete
owner and physical/event-parity checks pass. Set/map types are unchanged controls;
only multimap bag acquisition substitutes the existing
`author::append_unchecked` after fixed/full/immortal owner admission.

Across the 15 multimap cases, median admitted/checked ratios are below (composed /
direct; lower is faster). These are descriptive ratios across cases, not a
workload-weighted score or significance test.

| Target | Build | Update | Phase-summed lifecycle |
| --- | ---: | ---: | ---: |
| V2 | 0.964 / 0.980 | 0.933 / 0.948 | 0.969 / 0.970 |
| Zen5 | 0.930 / 0.950 | 0.927 / 0.913 | 0.933 / 0.940 |
| Granite | 0.934 / 0.950 | 0.938 / 0.941 | 0.936 / 0.946 |

Actual clock intervals retain the same per-entry mutation-lambda boundary.
Native bag acquire removes checked alias/length/dispatch calls and lets the
compiler fold the unchanged zeroed temporary into destination zeroing. V2's
static frame shrinks from 16,624 to 176 bytes; x86 shows the corresponding large
to small stack reservation. This is static evidence, not cycle attribution or a
claim that the whole frame was touched. Abseil search fuses into its timed loop,
with vector slow-append helpers retained.

The improvement is modest and does not close the multimap gap: admitted update
still costs roughly 5.0–5.9 times ordinary Abseil at the median across cases, and
phase-summed lifecycle about 2.3–3.5 times. The workload ends with two or three
values per key and mainly exercises promotion; it does not measure long-bag tail
extension. Set/map aggregate controls are near parity, but individual anomalies
remain. V2 K16/A10 set update is 24.60 versus 8.75 ns in two unchanged-type arms;
the actual timed regions match modulo relocation and call the same helper.
That unexplained runtime difference limits individual-cell causal claims and is
retained, not normalized away. No production policy is selected here.

## Owner-wide read permission: modest gains, broad target still unmet

The matched native access cohort is complete on
[Zen5 AVX2](evidence/access-zen5/context.json),
[Granite AVX-512](evidence/access-granite/context.json) and
[V2 NEON](evidence/access-neon/context.json). Every host passed the complete
owner and independent guard/byte oracles and produced 36,450 measurements:
45 cases, ten arms, nine phases and nine repetitions. The frozen source manifest
is `0e9190e5694d3781524512389cdfe47fa11becbf9883b7a1669fe7a19283913b`;
[receipts and regeneration tools](evidence/access-native/README.md) retain all
policies, baselines, repetitions and codegen controls.

This comparison keeps exact arbitrary-fit layouts, contents and populations
fixed. It admits 16-byte fingerprint reads within a whole initialized bucket,
and/or rounds suffix loads to a native byte width when every slot has enough
remaining bytes. Active masks and suffix masks exclude neighboring data; writes
and damage bounds remain exact. This permission requires the whole unit to stay
stable under one owner/protection domain. It is not permission to read an
independently mutable neighbor. Guard tests cover threshold fallback, poisoned
neighbors, all active counts and mutation/refusal preservation. All eight
candidate arms have identical logical footprints and build populations.

Wider reads help some paths but do not recover broad parity. Each cell below is
exact / wider-both / ordinary Abseil ns per operation, for K40/A10 maps:

| Platform | Independent hit | Independent miss | Serial value cycle |
| --- | ---: | ---: | ---: |
| Zen5 AVX2 | 4.305 / 3.860 / 2.042 | 2.729 / 2.513 / 4.740 | 10.851 / 9.863 / 7.378 |
| Granite AVX-512 | 5.147 / 4.808 / 2.879 | 3.561 / 3.569 / 12.222 | 15.024 / 14.491 / 10.995 |
| V2 NEON | 6.050 / 5.850 / 3.491 | 3.866 / 3.735 / 16.046 | 17.259 / 15.530 / 11.111 |

Across all cases, fixed wider-both composed hits beat ordinary Abseil in 0/45,
1/45 and 3/45 cases on Zen5, Granite and V2 respectively; the wins are multimaps.
All thirty map/multimap serial comparisons and all 45 updates still lose on
every host. Wider-both misses win 20/45, 25/45 and 31/45. Its phase-summed
lifecycle wins fourteen large set/map cases per host, with no multimap lifecycle
win. The initial logical-memory comparison is unchanged. Multimap updates also
retain the candidate inline-one versus Abseil actual-inline-two allocation
asymmetry described above; this cohort is not the separate admitted-append test.

Relative to each platform's same-layout exact composed arm, the median
wider-both hit ratios are 0.957/0.985/0.981, and map/multimap serial ratios are
0.952/0.950/0.930. Wider suffix alone has little aggregate read effect; wider
fingerprints account for most of the observed serial direction. These are
fixed-policy descriptive medians, not significance tests or causal attribution.
Fingerprint widening changes loaded bytes in only 40/45 cases and suffix
widening in 27/45, so unchanged cases are useful controls. The
[geometry census](evidence/access-native/admitted-reads.csv) records those
boundaries and the few widened suffixes that cross another cache line.

The complete audit covers 1,800 hit/miss/serial/erase intervals per host.
Candidate lookup and full value consumption are fused in a whole-loop helper,
except 24 V2 intervals where they fuse directly into the timer. All 270 baseline
read intervals per host have no calls. Candidate erase retains its per-key
owner boundary. Thus a weak unfused read baseline does not explain these results.
Strictly identical complete read-helper controls occur in 153/147/150 comparisons
on Zen5/Granite/V2, yet their timing ratios span 0.910–1.229, 0.941–1.067 and
0.915–1.065. Small differences need that placement/process qualification.

No read policy is promoted from these results. The same steady-clock timing,
complete read warmup, shuffled fresh case processes, staged inventories and
phase-summed lifecycle limits apply as in the format cohort. Neither resident
cache level, DRAM throughput, spatial prefetch nor ISA-only advantage is
established. The broader performance target remains unmet.

## Paired caller reduction: intended codegen, limited table uplift

The same-source native comparison of borrowed-checksum and local-result callers
is complete on Zen5 AVX2, Granite AVX-512 and V2 NEON. Each host passed both
complete-owner checks and independent recipe guards, then ran all 45 cases,
four fixed arms (exact composed/Direct, ordinary/exact-key Abseil), two callers
and nine repetitions: 29,160 raw phase rows. Adjacent caller processes have
randomized order, and every repetition receives complete read warmup. Per-rep
logical bytes, geometry, populations and operation counts match. The
[retained comparison](evidence/reduction-native/README.md) includes all write and
lifecycle controls, source identities and actual-loop audits.

Local-result / borrowed median ratios across the exact composed cases are:

| Platform | Hit | Miss | Map/multimap serial | Update | Phase-summed lifecycle |
| --- | ---: | ---: | ---: | ---: | ---: |
| Zen5 AVX2 | 1.007 | 1.003 | 0.998 | 0.998 | 1.000 |
| Granite AVX-512 | 0.991 | 0.984 | 1.002 | 1.003 | 1.007 |
| V2 NEON | 0.961 | 0.966 | 0.996 | 0.990 | 0.998 |

Thus V2 independent reads improve modestly, while serial improvement is small.
With local-result callers, exact composed hits beat ordinary Abseil in 0/45,
0/45 and 4/45 cases respectively. Misses win 11/45, 21/45 and 31/45. All thirty
map/multimap serial cells and all 45 updates still lose on every host; fourteen
large set/map phase-summed lifecycles win per host. The broad target remains unmet.

The complete audit resolves 720 hit/miss/serial/erase regions per binary through
frozen-source DWARF callsites, including fourteen outlined timing wrappers in
each local-result x86 binary. Both candidate and baseline lookup/full-value
consumers fuse in their actual read loops. Native K40/A10 candidate serial
checksums move from memory to registers, but per-key owner/seed/base loads remain;
Zen5 timing is 11.001 → 10.951 ns, V2 17.286 → 17.245 ns. This change therefore
reached native code without producing the hoped-for broad serial improvement.

This is a whole-caller codegen comparison, not an isolated checksum-store cost.
For example, Zen5 ordinary Abseil K40/A10 miss changes 5.032 → 1.833 ns even
though its borrowed loop already keeps the checksum in registers; loop shape,
constant/table-shape hoisting and outlining change too. The
[codegen notes](evidence/reduction-native/codegen-notes.md) retain that distinction.
Process placement, Abseil seeds and temporal variation remain; no PMU causality,
cache residency or platform policy is inferred. Earlier access and later
prepared-owner source captures also differ, and are not matched controls for
this isolated caller-source comparison.

## Cached formats and byte-coordinate reads: still below the broad target

The combined prepared-owner screen completed on all three native hosts, with
complete owner, recipe-guard and link-reservation checks before timing. All
45 cases, ten fixed arms and nine repetitions are retained: 36,450 phase rows
per host. This combines cached link-format pointers and byte-coordinate field
access with the local-result caller; it does not isolate either change. Relative
to comparable paired-reduction cases, requested storage and populations match,
but metadata rises 8 B per set/map owner and 16 B per multimap owner. The
[complete evidence](evidence/prepared-native/README.md) charges that storage and
preserves all read/write losses.

Exact composed / wider-both composed hit wins against ordinary Abseil are
0/45 / 1/45 on Zen5, 1/45 / 1/45 on Granite, and 5/45 / 4/45 on V2. Their median
hit ratios are 1.717 / 1.540, 1.654 / 1.508 and 1.596 / 1.573 respectively.
Every candidate policy still loses all thirty map/multimap serial cells and all
45 updates. Wider-both serial median ratios are 1.419/1.386/1.282 against Abseil.
Fourteen large set/map phase-summed lifecycles win per host; no multimap lifecycle
wins. Initial logical memory beats ordinary Abseil in 10/15 set cases and 0/15
map cases on each host. These are fixed-policy descriptive comparisons, not
selection of the best arm per case.

Read lookup and full-value consumption fuse in all audited native loops, with
no per-key or per-value calls. Cached-format guard calls are absent too. The
phase audit resolves actual source/DWARF callsites, including 166/147/166 outlined
timing wrappers on Zen5/Granite/V2; it does not infer phases from clock ordinals.
No cache-residence, DRAM, ISA-only or hardware-cycle attribution follows from
these measurements.

Stable Zen5 anomalies merit a separate focused investigation. K40/A10 set exact
Direct hits take 6.998 ns versus composed 2.895; multimap exact composed takes
12.737 ns versus wider-both 5.039. All four actual timed loops have zero calls.
The slow loops partially define CL before using RCX as the next-key BZHI count;
the fast controls avoid that particular previous-query register dependency.
This is a falsifiable hardware-dependency hypothesis, not an established cause
or reason to select a wider-read policy. The
[complete loops and profiling addresses](evidence/prepared-native/codegen-notes.md)
preserve that lead, repetition ranges, and other codegen differences. A focused
profile and a fully defined count-register control were completed separately;
see the matched intervention below.

## Two inline 64-bit values help, but do not close short-bag mutation

The [native inline assessment](evidence/inline-bags-native/README.md) retains
169,128 rows across Zen5, Granite Rapids and Neoverse V2. It compares one inline
value, two in the same 128-byte bucket, and two in a larger bucket that restores
at least the original slot capacity, alongside ordinary and exact-key Abseil.
All native read consumers fuse; mutation retains an outlined per-key path.

The larger two-inline control lowers mixed-update time about 19–21% relative to
one inline value, but still costs 4.7–5.7 times ordinary Abseil. Its second append
is about five times faster than the one-inline control; the third append instead
costs 3.5–4.7 times as much. At the same 128-byte bucket size, fewer slots cause
substantial key overflow and lookup losses. Initial and post-append logical
memory move differently because the larger payload avoids some external bags.
The evidence retains both costs rather than selecting a policy from append alone.

These results cover eight-byte values. They do not answer how many narrower or
wider values should fit inline. The [value-width experiment](value-widths.md)
generalizes the implementation by payload byte budget and compares actual typed
Abseil bags; its measurements and scope are kept separate from this frozen panel.

## A confirmed cause of two Zen5 lookup anomalies

The [matched count-definition experiment](evidence/bzhi-control/README.md) changes
only the selected partial count-register definitions in otherwise matched code.
The padded partial-definition control leaves both slow cases unchanged. Full
definitions reduce K40/A10 set Direct independent hits from 7.036 to 2.786 ns
and exact multimap hits from 12.730 to 5.434 ns. Fast controls and ordinary Abseil
show no comparable gain. All 432 complete-operation runs pass their oracles;
geometry, memory and the remaining code are independently checked.

This supports a hardware dependency caused by the compiler's partial register
definition in those loops. It does not explain the broad lookup/mutation gap:
serial and complete mutation lifecycle remain largely unchanged, and the fast
set control still trails Abseil. The intervention is diagnostic binary rewriting,
not an implementation patch. A source-level lowering that preserves the gain
without adding masks, spills or caller overhead remains to be qualified.

## Width-dependent inline capacity exposes a separate mutation cost

The [native byte-width panel](evidence/value-widths-native/README.md) covers
two-, eight- and sixteen-byte maps and multimaps on all three hosts, with
complete correctness histories, 53,460 phase rows and 540 read-loop audits.
The implementation admits whole-byte values from one through sixty-four bytes
in the exclusive-writer compact-list and hierarchy owners; the multi-writer
driver still uses word values. An eight/sixteen-byte inline payload budget holds
four/eight two-byte values, one/two eight-byte values, and zero/one sixteen-byte
values. These budgets and fixed bucket geometries remain experimental controls.

Abseil's typed `InlinedVector<uint16_t,1>` actually holds eight values inline.
At K40/A10 with singleton two-byte bags, the candidate's eight-byte budget uses
0.517 times Abseil's initial logical memory and has near-parity independent reads
on Granite and V2. Yet appending a second value, which stays inline, costs
4.35/4.55/5.08 times ordinary Abseil on Zen5/Granite/V2. Comparing with exact-width
keys still leaves a 4.02/4.32/5.03 ratio. Avoiding first spill at higher fanouts
helps substantially but does not remove the existing-key mutation deficit.

All typed read consumers fuse; this original receipt does not establish mutation
fusion. The later [matched mutation receipt](evidence/mutation-native/README.md)
compares complete caller inlining with a scoped existing-record patch, keeping
routing, layouts, allocation and publication policies constant.
The panel also retains map losses, width-specific spill costs and a Granite
fanout-four read anomaly; it does not establish broad parity, cache residence,
an RSS saving or a selected preset.

The mutation receipt includes 178,200 phase rows and 2,160 audited read/update
regions. Existing inline writes benefit substantially from eliminating the
whole-record proposal, beyond removing per-key calls. W2/A10 singleton append
with the eight-byte budget changes from 17.67/22.21/33.59 ns to
5.93/7.43/8.05 ns on Zen5/Granite/V2. The transformed arm still costs
1.52/1.57/1.16 times its unchanged-source ordinary Abseil control. W8's second
inline value in a 192-byte bucket costs 1.41/1.41/1.15 times Abseil. W2 and W8
map replacement approaches parity, while W16 remains layout-sensitive.

Spill transitions receive little benefit: W2/B8's fifth value remains roughly
19–21 times Abseil, whose typed vector is still inline. Conversely, appending at
some larger fanouts wins when Abseil reallocates and the candidate already has
space. Those alternating boundaries cannot select a policy from one operation.
Larger inline budgets, bucket capacity, link/block allocation and full histories
must be selected together. Native checks preserve before-write damage, refusal,
packed neighboring records and retained-reader histories. The live spike now
combines fusion with partial inline multimap writes, retaining the fused
whole-value map path after its better x86 result. That combined implementation
passes all eleven width checks and the publication check; its next native
assessment is the [coupled bag-policy panel](bag-policies.md). The individual
control timings above are not substituted for a measurement of that combination.

## Coupled bag policy has native evidence, without a universal winner

The [coupled policy assessment](bag-policies.md) now qualifies 135,000 rows,
3,000 fresh processes and all 720 native read/update regions. It measures the
live fused mutation path across W2/W8/W16, larger inline budgets, bucket units,
shared blocks and direct grown buffers, including uniform and mixed fanouts.
Larger shared blocks reduce mixed writes substantially; the W8 B16/U192/256-link
control takes 0.44–0.51 times ordinary Abseil's update time across the three hosts.
Its complete lifecycle still takes 1.20–1.40 times Abseil and its initial logical
memory is 1.14 times Abseil. Wider inline records likewise move spill boundaries
while spending space or reducing bucket capacity. No preset is selected.

The matched untimed inventories show reduced allocation growth and increased
whole-block damage extents with larger blocks. These are logical observations,
not unique dirty bytes, traffic or actual MVCC copying. The finite panel remains
K40 at fixed geometry/cardinality. The [geometry study](geometry.md) now tests
the independent concern that those controls underfill narrower set/map layouts.

## Fixed-cardinality geometry recovers memory, while reads remain open

The [native geometry cohort](geometry.md) retains 116,640 phase rows across all
three hosts, with 36 exact timed binaries and complete report/codegen checks.
At the same 6,144-key history, examples include K16 sets at 0.720 times Abseil's
logical memory, K40 sets at 0.476, and K40/W2 maps at 0.502. Their complete
trade-offs remain in the report: the set examples still lose independent hits,
and crowded wide maps develop large overflow penalties. Geometry alone has not
met the broad target.

K40/W2 at A9/U128 has a 4.16–4.42× x86 hit outlier despite no linked overflow.
Inspection identifies a possible carried dependency from the previous checksum
through a partially defined BZHI count register; V2's ratio is 1.43 and the
value-dependent read ratios are about 1.1 on every host. This is a code-generation
lead, not a causal timing measurement for those loops. The separate selector
experiment must account for it before assigning any gain to fingerprint width.

## Wider selectors help narrow-key layouts; the largest gain is confounded

The [native selector comparison](selectors.md) retains 51,840 complete-operation
rows, 1,152 fresh processes and all 2,304 read/update regions on Zen5 and Granite.
Physical inventory remains identical across composed/direct and scan16/scan32.
At K16/W2 A9/U64, scan32 lowers independent-hit ratios against ordinary Abseil
from 1.380 to 1.139 on Zen5 and 1.314 to 1.027 on Granite, using 0.857 times
Abseil's logical memory. Value-dependent read ratios are 1.134 and 1.057.
Other layouts retain read, lifecycle and overflow losses; this finite panel
does not select a preset or establish cache-residence coverage.

K40/W2 A9/U128 hits change from 8.262 to 2.875 ns on Zen5 and 11.967 to 3.625 ns
on Granite. The actual machine code also removes the checksum-to-BZHI-count
dependency in both composed and direct paths. That prevents assigning this
gain to the wider selector. The full-width composed and direct loads lower to
the same memory-operand SIMD comparison in those loops, so replacing SeriesPack
with direct loads does not explain the large deficit. The
[source-mask comparison](hash-masks.md) tests a separate intervention with
ordinary 16-byte selector execution held fixed; no remedy is adopted yet.

That source comparison now has qualified native results on both hosts:
51,840 rows, 1,152 fresh processes and all 2,304 read/update regions. At the
same K40/W2 A9/U128 geometry, explicit split MOVL/BZHI lowers hits from 8.219
to 2.959 ns on Zen5 and from 11.985 to 3.860 ns on Granite; the paired form
reaches 2.969 and 3.931 ns. Already-fast map controls and dependent reads incur
small regressions, with larger individual regressions elsewhere in the retained
panel. A forced AND introduces new set-hit slowdowns approaching threefold.
The source remedies are diagnostic choices rather than adopted defaults, and
the improved map cases still trail ordinary Abseil by roughly 37–51%.

The targeted instruction review confirms removal of the original count
dependency in those map controls, while the AND set control exposes a separate
dependency in the query producer. Changed masks also alter register allocation
and mask folding; total selected-path instruction counts are slightly lower,
so extra masks alone do not account for small regressions. The next
[bucket consumption experiments](lookup-consumption.md) target common-path work
that remains after the anomaly is removed. Both are now default-off executable
controls. The inactive-fingerprint cohort has qualified native reports and all
7,560 read/update regions across 30 binaries on all three hosts. Its
gains depend on layout and operation and do not close the broad performance gap.
The immediate row consumer separately tests layout and reuse of one load for
suffix confirmation and value consumption, with guarded admission checks.

## Later paired Granite DRAM-sized control

The closed separated-layout spike also measured the preceding word-map with
57.6M keys, roughly 1.14 GB requested table storage, exceeding the measured
Granite host's 480 MiB advertised LLC. In that single repetition, actual Loom
C32 hit reciprocal throughput was **20.77 ns/request** here, **35.75 ns** for
Abseil's better direct/prefetch arm and **26.47 ns** for the separated candidate.
Serial direct hit consumers were **233.33 / 205.49 / 344.57 ns**, respectively.
Mixed miss/retry C32 throughput was **21.20 / 41.99 / 23.68 ns**.

[Paired full scorecard and source/residence receipts](SEPARATED-EXPERIMENT.md#recovery)
retain the separate footprint categories. This is one repetition of K40/W8
scratch word maps; it qualifies neither cold multimap throughput nor protected
mutations, and exceeding LLC does not prove every access reached DRAM.
