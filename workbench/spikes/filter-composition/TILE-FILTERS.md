# Bounded SeriesPack tile filters

15 October 2026. Native eight-row comparisons and byte-aligned payload admission
use the existing SeriesPack window operation. The [K1–64 table](seriespack-tiles.md)
compares all five predicates against independent fused kernels. The selection
and final selected-policy cohorts passed all five native profiles. First-match
alternatives and the owner-bounded GFNI contains trick are measured separately.

## Contract and implementation

One existing `filter<Op>(source, first, count, rhs, active)` call returns one
original-position mask for 0–64 values. Complete 8/16/24/…/64 windows beginning at
a multiple of eight use bounded parallel comparisons. Narrow Local bitplanes
operate directly on row-mask bits; larger bodies use explicit AVX2/AVX-512/NEON.
This is one API operation, not a promise of one machine instruction. Logical
edges shorter than eight rows retain scalar evaluation. Query operands retain
full uint64 meaning; activity outside the requested count cannot select a bit.

The author entry `composition::filter8_unchecked` follows actual substituted
leaves, heads and Local/Striped residual geometry. A nonempty complete window can
read its occupied fields; it cannot borrow another tile or initialized bucket
padding. Empty activity reads no source. The existing reconstructed-value/CPS
carrier remains sixteen rows; no extra scheduler or carrier ABI is introduced.
The opt-in head guard also handles eight-row groups, preserving the decision to
skip a payload when no active head ties.

Payload origins have byte alignment through ordinary view admission, the dense
decoder and mutation bindings. Full occupied final tiles, stride, extent overflow,
overlap and write coverage remain checked. Striped payload stride remains a
multiple of 32 bytes. A complete Local headless N-slot payload uses N*K/8 bytes.
The old 64-byte origin rule was an admission restriction, not an aligned-load
requirement in these kernels.

The window traversal explicitly expands its maximum four sixteen-row groups,
with bounded eight-row and scalar edges. The previous mixed-step while loop
survived optimization even when first=0 and count=24 were constants. A K16/N24
NEON standalone consumer went from 63 to 30 static instructions, against 28 in
the first independent fused control (32 in the subsequently strengthened control). That observation is code generation, not a timing
claim. Ordinary activity is applied once at the window boundary; guarded groups
retain activity for their payload-access decision.

Native8 compares a body and residual separately and combines their masks. This
avoids promoting every value to the next word width just to compare it. The selected complete16 lowering keeps direct Local comparisons on AVX2.
AVX-512 retains reconstructed complete16 groups: the matched control showed
better reciprocal throughput across most odd widths. NEON uses direct equality;
ordered comparisons retain reconstruction above K40 and for six/seven residual
bits below K33. This is an internal decision, not a consumer hint. Narrow
bitplane, standard-word, substituted and guarded paths retain their own lowering. Contiguous
standard-word dense AVX-512 sources also use exact masked tail loads where a
larger vector fits: 24 u16 values need 48 readable bytes, not 64.

## Measurement design

`tiles-native.sh` runs Zen5 AVX2/AVX-512, Granite Rapids AVX2/AVX-512 and Neoverse
V2 NEON. Pinned Clang21.1.8, O3, no compiler loop/SLP vectorization. K=1–64,
N=8/24, byte offsets0/1/31/63, full/sparse activity, equality/less-than throughout;
the final panel adds all five predicates at N8/offset1. It also uses the actual
B64/B96 Iceberg–Swiss key geometries (N=8/16/24/32/40), including alternating line
offsets0/32 for B96. These are headless Local timings. Heads, substitutions and
Striped geometry have correctness coverage, not a claimed performance sweep.

Each arm executes the complete mask-to-next-address consumer. Serial latency
has one result-dependent stream; reciprocal throughput has eight independent
states with different salts. Sixty-four windows are repeatedly warmed. Query
loads, mask consumption, next-address arithmetic and loop control are charged.
These are small finite working sets, not DRAM results or hashtable throughput.
The activity masks are also not a uniform-selectivity model. Five repetitions
run in forward and reverse arm order; retain order variation rather than hide it.

The final five arms are the public filter, the scalar-tail algorithm control,
an independent fused whole-window kernel, direct8 throughout, and the original
complete16 algorithm with a native8 tail and explicit traversal. The scalar-tail
arm reproduces the former algorithm in this harness, not a bit-identical old
binary. The independent fused control calls no Ikea decode/compare/mask helper.
It uses word SIMD and bounded 512-bit masked loads for standard widths, and the
previous independently written byte-column/bitplane comparison for other widths.
It is a strong reproducible control, not proof of a globally optimal kernel.
All arms compare against independently encoded logical values before timing.

The first cohort measured the first candidate beside the scalar-tail and fused8
controls. The second all-width cohort isolated direct8 and the remaining16 path.
The final cohort uses the corrected traversal and strengthens the whole-window
word control. Those source changes mean their absolute timings are not one
interchangeable data set.

## Correctness and code generation

The filter matrix covers K1–64, all five predicates, full-width/out-of-domain
queries, sparse and empty activity, heads, substitutions, Local/Striped storage,
unaligned origins and arbitrary logical edges. Exact complete Local windows of
8 through64 rows end at an inaccessible page, including the dense AVX-512 path.
All64 byte offsets exercise decoding, checked replacement, point mutation,
canaries and source-qualified effect bounds. Guarded8 also has an unreadable
payload witness. The tests split width instantiations into small translation
units to keep sanitizer compiler memory bounded.

Native disassembly and LLVM-MCA are retained for standalone runtime-input
consumers. Native whole-loop timings make performance decisions. MCA linearizes
the witness bodies, including control flow, and does not model cache misses or
branch prediction; its instruction-pressure estimates are diagnostic only.

## Selected native eight-slot results

Equality, full activity, byte offset1, Local H=0. Each cell is **dependent latency /
reciprocal throughput, ns per eight-slot window**. The table includes consumer
address work and query loads; it does not estimate standalone instruction latency.

| Profile / K | SeriesPack | Scalar-tail control | Independent fused |
|---|---:|---:|---:|
| Zen5 AVX2 / 12 | 4.88 / 1.78 | 7.34 / 4.45 | 4.79 / 1.78 |
| Zen5 AVX2 / 30 | 5.86 / 2.30 | 9.60 / 6.38 | 6.70 / 2.66 |
| Zen5 AVX-512 / 16 | 4.80 / 0.77 | 4.92 / 2.37 | 4.81 / 0.73 |
| Neoverse V2 NEON / 30 | 10.64 / 3.37 | 13.22 / 8.78 | 12.79 / 3.83 |
| Neoverse V2 NEON / 64 | 10.36 / 2.96 | 7.67 / 3.38 | 11.24 / 3.00 |

Native8 improves reciprocal throughput over the scalar tail broadly. It does not
win every dependent-latency comparison: scalar standard-word and wide-body scans
can have a shorter serial path. Against the independent fused control, the median
native8 reciprocal cost ratio across K1–64 and all five predicates is 0.69 on
Zen5 AVX2, 0.73 on Zen5 AVX-512, 0.56 on Granite AVX2, 0.63 on Granite AVX-512,
and 0.68 on NEON. Those ratios summarize this finite case set, not a workload mix.
The large odd-width wins partly reflect direct body comparisons replacing the
control's byte-column comparisons; they do not establish global optimality.

Losses remain visible in the CSV. Examples: Granite AVX-512 K4 equality costs
6.14 ns serial against fused 5.09; Zen5 AVX2 K16 equality costs 0.87 ns reciprocal
against 0.74. Small independent-control wins are not grounds for exposing another
preset. A first-match consumer may make a different trade-off from a full mask.

The selection capture contains **1,326,000 observations**, including 262,800
separate metric/order cells. [Provenance](evidence/tiles/selection.json) records
jobs, successful native regression checks and content-addressed archives;
[CSV identities](evidence/tiles/sources.json) link the extract to exact samples.
Raw samples, binaries, complete disassembly, compiler inputs and individual MCA
runs remain in those archives. Compact [Zen5](evidence/tiles/zen5-codegen.json),
[Granite](evidence/tiles/granite-codegen.json) and [NEON](evidence/tiles/neon-codegen.json)
audits retain the selected witnesses. All five profiles have 104 standalone
witnesses without helper calls and successful MCA output for eight witnesses.
MCA block estimates include mutually exclusive control flow; use them to inspect
pressure and dependencies, not as the measured result or a path-time prediction.

## First match versus all matches

`indexof` has a distinct consumer meaning: the lowest active exact match, or an
explicit not-found result. It need not compute later matches. A fingerprint
probe usually still needs every candidate in its current group; its first
fingerprint match need not be the first complete-key match.

The focused `indexof.cpp` experiment compares whole-filter plus first-set-bit,
early eight- and sixteen-row groups, an independent whole-window fused filter
plus first-set-bit, body-first scalar candidate verification, body-first bounded
residual masks, and an owner-bounded GFNI residual control. It covers K/N=10/40,12/32,16/24,30/16,40/16,64/8;
first-group, last-group, uniform, miss, mixed and duplicate-match patterns;
clustered and random bodies, explicit shared-prefix losses, full and sparse activity. All payloads start at byte offset1. A
branchless sentinel in the ordinary early-group paths keeps even a miss's result
derived from the comparison before the next address. Body-first paths can return
a constant sentinel through predicted branches: their all-miss serial rows are
**predictable one-stream cost, not strict dependent latency**. Serial hit latency
and eight-stream reciprocal throughput are separate. All traces revisit a finite
64-window working set; mixed positions are not an unlimited random branch stream.
This initial capture added no public API; the subsequent [ARM graduation](ARM-INDEXOF.md)
adds the small semantic operation and measures its actual implementation.

This tests stopping between groups and preparing less data for a first match.
It does not exhaust dedicated first-lane SIMD reductions, which may avoid
constructing a full mask for some word widths. See the [GFNI derivation](CONTAINS.md)
for the owner-proved four-instruction residual contains core.

## Final selected-policy confirmation

A fresh source capture reran 18 representative widths, N8/N24, both predicates,
all four byte offsets, both activity masks and both arm orders, plus the complete
production filter regression on all five profiles. [The receipt](evidence/tiles/confirmation.json)
identifies those bytes and native checks; [the extract](evidence/tiles/selected-policy.csv)
keeps both metrics and order medians for offset1/full activity.

Selected N24 results below use **latency / reciprocal ns per window**. They are
from this confirmation capture, not recombined selection-run arms.

| Profile / operation | Selected SeriesPack | Old algorithm | Independent fused |
|---|---:|---:|---:|
| Zen5 AVX2, K30 == | 7.58 / 4.17 | 14.39 / 9.89 | 9.67 / 4.85 |
| Zen5 AVX-512, K12 == | 8.35 / 2.21 | 9.82 / 5.25 | 6.49 / 3.07 |
| Granite AVX2, K30 == | 9.85 / 5.63 | 18.74 / 12.96 | 12.85 / 8.20 |
| Granite AVX-512, K30 == | 12.44 / 5.10 | 14.41 / 10.20 | 12.62 / 7.79 |
| Neoverse V2, K30 == | 15.04 / 6.39 | 24.19 / 15.51 | 18.34 / 8.84 |
| Neoverse V2, K41 < | 27.09 / 12.68 | 24.32 / 12.90 | 38.32 / 26.36 |

The last row retains a serial-latency loss while reciprocal cost is similar.
The Zen5 AVX-512 K12 choice favors reciprocal throughput over the fused control's
shorter serial path. There is no claim of dominance on every metric or consumer.
No new runtime selector or public filter option is introduced.

## First-match decision

**Indexof is worth a small, separate semantic operation.** The evidence supports
internal width/ISA lowering choices, not another menu of public filter modes.
Keep the first active matching rank and explicit not-found semantics independent
of group size. The initial experiment remained in the spike. The subsequent
[ARM qualification and graduation](ARM-INDEXOF.md) adds `indexof` and a prepared
arithmetic composition body; ordinary leaves still cannot borrow padding.

Native [samples](evidence/tiles/indexof.csv) retain 109,200 observations / 10,920
metric cells, including forward/reverse medians. [Source and archive references](evidence/tiles/index-provenance.json)
identify the final seven-arm capture. The [complete-consumer codegen audit](evidence/tiles/index-codegen.json)
records native excerpts and confirms query preparation inside the timed serial
recurrence rather than outside it. Each sample is a complete first-rank-to-next-
address consumer, with runtime query preparation charged. Units below are
**dependent hit latency / reciprocal ns per lookup**, random bodies, uniformly
positioned hits, full activity, Local H=0, byte offset1:

| Profile, K/N | Full filter + first bit | First-match candidate | Candidate |
|---|---:|---:|---|
| Zen5 AVX2, 10/40 | 6.97 / 3.59 | 5.86 / 1.93 | Bounded body-first residual mask |
| Neoverse V2, 12/32 | 14.33 / 5.69 | 11.62 / 3.49 | Bounded body-first residual mask |
| Neoverse V2, 30/16 | 14.47 / 5.12 | 13.09 / 4.10 | Bounded body-first residual mask |
| Zen5 AVX-512, 30/16 | 10.03 / 1.75 | 7.40 / 1.46 | Owner-proved GFNI residual |
| Granite AVX-512, 30/16 | 13.18 / 3.44 | 8.53 / 1.94 | Owner-proved GFNI residual |
| Zen5 AVX-512, 12/32 | 10.13 / 1.46 | 8.46 / 1.35 | Early16 |
| Zen5 AVX2, 64/8 | 7.32 / 0.96 | 7.31 / 0.96 | Early8, equivalent single group |

CSV arm IDs are 0=full filter + first bit, 1=early8, 2=early16,
3=independent fused full mask + first bit, 4=body-first scalar candidates,
5=body-first bounded residual mask, 6=owner GFNI residual. Arm6 equals arm5
without AVX-512/GFNI; at word widths arms4–6 use early8. Patterns0–5 are
clustered-body first/last/uniform/miss/mixed/duplicate cases;6–11 repeat those
with random bodies;12/13 are shared-prefix miss/late-hit controls. `active` is
an original-rank bitmask. The all-miss serial caveat applies to patterns3,9,12
for body-first paths; sparse cases can also turn nominal hits into misses.

The independent fused full-mask/first-bit K30 control costs 9.68 / 3.77 ns on
Zen5 AVX-512 and 12.09 / 6.07 on Granite. The existing public filter is the
stronger reciprocal-throughput baseline here, and the owner GFNI candidate is
compared against it above. This is not a hashtable scorecard: no hash, value load,
Swiss overflow, allocation, Loom or cold residence is charged.

There is no universal early8 or scalar-candidate winner. On Zen5 AVX-512 K12/N32,
a last-group hit costs 5.92 ns reciprocal with early8 versus 1.51 for the full
filter. The bounded body-first form costs 2.35 ns reciprocal on uniform hits
versus 1.46 for the full filter, despite improving serial latency. On Zen5 AVX2
K30/N16 with equal bodies and late residual matches, scalar candidate verification
costs 13.85 / 11.22 ns versus 7.23 / 3.40 for the full filter. A residual mask caps
that candidate work. The owner GFNI form still loses a little Zen5 throughput
in the shared-prefix late-hit case: 1.88 versus 1.76 ns, while reducing serial
latency from 10.01 to 7.88 ns.

The GFNI check passed on both x86 hosts for R1–7, every in-domain query and 4096
randomized eight-byte tiles per width, with arbitrary values beyond the R planes.
[Zen5 disassembly](evidence/tiles/zen5-contains-codegen.txt) and
[Granite disassembly](evidence/tiles/granite-contains-codegen.txt) show the four-
instruction prepared core plus `setne`/`ret`, and the longer full runtime-query
function. The latter includes bounds, broadcast and query reversal; these are
not free merely because constants can be shared.

The final [check receipt](evidence/tiles/checks.json) records local filter/examples
and fully instrumented ASan/UBSan success alongside the native confirmations.
