# Boundlist findings

This records the closed prototype study. Subsequent authorized production work
and qualification are tracked in [IMPLEMENTATION.md](IMPLEMENTATION.md) and
[GRADUATION.md](GRADUATION.md).

The useful result is a reusable metadata/search layer with preserved StreamPack
storage, plus explicit checkpoint-to-local-window composition. It is not a new
format that wins every reader. General conditional-select/SIMD-window search is
strong; small full scans, batching and cold staged execution have distinct roles.
The [recommendation](RECOMMENDATION.md) selects that small set.

## What the numbers mean

Native hosts are Zen 5 (`c8a.large`), Granite Rapids (`c8i.large`) and Neoverse V2
(`c8g.medium`), with pinned Clang 21.1.8 and loop/SLP vectorization disabled.
Both x86 hosts run separate AVX2 and AVX-512 profiles; ARM uses explicit NEON.
This panel does not qualify Neoverse V3 or SVE2.

**Latency** is ns/request in one result-dependent chain. **Reciprocal throughput**
is ns/request across independent chains, not batch completion latency. Tables
state concurrency explicitly. Values are medians of at least three repetitions;
the [matrices](evidence/native/README.md) retain minima/maxima, counts, controls,
losing cells and source/host/raw recovery. Memory columns describe their stated
operands; logical bytes, readable grants, allocated capacity and process RSS are
not interchangeable.

## Small SIMD scans and convergence

Prepared-query search, 256 positive u16 endpoints, one hot list. This isolates
search from query generation and child consumption. Each pair is **serial ns /
eight-chain ns per query**.

| Host / ISA | Full SIMD count, fixed 256 | Select + SIMD window | Interleaved select + window | Paid Direct guide + local scan |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 AVX2 | 11.69 / 2.94 | 12.82 / 5.21 | 12.81 / 6.00 | 16.21 / 3.04 |
| Zen 5 AVX-512 | 10.40 / 2.36 | 10.40 / 4.03 | 10.38 / 5.17 | 8.88 / 7.07 |
| Granite AVX2 | 12.49 / 4.49 | 16.02 / 5.89 | 16.04 / 7.38 | 16.90 / 4.67 |
| Granite AVX-512 | 12.95 / 6.34 | 12.30 / 4.33 | 12.26 / 6.10 | 12.17 / 10.09 |
| Neoverse V2 NEON | 18.55 / 9.60 | 22.37 / 7.66 | 22.36 / 5.73 | 18.61 / 3.86 |

The guide adds one checkpoint per 16 endpoints: **6.25% extra endpoint storage**
at the same lane width, before any alignment. Its throughput benefit is not
portable. On NEON the complete query/child/hash consumer takes 21.93 / 5.13 ns
with the guide; fixed-256 full count takes 21.74 / 10.34 ns. The rank-replay
envelope alone takes 8.24 / 1.34 ns. These are separate controls, not costs that
can safely be subtracted to manufacture a kernel timing.

Full count uses vector accumulation and one final scalar reduction. The earlier
per-vector mask/branch scan left much more reduction work. This mattered on NEON,
but does not make a flat scan scale: at N=1024/4096/16384 the NEON count scan's
prepared-query throughput is approximately 42/175/708 ns, versus 7.3/8.4/9.6 ns
for select/window convergence. The sweep also includes u32/u64 endpoints,
duplicates and skew, rather than selecting from the u16 case alone.

The tested one-lerp/interleaved/select/window sequence loses. At N256/u16 its
eight-chain search costs 11.75 ns on NEON and 15.68 ns on Granite AVX-512,
versus 5.73 and 6.10 ns without lerp. Wide integer division/setup and broad
correction erase the probe saving. This rejects that implementation as a default;
it is not a result about every error-bounded interpolation algorithm. Beginning
gallop and quarter-pivot controls are also retained; caller-hint galloping was
not qualified.

## First and last overlapping ranks

The logical-keyspace consumer prepares a query, resolves both extrema boundaries,
and consumes the first/last 64-byte child descriptors with feedback. Segments
have four ordered records, repeated logical keys, gaps and independent population.
The following slice uses N256, duplicate repeat 8, sparse population and u16
coordinates. These are **complete consumer** ns, not the search-only table above.

| Host / ISA | Select/window serial | Select/window eight-chain | Fused binary serial | Fused binary eight-chain | Fused scan serial | Fused scan eight-chain |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Zen 5 AVX-512 | 24.51 | 19.08 | 36.69 | 33.23 | 31.04 | 27.45 |
| Granite AVX-512 | 31.30 | 23.37 | 44.23 | 39.82 | 37.84 | 35.04 |
| Neoverse V2 NEON | 44.28 | 36.00 | 57.62 | 53.16 | 61.91 | 61.21 |

The range matrix also covers all endpoint policies, unique point consumers,
u32/u64/128-bit coordinates, repeated runs 1/8/64, 1024 scattered lists and
N4096/N16384. A range window can contain holes or bound segments whose internal
records do not match; the owner still filters locally. Equal extrema do not imply
an empty segment. Full-key uniqueness does not make a truncated prefix exact.

The generic 128-bit convergence window is still a scalar ordered fallback. In the
same NEON slice it takes 81.35 / 75.97 ns versus the independent binary control's
73.68 / 74.42 ns. Its complete point consumer takes 46.39 / 37.32 ns. A native
lexicographic SIMD control exists, but this spike does not establish that every
wide domain has an optimized native production path.

Physical reservations use a separate adapter. It clamps to the storage extent,
distinguishes zero reservations from retired storage, and skips zero reservations
when live-successor correction moves a boundary. Its checked source does not
need a second positive-width bitmap. The geometric range matrix retains current
StreamPack and both paired/separate candidates. Pairing is **not** a universal
win: for W8 NEON, same-group paired range costs 50.33 / 40.40 ns, separate range
44.25 / 27.87 ns, current StreamPack 47.68 / 32.19 ns and the independent fused
control 40.28 / 25.48 ns (N256, one list, span257). The paired body widens a
prefix that the current inverse path handles narrowly; do not promote it.

## Extracting metadata without a payload

The compatibility adapter reads existing Direct/Width bytes without requiring
a payload binding. Bounds and U may have separate ownership while retaining
grouped or separated physical placement. Complete readers include actual value
loads, dependent successor feedback, ordered/shuffled projection and carried
traversal; query and value consumption remain timed.

Representative hot Width/W8 grouped R/U, 256 assigned ranks, U present and a
stored tombstone plane (all values live in this timing slice). **Current / bounds
+ U composition** in absolute ns:

| Host / ISA | Serial value chain | Eight-chain ns/value | Shuffled projection ns/value |
| --- | ---: | ---: | ---: |
| Zen 5 AVX2 | 6.56 / 6.61 | 1.39 / 1.31 | 1.26 / 1.58 |
| Zen 5 AVX-512 | 6.63 / 6.84 | 1.38 / 1.30 | 1.34 / 1.64 |
| Granite AVX2 | 7.51 / 7.48 | 1.80 / 1.89 | 1.89 / 2.04 |
| Granite AVX-512 | 8.37 / 8.35 | 1.75 / 1.78 | 1.87 / 2.05 |
| Neoverse V2 NEON | 10.62 / 10.63 | 2.24 / 2.02 | 1.92 / 2.10 |

Point extraction is close to cost-neutral here, but the flattened adapter loses
some projection efficiency. Across the large complete-cycle panel's 16 variable
schemas, median serial ratios are within about 4% of current StreamPack; eight-
chain ratios are within about 7%. Individual losses remain: Zen shuffled
projection is about 11% slower at the median, and its worst deltas reach
4.50 ns/value (AVX2) and 3.71 ns/value (AVX-512). Preserve compact existing
access bodies in the production extraction instead of adopting the adapter
unchanged. These observations do not establish a new writer speedup.

An earlier short prefix over a large allocation produced unstable residency
and up to fourfold variation in some dependent samples. The final large-reader
panel traverses complete 1,048,576-record cycles. It supersedes those misleading
medians. The retained matrix reports logical payload and allocated capacity
separately, plus metadata and binding bytes.

## Cold execution through actual Loom and Orbital

Fixed-256 point lookup plus child consumption. Each cell is **current Width /
staged Width**, in ns/request; concurrency remains explicit.

| Host / ISA | Serial | Direct eight-chain | Loom hints, 16 requests | Manual hints, 16 requests |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 AVX2 | 469.89 / 464.64 | 133.23 / 104.70 | 41.71 / 32.78 | 39.99 / 31.58 |
| Zen 5 AVX-512 | 466.87 / 464.39 | 131.93 / 100.04 | 41.76 / 32.96 | 39.97 / 31.67 |
| Granite AVX2 | 526.75 / 496.64 | 131.13 / 112.68 | 65.73 / 40.57 | 60.23 / 38.27 |
| Granite AVX-512 | 522.00 / 488.82 | 129.53 / 104.84 | 64.16 / 39.81 | 59.16 / 37.99 |
| Neoverse V2 NEON | 384.64 / 384.64 | 98.42 / 82.04 | 61.05 / 29.27 | 54.96 / 27.62 |

Fixed-256 range lookup plus both selected children, again **current Width /
staged Width** ns/request:

| Host / ISA | Serial | Loom hints, 16 requests |
| --- | ---: | ---: |
| Zen 5 AVX2 | 477.62 / 476.27 | 50.17 / 36.88 |
| Zen 5 AVX-512 | 478.70 / 473.21 | 49.72 / 35.94 |
| Granite AVX2 | 549.54 / 517.84 | 80.61 / 55.45 |
| Granite AVX-512 | 531.73 / 513.70 | 80.03 / 51.98 |
| Neoverse V2 NEON | 397.10 / 395.87 | 84.67 / 44.72 |

The throughput gain does not imply a universal serial gain. With mixed assigned
counts (64–256), NEON point latency rises from 279.30 to 328.86 ns,
while Loom-16 throughput improves from 49.11 to 31.58 ns/query. Preserve
hot/serial bodies and let cold consumers select staging through composition.
For context, fixed-256 NEON Loom-16 point lookup costs 94.20 ns through current
Direct and 99.98 ns through the independent full-count scan, versus 29.27 ns
through staged Width. The compact local target avoids prefetching/scanning an
entire endpoint array. Paid Direct guides can approach this result but have
additional storage/update costs. No new guide format is selected here.

The cold fixture has 524,288 independently addressed headers, 256-rank endpoint
and width storage, and a shared opaque child pool. The long run issues 2,097,152
queries per repetition, three repetitions, and tests fixed/mixed assigned counts
and point/range consumers. It measures real Loom allocations and Ring execution,
not a replacement scheduler. The timed interval includes task bootstrap, useful
work, refill and drain; setup/close and frame/scheduler storage are separate fields.

Both alternative stores are materialized in this control: 256 MiB endpoints,
128 MiB widths, 32 MiB headers and 128 MiB children, **544 MiB total operands**.
Do not describe the process RSS as the storage cost of only the winning format.
The random operands exceed the NEON/Zen5 workers' reported LLC capacities, but
not necessarily Granite's: its qualification receipt advertises480MiB L3, while
the Width operand upper bound is288MiB and Direct416MiB. The combined544MiB
includes an unused alternative store. These Granite cells are large random
consumers with unclassified LLC/DRAM service, not demonstrated DRAM residence.
Size and generic PMU counters do not prove individual load service; short reused
traces are not a basis for that claim.

The original broad scan control received only a first-line hint. After consulting
Loom, the final comparison explicitly covers each intersected native cache line
inside the qualified read window, binding a one-byte irregular demand once.
A demand size chooses a hint; it does not promise that many bytes of coverage.
Hints do not grant reads or prove readiness. Manual and Loom results remain
separate, including scheduler overhead and cases where hints/staging lose.

## Memory choices

No new representation is needed merely to extract ownership. For the current
StreamPack mortal 256-rank control with u16 offsets/checkpoints, the logical
metadata totals below include a 32-byte D plane and count/extent controls, but
exclude the shared eight-byte schema, allocator padding, payload and reader
bindings. They are format accounting, not measured RSS.

| Reservation maximum / U policy | Direct | Width |
| --- | ---: | ---: |
| 255 / omitted U | 546 B | 326 B |
| 255 / U present | 802 B | 582 B |
| 65535 / omitted U, 65535-byte block limit | 546 B | 582 B |

Thus Width is useful for small widths, not universally smaller. With only 1–16
assigned W8 ranks its first group costs 86 B versus Direct's 546 B fully reserved
directory; W16 costs 102 B. This distinction is compatible with retaining the
small fixed directory up front and growing width/used groups. Fixed reservations
can omit the geometry arrays altogether.

SeriesPack Local endpoint experiments cover K8/12/16/24/32/40/48/64 through
complete point consumers, with native arrays and independent bit encoding/fused
controls. They trade footprint against decode/search work and do not justify
making packed endpoints the universal representation. The complete selected
packed matrix is retained, including losses.

## Correctness, lifetime and diagnosis

- Independent linear/record/geometry oracles cover repeated bounds, dead/empty
  ranks, N0 through partial/full groups, G8/16/32, W8/W16, large origins, all four
  endpoint policies and query-domain extremes. Exact-tail guard pages and
  ASan/UBSan checks pass. Ordered strings, 128-bit points and a virtual top fence
  establish that comparison does not require a 64-bit metric domain.
- Actual Loom/Orbital material checks pass **192 histories** crossing Direct/
  Width, grouped/separated R/U, allocation placement, width limits, full/used,
  mortal/immortal and fixed recipes. They exercise append across 15/16/17,
  replacement, retirement, soft packing, selected reorder/remaps and coherent
  holder repair. Fresh, late and retained readers remain coherent after Store
  drain/reset; shutdown releases Arena charges. Effects replay to the original
  source bytes. Full-copy publication is the control, not selective-sharing proof.
- Read protection includes full initialized native tails. Appending an unassigned
  rank can still modify bytes in an old reader's group load. Issued-write effects
  include stored preserved neighbours; a temporary read projection is not a new
  persistent source identity.
- Linked-consumer audits reject ordinary outlined helpers; explicit cmov/csel
  was necessary when the compiler reintroduced branches. The first outlined
  timing panel is diagnostic only. The NEON fixed-count witness has 101 static
  instructions/109 uops and llvm-mca models 18.2 cycles reciprocal throughput;
  that is a bounded model, not a native end-to-end timing. Large independent
  bodies also reveal register pressure/spills. Native PMU diagnostics accompany
  search; unsupported events are reported rather than invented.

The remaining implementation work is the actual module move, standalone
admission/effects/public API qualification and regression measurement. New
keyspace split/merge/rebalance costs, complete multi-level directory policy and
selective metadata sharing were **not** established here; Engine's directory
study owns the former. The sampled workloads do not prove universal cache-level
residency or mutation competitiveness. These limits bound the recommendation,
rather than justify promoting every experimental option.
