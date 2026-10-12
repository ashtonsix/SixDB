# Experiments and completion evidence

The objective is a measured design, not the largest menu of policies. Every
implemented candidate pairs independent state/invariant checks with timed useful
work, actual allocated-byte accounting, instruction inspection and ownership
costs. Failed performance hypotheses should change the design immediately.

## Comparisons

Pin dependency revisions, compiler/ISA flags and actual platform receipts.
Abseil `flat_hash_set`/`flat_hash_map` with their normal strong hash is the main
single-writer baseline. Include a common well-mixed full-width hash control where
semantics permit; handing Abseil a truncated quotient permutation would weaken
its own fingerprinting and is not a fair equal-hash experiment. Distinguish
algorithm/layout controls from end-to-end native-policy comparisons.

Add Folly F14 as a bucket/SIMD/overflow-counter competitor; packed integer tables
for 24/40/48/56-bit keys; worker-partitioned Abseil and strong shared concurrent
tables (Boost concurrent flat containers with bulk visitation, libcuckoo and/or
IcebergHT). For multimaps compare flat-map-to-inline-small-vector and contiguous
grouped-value baselines; node-based `unordered_multimap` alone is insufficient.
Join-specific ClickHouse/DuckDB layouts supply additional workloads/prior art,
not interchangeable mutable-map contracts. Report multimap memory even when no
like-for-like Abseil container exists.

Account for primitive payload, both bucket arrays, filters/counts, chain sidecars,
block directories, initialized spare lanes, allocator capacity/slack, owner
objects and scratch separately. Report live, retained high-water and rebuild peak
bytes per logical key/value pair alongside process RSS. Shared backing or matched
allocator controls isolate allocation effects; actual Orbital integration remains
an end-to-end comparison. Do not compare candidate modeled bytes with baseline
element capacity or conflate RSS with exact structure bytes.

## Workload coverage

| Axis | Required discriminating cases |
| --- | --- |
| Key/value | all 8 key widths; set and narrow/word/handle values; out-of-line payload when consumed |
| Cardinality | empty,1, small-list boundary, every bucket/growth transition, sparse through near-growth load |
| Distribution | uniform, sequential, power-of-two strides, shared low/high prefixes, Zipf hot keys, concentrated primary and conditional-secondary collisions |
| Lookup | hit/miss/mixed, primary/secondary/chain, hot subset amid large cold table, random vs correlated requests |
| Mutation | existing-value update, insert, missing/last/swap erase, balanced and burst churn, clear/refill, duplicate batch keys |
| Multimap | singleton-heavy, varied fanout, one extreme hot key, duplicate pairs, partial/full erase, complete value consumption |
| Lifecycle | fresh-seed growth, failure before install, repeated grow/shrink/clear, chain reclaim/compaction, old-reader retention |
| Concurrency | worker counts, affinity/skew, exception routing, queues, same-key conflicts, merge, retained readers and publication |
| Exposure | direct hot path; forced Ring; heat-directed mixed regime; batch 4/8; effect-free, bucket and exact publication |

Measure **dependent serial lookup** with the next request derived from the
previous lookup result. For misses use a genuinely dependent next-key rule
without adding asymmetric work. Separately measure independent loop throughput,
fixed batch 4/8 and actual Loom Ring throughput (including fill/drain, admission
and continuation state). Hardware gather does not substitute for a batch API
comparison. Mutation sequences check complete final state and deltas outside
timing, with matched work inside timing.

Use cache-scaled populations and actual access distributions for L1/L2, LLC and
large-memory regimes; retain cache geometry, page/NUMA placement, measured timing
regimes and PMU evidence where available. Capacity alone is not residence proof.
8/16-bit domains cannot create arbitrarily large single-table sets: use many
independent realistic small tables for large footprints and report table count.
Avoid a huge mostly empty directory solely to obtain a DRAM label. Distinguish
equal logical content from equal footprint controls; memory savings are an
intended advantage at equal content. Query/permutation buffers also consume cache.

Both architectures and vendors need native results: AVX2 and AVX-512 on Intel and
AMD, NEON on Neoverse, optional SVE2. Cross-compilation/emulation validates code,
not speed. Inspect the complete hit/miss/update/dependent code paths and use
llvm-mca for modeled scheduling; qualify its limits and verify mechanisms with
timing/counters. Do not claim broad advantage from selected favorable cells or
one geometric mean. Retain regressions, variation and Pareto comparisons.
Across seed ensembles and post-churn histories, retain primary/secondary/chain
fractions, chain-length tails, negative-filter pass rates and operation latency
tails. Average behavior from one favorable seed is insufficient.

## First experiments

1. Reproduce the permutation counterexamples and enumerate geometry/occupancy
   costs. Check reversible reconstruction across all splits, realistic secondary
   sizing and exact descendant accounting. These are models, not speed results.
2. Build a bounded composed bucket consumer and a matched fused control with
   explicit SIMD. Validate actual SeriesPack/TuplePack formats, shifted windows,
   empty/full/tail groups and source-qualified effects. Measure serial and
   independent work while choosing a small number of viable layouts.
3. Extend those bodies into the two-table/chain candidate with real allocation,
   caller occupancy, growth and churn. Start the Abseil comparisons immediately
   with this first complete table, rather than freezing a layout beforehand.
4. Run actual Loom dependency continuations at meaningful footprints and against
   competitor bulk/prefetch paths. Integrate StreamPack chains and multimap fanout
   early enough to change bucket/tag decisions. Evaluate routing/merge modes with
   strong baselines and full publication/lifecycle cost.

This ordering follows dependencies; no step is a correctness-only or performance-only
phase. An early composition failure may require a primitive change before a
table-body expansion. Independent reviewers challenge the initial design and
the emerging measurements; Ikea retains integration ownership.

## Spatial-prefetch work

Existing `memory-characterisation/probe.cpp` tests single-demand neighbours at
both parities, directions/distance, plus separate stream and page-boundary cases.
The old retained fleet observations are now historical diagnostics: Loom found
that conditional stimulus loads could execute speculatively before a trailing
fence and contaminate the controls. Both the old helper and runtime calibrator
have been corrected to fence at entry; corrected native captures are retained
in [the owning study](../memory-characterisation/ADJACENT.md).
Do not import the old adjacent/training results as qualified hardware facts.
The local VM timer also did not separate hot/cold controls adequately.

Loom delivered cold snapshot fields and bounded x86 missing-field calibration
within Ashton's one-hour requirement from 22:38 UTC on 2026-10-12. ARM remains
unknown. Native/local/sanitizer service checks and qualified protocol imports
are recorded in the owning study and [architecture account](../../../loom/docs/hardware.md).
Evidence for demand-triggered adjacent fetch does not automatically apply to
software-prefetched Ring accesses. Keep those histories separate. Unknown is a
usable result; test complete bucket consumers rather than blocking all design
until hidden hardware mechanisms can be uniquely identified.

## Current status

The new root/design and three independent reviews are complete; the supplied
critique has been assessed selectively in REVIEW.md. The logical model and first
bounded native composition consumer are implemented. Local Release/ASan/UBSan
checks pass, instruction bodies have been inspected and fingerprint scheduling
has been modeled. Native NEON/AVX2/AVX-512 captures are retained in FINDINGS.md.

No complete hashtable throughput or memory advantage has been demonstrated yet.
The two-table implementation, StreamPack chain/bag lifecycle, full competitor
suite, Loom/routed-writer integrations, cache/DRAM campaigns and final bucket/
batch decisions remain open. The next implementation work follows the measured
composition gaps; old experiments are not completion evidence for this task.
