# Locality/execution and contiguous demand

This private spike keeps Loom's native Ring and Ikea's formats intact. It tests
two Loom-owned choices at their appropriate boundaries: selecting direct versus
Ring for a caller cohort, and lowering a qualified contiguous memory demand to
inline hint classes. No worker service, address directory or new native ABI was
added. The captured readers, complete-byte answers, successor hints and allocator
bytes are shared by all variants.

## First campaign

Five repetitions on Neoverse V2 and Zen 5 passed the complete 744-window-cell and
1,512-execution-cell matrices. Each campaign qualified 33 CTest identities,
34 binary hashes and 24 native codegen replays. Original and derived consumer
hashes reproduce with the current controlled generator. The first native source
digest is `b64c401411d315a7154e2cc662c801f1e120ecee1651d8bedbd79d772c3bb07a`.

Reference large mixed C32 throughput remains strong. Values are median ns per
complete useful request, with all bytes consumed and result-dependent successors.

| Host | Kind | Bare direct | Hinted Ring |
| --- | --- | ---: | ---: |
| V2 | Set | 36.622 | 18.285 |
| V2 | Map | 38.782 | 24.277 |
| V2 | Multimap | 52.267 | 35.511 |
| Zen 5 | Set | 42.872 | 11.608 |
| Zen 5 | Map | 48.487 | 10.182 |
| Zen 5 | Multimap | 74.040 | 18.378 |

The contiguous endpoint L1 procedure is often competitive, but does not justify
replacing the original emitter. It duplicates the covered tail's physical line
for many short windows, and the prototype adds a 16,400-byte prepared shape table.
Some large V2 cases regress. The fixed L2 procedure loses severely in the Zen map
case (57.950 versus 10.182 ns for reference hinted Ring); it is rejected as an
unconditional pressure mapping. It was a fixed locality control, with no measured
pressure signal.

Window variants ran in fixed per-kind process order. Unchanged controls reveal
material drift: V2 large mixed set Abseil direct is 38.825 ns in reference and
52.131 ns in the endpoint candidate process. Close procedure comparisons from
this campaign are conditional. The follow-up alternates process order and pairs
pass IDs, preserving the direct, manual, unhinted Ring and Abseil controls.

## Execution policy and observation cost

The record is 56 bytes, owned by one caller cohort. Identity includes owner
incarnation, storage generation, query population and assigned worker/cache
domain. It records completed serial consumer cost, with full epochs and bounded
expiry. Neither issued hints nor pipelined reciprocal throughput creates cheap
serial evidence. Useful decisions, clocks and samples remain outside native
visits; product callbacks retain their existing ABI and hint path.

The ordinary noinline decision wrapper costs about 4.5 ns on V2 and 2.8 ns on
Zen for fresh evidence. This includes wrapper argument transfer and decision
execution. It is one outer decision per 256 useful requests in this fixture,
not a cost paid for each visit. Caller-known direct costs about 1.4 ns through
the same ordinary wrapper. The complete-operation timing additionally charges
epoch reads, sampling and selection.

Serial cost is a predictor, not a universal crossover rule: an ordinary loop can
already expose overlap, and native stage work changes Ring overhead. The tested
32/64/128-ns cutoffs are experimental policy inputs. The caller supplies legal
paths, useful independence and population identity; publication/ownership remain
with the operation. Loom owns the record and decision procedure, without querying
hardware, clocks or shared state in each native continuation.

Full sampling repeats 256 completed serial queries. It successfully selects
direct for reused queries, but its observation work and 32-batch reclassification
interval hurt changing and post-interference phases. Large mixed map at the
32-ns cutoff illustrates both effects:

| Host / phase | Fixed direct | Fixed Ring | Adaptive, full sample |
| --- | ---: | ---: | ---: |
| V2 / reused | 4.827 | 8.880 | 5.437 |
| V2 / interference | 5.396 | 9.018 | 8.810 |
| V2 / changing | 43.579 | 33.785 | 36.723 |
| Zen / reused | 3.242 | 5.512 | 3.621 |
| Zen / interference | 3.924 | 5.631 | 6.074 |
| Zen / changing | 51.785 | 18.680 | 21.945 |

Interference executes a real 128-MiB line-stride traversal before query timing.
It conditions history without identifying a cache serving tier. Generation,
domain and expiry phases exercise identity logic synthetically; they are not
publication or physical migration experiments.

## Bounded follow-up

The follow-up compares the original reference with unique-line L1 classes, and
the full sampler with a 32-query rotating serial probe and earlier recheck.
Five process passes alternate candidate order. All three kinds retain hot
C1/C32 and large C32 full graphs, all-hit and mixed inputs, and complete policy
mode/phase/cutoff controls. No timings from the first campaign are pooled into
these comparisons.

Both contiguous implementations use the same 16-byte plan, qualified full
requested extent and explicit eight-line prefix cap. Empty plans are inert.
The optional class identity prevents LLVM suffix merging without emitted
instructions or register/memory clobbers. Fixed class bodies have no calls,
stack traffic or conditional branches. Branchless selection still has a cost:

| Standalone identity-controlled dispatcher | ARM instructions through dispatch | x86 instructions through dispatch |
| --- | ---: | ---: |
| Endpoint classes | 8 | 6 |
| Unique physical-line classes | 25 | 21 |

The unique emitter issues one hint per physical line in classes one through
seven. The largest class can still duplicate the tail for an eight-line
intersection, with at most nine hints. The full consumer measures whether
saved hint volume offsets the selector work. The follow-up retains actual
window sizes and counts, so use of the prefix cap can be assessed explicitly.

Zen's follow-up qualifies all 372 window and 3,024 execution cells, with five
passes / 120 processes, 27 checks, 28 binary hashes and compilation targets,
12 codegen replays, both consumer derivations and all 3,383 launch/source files.
Its source digest is
`e070cc00745277a97a2d64cafd3ecc50d43092b933eece24d37add35711535ce`.

The unique-line procedure loses every generic C32 hit/mixed cell on Zen. Each
mixed cell is slower in all five paired passes. Corresponding generic unhinted
controls are mostly within 3%; some Abseil controls still drift materially.

| Zen mixed C32 | Reference ns | Unique ns | Median paired unique/reference |
| --- | ---: | ---: | ---: |
| Hot set | 8.266 | 9.048 | 1.103 |
| Hot map | 5.183 | 6.155 | 1.188 |
| Hot multimap | 11.817 | 12.399 | 1.052 |
| Large set | 11.601 | 12.147 | 1.046 |
| Large map | 10.165 | 11.301 | 1.113 |
| Large multimap | 18.110 | 19.961 | 1.102 |

Actual maxima are 64 bytes for set, 128 for map and 518 for multimap. The
512-byte cap lowers 67 hot / 35,972 large multimap windows in each hit/mixed
audit per pass. Sizes/counts are stable across passes. Starting alignment was
not inventoried, so this does not count exactly which final physical lines
were omitted. The cap and selector overhead are separate concerns; the set/map
regressions occur without cap lowering.

At the 32-ns cutoff, smaller sampling brings reused and post-interference map
work close to direct, while reducing the charged observation work:

| Zen large mixed map phase | Full adaptive ns | Budgeted adaptive ns | Budgeted direct ns | Budgeted Ring ns |
| --- | ---: | ---: | ---: | ---: |
| Reused | 3.636 | 3.383 | 3.284 | 5.457 |
| Interference | 6.452 | 4.341 | 4.243 | 5.845 |
| Return | 6.409 | 4.154 | 4.503 | 5.582 |

Reused work retains 63 direct / one Ring batch in both variants. Samples fall
from 768 to 96 completed queries; median charged sample time falls from 0.258
to 0.038 ns per useful request. Interference changes from 31 direct / 33 Ring
to 64 direct / zero Ring batches, with 768 versus 64 samples and 1.681 versus
0.039 ns charged sample time. Return uses 62 direct / two Ring batches with
median 128 sampled queries. Cadence persists across the trace: a still-fresh
prediction can cover the first batch after interference before another probe.
It supplies no guarantee about that batch's latency.

Changing work at cutoffs 32/64 retains Ring in the median; sample charges fall
from roughly 4.2–7.4 to 0.9–1.4 ns per useful request across the three kinds.
Some adaptive totals are lower than fixed Ring, but sampling also conditions
the current work. Internal mode order is fixed because each pass process has
raw repetition zero. Outer candidate order alternates. These comparisons do
not identify a causal selection benefit for changing work.

The 128-ns cutoff is a concrete counterexample to universal cheapness thresholds.
Large mixed map spends 30 direct / 34 Ring batches in every budgeted pass and
costs 35.724 ns versus fixed Ring's 19.129 ns. Full sampling costs 41.735 ns.
Hot changing map similarly costs 12.389 ns adaptive versus 7.364 ns Ring.
Serial cost alone cannot establish the winner of the C32 ordinary loop.

The V2 on-demand rerun passes the same complete qualification and source digest.
Budgeted sampling also brings reused and post-interference work close to direct:

| V2 large mixed map phase, cutoff 32 | Full adaptive ns | Budgeted adaptive ns | Budgeted direct ns | Budgeted Ring ns |
| --- | ---: | ---: | ---: | ---: |
| Reused | 5.433 | 5.119 | 5.030 | 9.197 |
| Interference | 9.716 | 5.986 | 5.828 | 9.480 |
| Changing | 38.238 | 35.720 | 45.437 | 35.225 |
| Return | 9.899 | 6.642 | 6.093 | 9.479 |

Across large mixed set/map/multimap changing work at cutoff 32, budgeted adaptive
costs 37.713 / 35.720 / 52.705 ns versus fixed Ring's 37.404 / 35.225 / 52.360 ns.
The remaining overhead is below 0.5 ns per useful request in these medians.
This is a complete-operation comparison with probes charged, not a per-visit
instrumentation result or a guarantee for another population.

The reference full-graph gains also remain present in the complete V2 rerun:
set/map/multimap hinted Ring costs 19.819 / 26.506 / 38.547 ns versus bare direct
37.325 / 38.569 / 57.056 ns. These are the rerun's own controls, not values pooled
with the first Spot campaign.

The useful architectural result is an owner-boundary policy with explicit
identity, legal paths and charged observations. The smaller sampling mechanism
is worth retaining, with a consumer-qualified cutoff and the stated latency/
heterogeneity limits. It adds no clocks, shared observer or metadata reads to
native visits. It does not justify a universal heat threshold.

V2 has composition-specific unique-line wins: hot mixed multimap improves from
21.378 to 20.880 ns in all five pairs (median paired ratio 0.982), and large
all-hit map from 15.848 to 15.161 ns (median ratio 0.936, one pass loses).
Other V2 mixed ratios are 1.087 / 1.105 for hot set/map and 1.003 / 1.050 / 1.040
for large set/map/multimap. Unchanged controls also move, so small wins are
conditional. The complete panels retain these exceptions alongside the losses.

The contiguous extent vocabulary is separate from its lowering procedure. The
general unique-line selector is rejected: removing hint duplicates does not
consistently pay for its arithmetic and dispatch. The existing L1
emission and Ring implementation remain the product path. Simple endpoint classes
remain an experimental control; these results do not establish a faster general
bulk-hint replacement. No product API has been promoted by this spike.

## Recovery

The selected evidence keeps every timing cell and repetition used above,
including negative candidates, logical work and compact receipts. Full source,
binaries, assembly, compilation databases and before/after page/NUMA mappings
remain in verified worker archives. No PMU, IPC, stall or miss-occupancy attribution
is claimed.

| Campaign | Worker job | Evidence |
| --- | --- | --- |
| First V2 | `20261015T011343Z-52981467` | [controls and receipts](evidence/initial-v2-20261015/artifact.json) |
| First Zen | `20261015T011343Z-a572718d` | [controls and receipts](evidence/initial-zen5-20261015/artifact.json) |
| Follow-up Zen | `20261015T020256Z-7d47a3a3` | [alternating controls and receipts](evidence/followup-zen5-20261015/artifact.json) |
| Follow-up V2, on-demand | `20261015T022051Z-14c95fc1` | [alternating controls and receipts](evidence/followup-v2-20261015/artifact.json) |
| Interrupted V2 follow-up | `20261015T020256Z-2168569e` | [failure and recovery reference](evidence/interrupted-v2-20261015/artifact.json) |

The V2 Spot follow-up was interrupted after 76 completed scheduled processes.
Its partial timings are excluded from the complete comparison. The rerun uses
the identical frozen source on on-demand capacity.

Release and O3 ASan/UBSan with always-UAR checks cover independent line coverage,
admission refusals, evidence identity/expiry, every bounded-probe group and actual
composed answers. [Primitive assembly and qualification](evidence/primitive-20261015/artifact.json)
retain the class-body/selector proof. The current source also rebuilds and passes
the self-contained primitives and representative recovered-input consumers after
the fixture-input plumbing was separated from Ikea's uncommitted work.
The Loom-owned consumer checker independently reproduces all 72 native assembly
receipts; positive and mutation controls also exercise its refusal paths,
including Python's optimized mode.
