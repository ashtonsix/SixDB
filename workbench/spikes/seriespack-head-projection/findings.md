# Shared H16 head projection

The isolated H16/u64 candidate improves all 122 dense whole-encode cases at
8,192 and 1,048,576 values on Zen, but has 25 AVX512 regressions at 256 values.
It is not installed. The large-array gain is worth pursuing; small-call cost
and independently strided performance remain part of that decision.

## Mechanism and comparison

The candidate shifts full u64 values by payload width W before narrowing to
owned 16-bit register values. Their high byte supplies head0 and low byte
supplies head1. The inline projection is enclosed by a non-suspending driver;
payload encoding remains its existing separate pass. Each head retains its
own pointer and stride. The driver crosses physical tiles only when both
head strides establish contiguity; otherwise it processes each actual tile.
Source extent, final head slack and effect reporting retain their contracts.

This revisits the [head-pass investigation](encoder/README.md)
with a whole-encoder overlay on fresh current source. It uses actual layout
tile sizes, not that earlier standalone diagnostic's fixed cell assumptions.
Work grain and physical tile size remain independent, as discussed in the
[composition design](../ikea-composition/design.md).

Zen job `20260911T005317Z-6996b154` uses capture
`eab5e70c92c79114090cc436c5854739a3d37f29cb9e6c3b28cf26e47f49dcec`.
It reuses the freshly built baseline from `20260910T234310Z-87cf647d`, with
all common benchmark, facade, comparator and check objects pinned. Baseline
programs relink byte-identically; only `native.cpp.o` changes in the library.
The rejected [ordinary materializer](../seriespack-range-execution/integration.md) is
absent from both versions.

Each size contains 61 affected H16/u64 descriptions and 16 unchanged Ikea
controls per x86 family, plus 98 Calico controls: 252 cases. CPU0 runs
before/after/after/before, with three sequential 30 ms repetitions per case
and no background uploads. Both labelled x86 families have the full
v4/VBMI/VBMI2/GFNI ceiling; this is not pure AVX2 evidence. The
[retained audit](evidence/head16-current-20260911/20260911T005317Z-6996b154/audit.json)
checks all 9,072 measured samples and 1,512 separate one-iteration preflights.

Before and after pass the full scalar/AVX2/AVX512 public guards, including
3,737,352 exact guarded read ranges per target. The additional H16 checker
passes 5,590 independent placements, 83,076 reads, 53,560 mutations and
36,400 exact checked/bound encodes on each x86 family and each version. It
checks independent head strides, partial extents, canonical slack and guard
boundaries. These establish correctness, not independently strided timing.
The timed bulk fixture places each head plane densely.

The frozen runner inherited an inapplicable ordinary-reader `local_validation`
description. The [metadata correction](evidence/head16-current-20260911/20260911T005317Z-6996b154/metadata-correction.json)
explicitly rejects that field as H16 evidence and identifies the actual checks
run. The original capture and receipt remain unchanged.

## Observed cost and limits

"Separated" means all six candidate samples lie on one side of all six
baseline samples; it is not a confidence interval. All cases, controls and
ordering edges remain in the
[paired table](evidence/head16-current-20260911/20260911T005317Z-6996b154/paired.csv).

| Values | Affected cases | Separated wins | Separated losses | Overlap | Median candidate/baseline |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 256 | 122 | 76 | 25 | 21 | 0.888 |
| 8,192 | 122 | 122 | 0 | 0 | 0.756 |
| 1,048,576 | 122 | 122 | 0 | 0 | 0.708 |

Every affected bulk/large case also improves on both ordering edges. All
61 AVX2 small cases improve with separated samples. All 25 small losses are
AVX512: LocalK43/H16 rises from 35.73 to 47.39 ns/call (+32.6%), LocalK44
from 33.64 to 42.16 (+25.3%), and LocalK51 from 35.34 to 40.39 (+14.3%).
The remaining losses are12% or less, mostly1–5%. This is not evidence for
an across-ISA small-count cutoff or a per-width exception list.

Unchanged controls also move. Small Calico LocalK26 encode is 29.2% slower;
large Calico LocalK16 is 17.8% slower; large unchanged AVX2 StripedK15/H8
encode is 5.9% slower. The
[category summary](evidence/head16-current-20260911/20260911T005317Z-6996b154/category-summary.json)
keeps those movements separate and does not subtract them from affected
cases or waive the small losses.

Native text grows from 3,051,595 to 3,085,310 bytes: +33,715, or 1.10%.
Read-only data grows by 960 bytes. The source changes by+96/−2 lines, without
new private headers. Incremental native compilation takes 67.00 seconds;
the baseline native compilation was not separately timed, so this is not
a compile-time delta. The
[worker-derived accounting](evidence/head16-current-20260911/20260911T005317Z-6996b154/code-maintenance.json)
includes complete function families and finds no changed non-encode
disassembled instruction bodies. That body check does not compare relocation
targets or data contents; whole-object hashes and correctness evidence remain
separate.

Inspection of the exact AVX512 K43 worker code finds the same typed caller's
six saves, aligned stack frame and partial-payload stack traffic before/after.
The complete 256-value case does not take the partial-payload path. Previously
inline head loops become an ordinary call to a six-scalar head helper with
three register pushes. That is a plausible fixed-cost contributor, not a
cycle attribution or an explanation of every outlier.

The [baseline comparison](evidence/head16-current-20260911/20260911T005317Z-6996b154/before-prior.csv)
and [candidate comparison](evidence/head16-current-20260911/20260911T005317Z-6996b154/after-prior.csv)
choose each layout's fastest Ikea family and the faster retained Calico
geometry at the same logical width. Calico's H0 adapter label still encodes
the full width using its own internal planes and 256-value representation;
these are whole-operation comparisons with different representations.
Only Calico prior controls were timed here; this table does not establish
the strongest available comparator at width56, where LocalPack also exists.
At 8,192 values, StripedK17 falls from 2.22× to 1.63× the sampled Calico prior,
StripedK23 from 2.28× to 1.67×, and LocalK56 from 1.90× to 1.42×. The head
projection improves competitiveness but does not close the remaining gaps.

## Follow-up discriminator

The next isolated run keeps both native libraries unchanged. It reuses the
casing fixture for five representative layouts: head-only LocalK16, residual
LocalK23, byte-body LocalK56, StripedK23 with T256 and StripedK28 with T64.
Counts257/8193 exercise partial final tiles. Head planes are dense, only
head0 is gapped, only head1 is gapped, or both are gapped; payload is dense.
One common caller is linked before/after, with actual strides and address
offsets recorded. These 80 cases test the joint-contiguity condition directly.

A separate 66-case small-input repeat uses the original byte-identical
benchmark programs: the 25 losses, matching AVX2 cases, nearby widths and
unchanged Ikea/Calico controls. It is a deliberately selected diagnostic,
not a fresh all-layout estimate.

Job `20260911T011534Z-e266add4`, capture
`dc21bfe17b231b0e5efec0b70a41257184d03dc6c4286e697ff27160bdf77d92`,
passes the [independent audit](evidence/head16-current-20260911/20260911T011534Z-e266add4/audit.json)
of 1,752 timings and 292 preflights. Both native libraries and the original
small-repeat programs are hash-identical to the dense experiment. The new
placement caller verifies full values, slack and foreign bytes before and
after timing; its reported extents, strides and tile counts are independently
checked. An earlier launch stopped at regex inventory validation before any
preflight or timing and is excluded. The corrected filters were checked with
both C++ extended and POSIX regex engines before resubmission.

The [placement results](evidence/head16-current-20260911/20260911T011534Z-e266add4/paired.csv)
contain 63 separated wins, six losses and 11 overlaps. All20 dense placements
improve, as do all 15 gapped AVX512 cases at 8,193 values. All six losses are
AVX2. LocalK23/H16 at 8,193 values rises from 0.1458/0.1480 ns/value when
only one head is gapped to about0.1657 in either candidate case: +13.6%/+11.9%.
With both heads gapped it rises from 0.1552 to 0.1656 (+6.7%). At257 values,
the two one-head-gapped K23 cases lose 6.4–6.7%; LocalK56 with both heads
gapped loses 2.4%.

That pattern is consistent with the joint-contiguity fallback sacrificing
coalescing in the still-dense child. It is not a cycle attribution. A later
revision could keep native groups of owned source words and let each head
sink split them according to its own placement. That would preserve separate
child admission and work grain, rather than introduce width exceptions.

The selected small-input repeat has 35 separated wins,23 losses and eight
overlaps; four losses are unchanged controls. The original K43/K44/K51
outliers do not repeat as separated losses: K43 now wins, while K44/K51
overlap. K61 instead loses 20.7%, and many 1–4% AVX512 losses persist. These
movements with identical programs caution against tuning to individual
outliers; they do not erase the recurring smaller losses.

The head candidate remains isolated. The next implementation probe simplifies
the shared trusted encoder call boundary against the original baseline,
without mixing in head projection, so its effect can be attributed separately.
Any later head adoption also needs the relevant GNR comparison described by
the owning head-pass investigation.
