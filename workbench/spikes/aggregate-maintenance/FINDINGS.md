# First aggregate-delta findings

These findings cover the first probe. The [spike closeout](CONCLUSIONS.md)
combines them with the subsequent shared dirty-buffer comparison.

2026-09-07. The [first probe](README.md) supports a distinction we needed to
make early: **coalescing can remove a large amount of ancestor work while
still being an expensive way to maintain resident summary arrays.**
Location-addressed runs are a leaner correction representation than the
ancestor-addressed runs implemented here. Neither is a selected SixDB design.

## Evidence and scope

The retained [screen](evidence/local-arm-20260907/screen/summary.md) covers
14 scenarios × 4 methods × 3 repetitions. The longer
[confirmation](evidence/local-arm-20260907/confirm/summary.md) covers five
scenarios × 4 methods × 5 repetitions at a 0.2-second minimum measurement time.
Both receipts record the same source digest and unchanged source throughout
execution. Individual samples, counters, and compact provenance are in Git;
full run bundles, source snapshots, and logs are in S3 with verified references
in the [evidence index](evidence/local-arm-20260907/README.md).

This was Linux/OrbStack on an Apple ARM host, pinned to guest CPU 0, using
Clang 21.1.8, C++23, `-O3`, generic tuning, and Google Benchmark 1.9.4.
Guest affinity does not establish a fixed physical core or cache residence.
ASLR remained enabled. Some repetitions were noisy, particularly the tiny
single-key cycle and the eager read-heavy baseline. Small differences between
coalescing methods in the single-key case should not be ranked.

The default scenario has 65,536 keys, four interior strata above leaf
summaries, 16 spatial partitions, 16,384 mutations in batches of 256, four
queries per batch, and folding after four runs in an affected partition.
The query mix includes root, tiny, aligned, and broader non-aligned ranges.
Each batch is one visible state; historical readers are absent.

Every timed query is checked against a precomputed row oracle. The separate
checker passes 146,880 exhaustive range/batch-boundary expectations per method
in both accounting and counter-free modes, plus generated scenario checks.
AddressSanitizer and UndefinedBehaviorSanitizer checks also pass. All runs are
drained before a measured cycle ends.

## What the numbers say

Median full summary-cycle ns per mutation, including the configured queries,
folding, and final drain. These are **not isolated insert latencies**.

| Workload | Eager | Eager batch | Ancestor runs | Location runs |
| --- | ---: | ---: | ---: | ---: |
| Uniform locations | 6.3 | 143.5 | 162.3 | 152.9 |
| Hot 64-key range | 9.3 | 40.5 | 47.1 | 38.2 |
| Repeated single key | 11.0 | 7.7 | 8.3 | 8.2 |
| Read-heavy mix | 37.0 | 180.8 | 277.5 | 199.2 |

Values come from the [confirmation CSV](evidence/local-arm-20260907/confirm/summary.csv).
“Hot” changes key reuse and consequently the realised insert/update/delete
mix; it is not a controlled experiment holding mutation types constant.
Within each row, every method consumes exactly the same mutations and queries.

**Ancestor work can fall sharply.** For the uniform scenario, eager performs
65,536 interior-summary adjustments. Eager batch performs 27,339; both buffered
methods perform 18,970, a 71.1% reduction from eager. Leaf-summary adjustments
are accounted separately. Over the hot range, buffered interior adjustments
fall to 112, versus eager's 65,536. This is coalescing across shared prefixes
and repeated locations, including cancellation where the batch boundaries
permit it. [Accounting](evidence/local-arm-20260907/confirm/accounting.csv)

**Location addressing reduces delta representation and lookup work.** In the
uniform scenario, ancestor runs materialise 44,639 entries; location runs
materialise 16,340 entries plus 17,364 prefix-sum cells. Allocated pending
entry/prefix capacity peaks at 95,160 versus 32,504 bytes. These counters omit
some metadata and allocator overhead and are not peak RSS. Both methods make
376 run probes, but ancestor lookups perform 51,648 search comparisons versus
3,088 for location runs. Read-heavy queries expose the difference more clearly:
800,580 versus 48,796 comparisons, with a roughly 28% lower cycle time for
location runs than ancestor runs. [Counters](evidence/local-arm-20260907/confirm/accounting.csv),
[timings](evidence/local-arm-20260907/confirm/summary.csv)

**Less ancestor work does not yet repay construction cost in this baseline.**
The uniform location-run cycle is about 24× eager. Eager batch, which has no
pending-run query correction, is already about 23× eager. That points to the
sort/coalesce/expand/allocation path as the next attribution target; these
measurements do not isolate sorting from the other construction work.
The single-key case is the exception: all three coalescing implementations
beat scalar eager maintenance, even with their construction overhead.

**Buffer capacity has a measurable read/space price.** In the screen, increasing
the run threshold from 4 to 16 reduced location-run interior adjustments from
18,970 to 11,501. It increased run probes from 376 to 1,864 and peak pending
payload capacity from 32,504 to 157,736 bytes. Its median cycle also became
slower in that run. More coalescing is not automatically a better operating
point. [Screen counters and samples](evidence/local-arm-20260907/screen/summary.csv)

## What remains unresolved

The base is a resident array of fixed-size count/sum pairs. It has no compressed
containers, durability, WAL, replication, version publication, or real storage
write units. Precomputed deltas also exclude acquiring beforeimages and
modifying primary rows. Therefore the measured reduction in logical ancestor
adjustments is not a physical write-amplification result, and the resident-array
CPU result cannot reject buffering for durable SixDB structures.

This implementation also expands ancestors while folding location runs into
the base. We have reduced how often that happens, not removed that work.
`std::sort`, the run representation, and folding policy are straightforward
comparands rather than tuned kernels. No min/max repair or per-field NULL
semantics have been exercised. Retaining an older snapshot would restrict
the cancellation that makes some of these results attractive.

Two useful next investigations are independent of choosing the core structure:
attribute and reduce the batch-construction CPU cost; and introduce retained
snapshot boundaries to measure how much coalescing survives. Physical write
costs can subsequently be bracketed with explicit patch/container/page models,
then measured against candidate representations. These are directions opened
by the evidence, not new mandatory stages or a product milestone.

The [notebook](../../notebook/ideas.md#when-less-logical-work-costs-more-cpu)
connects this result back to the broader question of when less logical work
repays the cost of constructing a different representation.
