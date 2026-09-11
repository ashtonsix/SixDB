# SeriesPack caller and composition measurements

These measurements concern the first Ikea implementation; they are not a
universal cost model. [Workload contracts](../seriespack-predecessor/workloads.md) distinguish the compared operations.
The early seam source, also reused by the isolated follow-ups below, carries
global bulk-only `codec_contract`/`access_contract` strings. Read the individual
case labels and workload contracts for the actual composition/caller operation;
those global strings do not describe effect reporting or intermediate results.

## First V2 screen, 2026-09-10

Job `20260910T155225Z-fd157f11`, source
`fd34642d52bd23584cccf99f4ed95c475d88bd4c317f1867d9bdc793c95197aa`,
measured 182 cases with three sequential repetitions, 0.03 seconds minimum,
CPU 0 pinned, Clang 21.1.8 Release, baseline NEON with Neoverse V2 tuning.
The aggregate correctness check passed. Raw samples, binaries, source identity
and commands are in the [retained bundle reference](evidence/v2-seams-initial.artifact.json).
Global `codec_contract` and `access_contract` context strings in this first
archive describe the bulk workload; the individual seam cases supply their
actual contracts, including validation and effect reporting.

The authored and handwritten native compositions closely match. A separate
materialized plan gives a more useful challenge to their shared lowering:

| Composition, 8,192 values | Authored ns/value | Materialized ns/value |
| --- | ---: | ---: |
| Local k5, all active | 0.2550 | 0.4812 |
| Striped k12, all active | 0.1314 | 0.3299 |
| Local k60/h8, all active | 0.7142 | 0.6760 |
| Local k60/h8, 75% active | 0.7357 | 0.9703 |

“Active” describes the prefilter; the unsigned comparison further selects rows.
The k60/h8 all-active native composition takes about 5.6% longer even though it
avoids a scratch array. This does not identify its cause or justify changing
every native region. Materialization remains an explicitly priced alternative.
The consumer loop in that control may autovectorize; its buffer stores, reads,
comparison and reduction are all timed, with allocation and binding excluded.

The caller measurements expose a larger integration cost:

| Bound NEON construction | No effects, ns/call | With effects, ns/call | Records |
| --- | ---: | ---: | ---: |
| Dense Local k12, n=8,193 | 2,036 | 2,038 | 1 |
| Strided Local k60/h8, n=8,193, gap=32 | 4,148 | 8,727 | 2,050 |

The strided case emits 32,800 bytes of records; its prepared capacity also
supports selected mutation and is larger. The old collector's generated loop
stores and reloads `effect_output.size`, then reloads the output base, for each
record. The focused correction keeps the append cursor and placement facts
local and updates the output length once after all streams. Existing operation
checks pass; generated code removes that dependency for 44 additional text
bytes. The paired measurement below isolates its effect. Exact coverage, prefix
preservation and the public reporting representation remain unchanged.

Binding is charged separately: about 13.5 ns for the native reader and 8.1 ns
for the native encoder in these cases. Checked construction also pays for
input-fit validation when the input carrier admits values above k. For dense
Local k12 with 8,193 u64 inputs, checked construction takes about 3,549 ns versus
2,036 ns bound. The narrower-source cases exercise admission where the source
type already proves fit. These costs belong at the caller boundary; they are
not evidence of an inner-kernel ISA cost.

## Paired V2 collector measurement

Job `20260910T161827Z-6747e82d`, source
`fe0cadb02925dcad3fb2b461aba6503b9749f2d01730792193d59dcbd3a3b8dc`,
replayed the exact archived baseline and measured the collector change on the
same CPU 0. Both arms cover 80 cases with five sequential repetitions, 0.2 seconds
minimum. The candidate passed `ikea_seriespack_operations_check`; native kernels
and workload sources are unchanged. The [paired bundle reference](evidence/v2-effects-pair.artifact.json)
retains both arms, source hashes, commands and code. Values below are medians in
ns/call, with allocation and admission excluded from bound calls.

| Bound NEON construction | Before | After |
| --- | ---: | ---: |
| Strided k60/h8, n=8,193, no effects | 4,148.2 | 4,148.9 |
| Same, with 2,050 effects | 8,727.0 | 4,669.1 |
| Dense k12, n=8,193, with one effect | 2,037.1 | 2,038.4 |
| Dense k12, n=17, with one effect | 18.06 | 20.05 |
| Dense k9/h8, n=17, u8 input, with two effects | 14.02 | 16.01 |

The strided reporting increment falls from about 4,579 ns to 520 ns, consistent
with the removed cursor dependency. However, the short dense cases lose about
2 ns (11–14%). The large-array gain did not justify that regression; the refined
measurement below resolves it without changing the reporting contract.
Selected-write reporting was an unchanged control in this run. A separate
codegen experiment can remove its cursor dependency, but grows the driver family
by about 30%; that does not justify adopting it without a better scoped lowering
and measurements. These experiments concern span reporting; consuming the borrowed
addresses, summary maintenance and durable publication are outside these timings.

The refinement acquires each present head plane only when reaching its stream,
instead of eagerly copying both head descriptors. This removes the collector's
stack frame and reduces its text to 896 bytes (original 884, first correction
928), while retaining register cursors and one final output-length store.
Job `20260910T163711Z-3c16a764`, source
`23131ce0adb3167f8983a876f590d48eddd8f5ffa3b193b1a328812f86713867`,
repeats the same original-baseline/candidate comparison, 80 cases × five sequential
repetitions at 0.2 seconds, CPU 0. The focused operation check and fixture oracles
pass. The [refined paired bundle](evidence/v2-effects-refined-pair.artifact.json)
retains source and measurements.

| Bound NEON construction with effects | Original | Refined, ns/call |
| --- | ---: | ---: |
| Dense k12, n=17 | 18.06 | 18.05 |
| Dense k9/h8, n=17, u8 input | 14.17 | 14.17 |
| Strided k60/h8, n=8,193, gap=32 | 8,725.2 | 4,664.1 |

Plain strided encode remains about 4,148 ns; the measured reporting increment
is now about 518 ns. The earlier short-case regression is absent. Across the 80
paired cases the largest median increase is below 0.8%, including unchanged
selected-write controls; that is a description of this run, not an acceptance
threshold or evidence about other machines. Null/empty behavior, exact span
coverage, preserved prefixes and borrowed-address lifetime obligations are unchanged.

## First GNR composition screen

Job `20260910T160639Z-bd4aae11` measured 286 composition/caller cases, three
sequential repetitions, CPU 0, full-feature x86 profile with Granite Rapids
tuning. The [initial GNR bundle reference](evidence/gnr-seams-initial.artifact.json)
retains samples and provenance. The aggregate check passed. The authored and direct versions again
closely match, but both share a poor lowering for several local formats:

| AVX-512 composition, 8,192 values, all active | Authored ns/value | Materialized ns/value |
| --- | ---: | ---: |
| Local k5 | 0.2433 | 0.1383 |
| Local k7 | 0.2433 | 0.1386 |
| Local k12 | 0.3517 | 0.1955 |

The current immediate executor limits its work to the eight-value physical tile.
Bulk decode can combine many such tiles before consumption. The observed
1.76–1.80× losses make a larger native work region a concrete next experiment;
they do not establish that materialization or fusion wins universally. A probe
reuses the existing 32/64-value native readers with the same authored operation,
leaving dense-region admission outside the loop. The follow-up below measures it.

## GNR dense-region follow-up

Job `20260910T163358Z-9184531a` adds only the probe composition TU to the same
physical sources. All 32 focused cases ran five sequential repetitions, 0.2 seconds
minimum, CPU 0. The native composition check and benchmark pre/post oracles passed.
The [retained bundle reference](evidence/gnr-composition-grain.artifact.json) includes
the frozen source, code, commands and samples. All four plans run in the same binary.

| Full-feature GNR, local k5 / k7 | Tile authored | Materialized | Dense authored |
| --- | ---: | ---: | ---: |
| AVX-512, all active, ns/value | 0.2439 / 0.2440 | 0.1393 / 0.1387 | 0.03136 / 0.03136 |
| AVX-512, 75% active | 0.2439 / 0.2439 | 0.1475 / 0.1472 | 0.03043 / 0.03044 |
| AVX2, all active | 0.1800 / 0.1799 | 0.1442 / 0.1437 | 0.03885 / 0.03916 |
| AVX2, 75% active | 0.1802 / 0.1806 | 0.1522 / 0.1521 | 0.04344 / 0.04379 |

The dense producer borrows four/eight adjacent physical tiles, using the existing
native 32/64-value readers. The generic `selected_sum` and native comparison and
reduction bodies are reused; only read grain, active-mask construction and original
coordinates change. Codegen retains values and predicates in registers. Admission
requires complete contiguous headless regions and occurs outside timing. This does
not widen a tile executor's access permission or authorize crossing stride gaps.
The AVX2 family runs in the full-feature binary, not an AVX2-only instruction ceiling.

This is concrete support for separating work grain from physical tile size and
keeping the authored operation reusable across both. A small explicit native
region executor is being carried forward. The experiment does not settle k12,
headed/strided regions, partial boundaries, progressive filtering or accumulator
scheduling, and does not replace the materialized control.

The [same original seam source on Zen](evidence/zen-seams-initial.artifact.json),
job `20260910T163736Z-75194fc8`, also passed aggregate checks and measured 286 cases
with three sequential repetitions, 0.03 seconds minimum, CPU 0. Tile-based
immediate composition loses more broadly there: all-active local k12 takes about
2.6–2.7× the materialized plan's time, k31 about 1.8–1.9×. This is not resolved
by the narrow dense-region result above. A second experiment keeps the tile
producer but represents each logical sum as native u64 partial sums, combines
those carriers in the driver and reduces horizontally once at the end. It tests
the cost of early scalar reduction while preserving the same authored operation
and final modulo-u64 result. The deferred-reduction measurement below evaluates
that representation; it remains a workbench diagnostic, not a public contract.

The production dense executor subsequently passed its guarded native checks on
Zen in job `20260910T165949Z-7346676c`, source
`6bff73722802033795b551f26efcae3eae387b6ebfd501f2a6069a84d651b1a3`.
All 32 focused cases ran five sequential repetitions at 0.2 seconds, CPU 0;
[the retained bundle](evidence/zen-dense-composition.artifact.json) includes code,
checks and samples. Production separates the ISA comparison/reduction bodies
from coordinate and access-grain operations; existing tile and dense witness
instructions/constants remain identical to their respective prior implementations.

| Full-feature Zen, local k5 / k7 | Tile authored | Materialized | Production dense |
| --- | ---: | ---: | ---: |
| AVX-512, all active, ns/value | 0.1294 / 0.1293 | 0.03569 / 0.03620 | 0.01982 / 0.01984 |
| AVX-512, 75% active | 0.1293 / 0.1292 | 0.05119 / 0.05186 | 0.01772 / 0.01771 |
| AVX2, all active | 0.1109 / 0.1132 | 0.04176 / 0.04408 | 0.02754 / 0.02750 |
| AVX2, 75% active | 0.1110 / 0.1131 | 0.05747 / 0.05941 | 0.03009 / 0.03016 |

The public executor supports headless local widths 1–7 using the existing exact
native readers. Guarded checks cover all seven widths, nonzero original origins,
independently admitted substituted sources, masks and comparison domains. The
timed composition comparison above covers widths 5 and 7 only. Larger sources,
head/stride boundaries and other operations still need their own measurements.

## Zen deferred horizontal reduction

Job `20260910T170305Z-172a2d82`, source
`5d003f2927ddcd143790428e2c02321a0cf1bb9bafa5ced0ec26d76b44afa5ac`,
overlays only the carrier benchmark TU on the original seam headers and physical
implementation. The [retained bundle](evidence/zen-reduction-carrier.artifact.json)
contains the source, checks and all 128 cases, each with three sequential
repetitions at 0.2 seconds, CPU 0. The original native composition check and extra
benchmark cutoff/overflow oracles passed. All four plans remain in one binary;
this experiment does not include the newer dense producer.

| AVX-512, ns/value | Tile authored | Native sum carrier | Materialized |
| --- | ---: | ---: | ---: |
| Local k12, all active | 0.22760 | 0.15597 | 0.08662 |
| Local k31, all active | 0.20826 | 0.15224 | 0.11236 |
| Local k56, all active | 0.12323 | 0.07493 | 0.11588 |
| Local k56, 75% active | 0.10559 | 0.06157 | 0.12285 |
| Local k60/h8, all active | 0.17749 | 0.10076 | 0.12822 |
| Local k64, all active | 0.11313 | 0.05569 | 0.11892 |
| Striped k12, all active | 0.04826 | 0.03417 | 0.03969 |

Every carrier case has a lower median than its old authored counterpart in this
run. However, local k12/k31 and many AVX2 cases still lose to materialization;
AVX2 local k12 takes 0.16781 versus 0.08653 ns/value. Narrow k5/k7 still benefit
much more from the independently measured dense producer. Larger read regions
and later reduction are separate decisions; these results do not establish one
universally preferred composition or resolve all remaining losses.

The unchanged authored function returns one logical fragment sum, represented
by owning native u64 partial lanes. The driver combines these values and finishes
once. The explicit modulo-u64 arithmetic law makes that combination equivalent
even on overflow. Partial lanes are a representation of a sum, not row values or
row coordinates. A scalar consumer needs explicit finalization; an erased/CPS or
suspension boundary needs a separately established carrier convention. No such
ABI is promoted by this inline experiment. Cross-target measurements and choices
for the remaining local formats are still open.

The [carrier diagnostic](carriers/README.md) is now retained
beside the current benchmark, with the existing authored, direct, materialized
and dense controls preserved. Its first integrated source uses headers identical
to coherent physical capture
`db9238fea0ea2294b974c34a63447571ec068c2aedf0310f29ec5564c5da8d00`.
Native NEON cutoff/overflow checks and full-feature x86 compilation pass; the
combined source has 136 full-feature x86 cases and 64 NEON cases. This is a new
measurement candidate, not a transfer of the isolated B timings to newer physical
kernels. The original B capture above remains the evidence for those timings.

## Coherent V2 composition comparison

Job `20260910T180439Z-48fa4114`, source
`95d879280aa283e48992cd2b2518b14f1e1d5f8cf86ada6608e0ec3621436f17`,
measures the combined workload against that coherent physical capture. The
production library hash is unchanged before/after the benchmark rebuild. Its
preceding aggregate check, the standalone carrier cutoff/overflow check and all
64 benchmark cases pass. Cases have five sequential repetitions, 0.2 seconds
minimum, CPU 0, Clang 21.1.8 Release, baseline NEON with Neoverse V2 tuning.
[Per-case medians/ranges](evidence/composition-coherent/v2.csv),
[metadata](evidence/composition-coherent/v2.json) and the
[retained artifact](evidence/v2-composition-coherent.artifact.json) preserve the
comparison. The x86 dense32/64 alternatives are not available in this profile.

| NEON, ns/value | Tile authored | Native sum carrier | Materialized |
| --- | ---: | ---: | ---: |
| Local k12, all active | 0.20407 | 0.18019 | 0.42587 |
| Local k31, all active | 0.33934 | 0.29631 | 0.46657 |
| Local k60/h8, all active | 0.71110 | 0.70406 | 0.70671 |
| Local k64, 75% active | 0.24634 | 0.26952 | 0.61041 |
| Striped k12, all active | 0.13135 | 0.12522 | 0.31971 |

The carrier improves 15 of the 16 authored medians, but loses about 9.4% on
masked k64. That loss prevents treating it as a universal replacement, even
though both beat materialization in that case. Local k12/k31 behave differently
from the earlier x86 results. All-active k60/h8 remains near parity with
materialization; the small median ordering does not establish a preferred plan.
These are measurements of one code closure, not a claim that deferred reduction
is intrinsically better or that every composition path is competitive.

## Coherent Zen composition comparison

Job `20260910T182929Z-75d7e42e` measures the same physical and composition inputs on Zen, with
136 cases and five sequential repetitions at 0.2 seconds minimum, CPU 0.
The full-feature x86 profile admits AVX-512 BW/VBMI/VBMI2 and GFNI; AVX2 and
AVX-512 case names identify implementation families within that profile.
The unchanged production library passed the preceding coherent aggregate
checks; the standalone cutoff/overflow check and all fixture oracles pass.
[Cases and repetitions](evidence/composition-coherent/zen/cases.csv),
[source/check facts](evidence/composition-coherent/zen/run-facts.json),
[summary provenance](evidence/composition-coherent/zen/provenance.json) and the
[retained artifact](evidence/composition-coherent/zen/artifact.json) preserve
the comparison. All 32 groups agree on logical, active and selected extents.

| AVX-512, all active, ns/value | Tile authored | Sum carrier | Materialized | Dense authored |
| --- | ---: | ---: | ---: | ---: |
| Local k5 | 0.13932 | 0.08520 | 0.03639 | 0.02075 |
| Local k7 | 0.13634 | 0.11967 | 0.03600 | 0.02061 |
| Local k12 | 0.24196 | 0.16495 | 0.09052 | — |
| Local k31 | 0.21953 | 0.15307 | 0.11654 | — |
| Local k56 | 0.13168 | 0.07906 | 0.12185 | — |
| Local k60/h8 | 0.19014 | 0.10712 | 0.13066 | — |
| Local k64 | 0.08420 | 0.05823 | 0.10763 | — |

Dense32/64 retains its advantage for both narrow widths and masks. Deferred
reduction remains useful for wider AVX-512 cases, but still leaves local k12/k31
1.82×/1.31× slower than materialization here; AVX2 takes 1.96×/1.48×. The coherent
comparison therefore still warrants investigating those lowerings.

The captured object shows eight-value reads and one accumulator dependency
chain in those carrier loops. Materialized consumption uses larger iterations
and four independent accumulators; its decoder is a separate bound call. A
driver-only experiment retains the same readers and compares fixed four-tile
unrolling with one, two or four carriers, plus the original driver. Its compiled
loops retain those dependencies, but unrolling also introduces constant reloads
for k31 and increases text. Measurement must determine whether it helps; fewer
horizontal reductions or more independent accumulators alone do not prove a win.

## Coherent GNR composition comparison

Job `20260910T183151Z-9b5add59` uses a source manifest identical to the coherent
Zen composition job, with Granite Rapids tuning. The full-feature profile,
136 cases, five sequential repetitions, 0.2-second minimum and CPU 0 affinity
match the comparison structure above. The library hash is unchanged and the
standalone carrier and benchmark checks pass. Retained
[cases](evidence/composition-coherent/gnr/cases.csv),
[run facts](evidence/composition-coherent/gnr/run-facts.json),
[provenance](evidence/composition-coherent/gnr/provenance.json) and
[artifact reference](evidence/composition-coherent/gnr/artifact.json) preserve
all 32 matched groups.

| GNR, all active, ns/value | Tile authored | Sum carrier | Materialized |
| --- | ---: | ---: | ---: |
| AVX2 Local k12 | 0.31098 | 0.22701 | 0.19609 |
| AVX-512 Local k12 | 0.35143 | 0.25869 | 0.19616 |
| AVX2 Local k31 | 0.26630 | 0.19850 | 0.26096 |
| AVX-512 Local k31 | 0.34693 | 0.26267 | 0.29737 |

Local k12 still loses to materialization: about 1.16× for the AVX2 carrier and
1.32× for AVX-512 all-active; masked AVX-512 is 1.22×. Masked AVX2 is near parity.
Local k31's carrier beats materialization here in both families and masks,
unlike Zen. Dense32/64 again wins for k5/k7; full AVX-512 all-active takes about
0.0314 ns/value versus 0.138 for materialization. These results support retaining
target/context-specific choices while investigating k12; they do not establish
one preferred reduction representation or accumulator schedule across machines.

## Zen accumulator-driver experiment

Job `20260910T183752Z-b4812362`, source
`3e10e1faeb7a0255d112499b94e192e43425460cfc8fba19cbca98cf3bd7888f`,
holds physical sources and the production library unchanged. It adds fixed
four-tile unrolling with one, two or four independent sum carriers to the
existing Local k12/k31 controls. Keeping unrolling equal between the new arms
separates that choice from adding accumulator chains. All 56 cases have five
sequential repetitions, 0.2 seconds minimum, CPU 0, under the same full-feature
Zen profile. Original cutoff/overflow checks and the new modulo-bank combination
checks pass. Retained [cases](evidence/accumulator-driver/zen/cases.csv),
[run facts](evidence/accumulator-driver/zen/run-facts.json),
[provenance](evidence/accumulator-driver/zen/provenance.json) and
[artifact](evidence/accumulator-driver/zen/artifact.json) preserve all eight
matched seven-plan groups.

| All active, ns/value | Original carrier | Unroll4, one | Two | Four | Materialized |
| --- | ---: | ---: | ---: | ---: | ---: |
| AVX2 k12 | 0.17827 | 0.16924 | 0.16614 | 0.16483 | 0.09097 |
| AVX2 k31 | 0.16545 | 0.15262 | 0.14943 | 0.15115 | 0.10924 |
| AVX-512 k12 | 0.16793 | 0.15668 | 0.15411 | 0.15247 | 0.09378 |
| AVX-512 k31 | 0.15576 | 0.14671 | 0.14484 | 0.14543 | 0.12025 |

Most observed median improvement is already present with serial unrolling;
additional chains add little, repetition ranges overlap, and the best median
count varies with the mask and width. Materialization still wins every group.
The local codegen witnesses also show increased text and k31 constant reloads
under unrolling. This is a measured limited improvement, not a sufficient fix
or a reason to adopt one driver schedule everywhere. It is not carried into the
module. A GNR repetition of this variant is lower priority than testing larger
read groups with fuller native lane use; multiple accumulators remain available
for a future lowering where evidence supports them.

The [retained worker code check](evidence/accumulator-driver/zen/worker-codegen.json)
confirms all 32 ordinary carrier loops preserve the requested accumulator
dependencies, native instructions and loop byte counts. AVX-512 k31 still
reloads two fixed active masks from stack per four tiles in every unrolled arm;
there are no decoded-value or carrier spills or calls in these loops. General
register assignments and two stack-slot offsets differ from local compilation.
The separate endpoint text witnesses remain local-only: their standalone TU
was not compiled by this worker, so those sizes are not worker function sizes.

## Zen grouped-read composition

Job `20260910T200444Z-c1ee1d8a`, source
`ce8b62ceb69e04f5fef3064e835c9bf87339b5db2f112ec5e5996fccd6994c64`,
tests larger native working groups over unchanged Local12/31 packets. Four
existing AVX2 child readers provide 32 u16 lanes for k12; two provide 16 u32
lanes for k31. Each body and tail resolves its actual source and stride. The
ordinary authored `selected_sum` and existing native consumer bodies then
operate on the group. Scalar and deferred-sum results are separate arms.

All 40 cases have five sequential repetitions, 0.2 seconds minimum, CPU 0,
Clang 21.1.8 with Zen5 tuning in the full AVX-512/VBMI/VBMI2/GFNI profile.
The fresh bootstrap reproduces the exact coherent production library; it stays
unchanged during this experiment. All 40 captured Ikea source files match the
coherent base. The 2,688 cutoff/mask/source cases pass, including independently
strided body/tail replacements, guarded packets, original origins and logical
slack. The standalone carrier checks also pass. Every compared plan agrees on
logical, active, selected and encoded extents. Retained
[cases](evidence/grouped-composition/zen/cases.csv),
[provenance](evidence/grouped-composition/zen/provenance.json),
[run facts](evidence/grouped-composition/zen/run-facts.json) and
[artifact](evidence/grouped-composition/zen/artifact.json) identify the comparison.

| AVX-512 family, ns/value | Tile scalar | Tile carrier | Group scalar | Group carrier | Materialized |
| --- | ---: | ---: | ---: | ---: | ---: |
| k12, all active | 0.22766 | 0.15587 | 0.07727 | 0.06123 | 0.08695 |
| k12, prefilter75 | 0.22748 | 0.15579 | 0.07965 | 0.06148 | 0.09897 |
| k31, all active | 0.20818 | 0.14761 | 0.12933 | 0.09107 | 0.10851 |
| k31, prefilter75 | 0.20752 | 0.14712 | 0.13031 | 0.09665 | 0.12018 |

Grouped carriers beat materialization in all four comparisons: 29.6–37.9% less
time for k12 and 16.1–19.6% for k31. Grouping with an immediate scalar result
also wins for k12; it still loses for k31. Thus the larger read group and the
result representation are both consequential choices. The previous experiment's
extra accumulator chains did not solve these gaps; composing fuller working
groups from the existing child readers does, in these measured contexts.

This is a whole-consumer improvement with the composition contracts intact.
It does not isolate concatenation, instruction scheduling or a particular
hardware bottleneck as the cause. Timings use dense placement and 8,192 complete
logical positions; independent strides and substitutions are correctness
witnesses, not measured locality claims. The scalar and carrier arms share
source semantics but do not establish interchangeable public calling conventions
without finalization. The materialized control uses the same retained coherent
library, predating the separate callback scalarization; a later combined
implementation must compare its then-current controls.

The result warrants a GNR comparison and consideration of a small reusable
group executor. It does not select a universal grain, a public native-sum ABI,
or a suspension boundary. The grouped implementation remains experimental.

The [worker code check](evidence/grouped-composition/zen/worker-codegen.json)
maps all eight actual timed loops back to the retained object and prepared
probe. Instructions and loop byte counts match after address/relocation
normalization, with no hot calls or stack/value/carrier spills. The loops
process 32 u16 lanes for k12 and 16 u32 lanes for k31. Scalar arms reduce each
group; deferred arms retain native partial sums. The separate endpoint and
substitution codegen witnesses remain local-only.

## GNR grouped-read comparison

Job `20260910T202148Z-94f35804` uses the same complete source manifest and
archive as the Zen grouped experiment. Its fixed production library matches
the earlier GNR coherent library. All 40 cases have five sequential 0.2-second
repetitions on CPU 0, Clang 21.1.8, GNR tuning and the same full feature ceiling.
The 2,688 grouped guard/source cases, carrier checks and all matched-work
counters pass. Retained [cases](evidence/grouped-composition/gnr/cases.csv),
[provenance](evidence/grouped-composition/gnr/provenance.json),
[run facts](evidence/grouped-composition/gnr/run-facts.json) and
[artifact](evidence/grouped-composition/gnr/artifact.json) preserve the comparison.

| AVX-512 family, ns/value | Tile scalar | Tile carrier | Group scalar | Group carrier | Materialized |
| --- | ---: | ---: | ---: | ---: | ---: |
| k12, all active | 0.35202 | 0.26068 | 0.13036 | 0.10598 | 0.19713 |
| k12, prefilter75 | 0.35378 | 0.26049 | 0.13101 | 0.10466 | 0.21340 |
| k31, all active | 0.35243 | 0.25135 | 0.22949 | 0.17268 | 0.27701 |
| k31, prefilter75 | 0.35202 | 0.25278 | 0.23126 | 0.17759 | 0.28744 |

The grouped carrier is the fastest measured plan for each width/mask on both
machines. It also beats the faster AVX2-family materialized controls: GNR k31
takes 0.25265/0.26368 ns/value there. Against the fastest materialized family,
the grouped carrier saves about 46–51% for k12 and 32–33% for k31 on GNR;
the corresponding Zen ranges are 30–38% and 10–14%. These compare whole
operations over the same original values and selection, within each run.

GNR's grouped scalar k31 also beats materialization, whereas Zen's loses.
Consequently grouping and finalization should remain separate choices even
when the grouped carrier is preferred for this complete reduction. A native
partial sum represents one modulo-u64 result, not a set of rows; a scalar
endpoint still owes finalization. There is no new erased or CPS ABI here.

The two-target result supports curating a small reusable group/result option
from these mechanisms. It does not justify promoting two benchmark-specific
adapters, selecting every possible group size, or weakening source admission.
The timing scope remains dense complete regions at 8,192 positions with all
and 75% masks; the updated materializer and other placements/extents remain
relevant controls when integrating the option. The ordinary dynamic-range
driver's separate work is still unfinished.

The [actual GNR executable audit](evidence/grouped-composition/gnr/worker-codegen.json)
checks all eight ordinary grouped loops against their retained object assembly.
They preserve the native group widths, instruction counts and inner byte sizes
observed on Zen, with different instruction scheduling. None has inner calls,
stack operands or RIP-relative constant loads. The separate above-domain k31
all-carrier bypass has one group on GNR versus two on Zen; the measured cutoff
takes the ordinary one-group loop on both. These are code-generation observations,
not a hardware explanation of the timings. Substituted-source endpoints remain
local-only code-generation witnesses.

### Reusable group and result curation

The optional implementation now lives in `avx512::grouped_local_ops<Format>`
and `avx512::deferred_sum_ops<Base>` in Ikea's native composition header. Grouped
reads cover the two natural working-lane families: headless local widths 9–16
use 32 u16 lanes; widths 17–32 use 16 u32 lanes. The two existing packet-read
mechanisms and native consumer bodies are reused. This broadens supported
formats without multiplying benchmark-specific public adapters. It does not
claim that all supported widths benefit.

Each child retains its own source, stride and admission. An eight-row-aligned
origin and complete logical groups suffice; density is not required. The
deferred adapter changes only the result of `sum`, retaining any base state,
native masks, comparisons and read operations. Its owned eight-u64 partials
represent one modulo-u64 result and explicitly finalize at a scalar boundary.
Existing immediate scalar executors and bound dispatch remain unchanged.

Native GNR job `20260910T204008Z-8553e4e4` passes 32,256 grouped guard/source
cases with full features and another 32,256 without GFNI/VBMI2. It also passes
224,490 deferred-result checks across 1/2/4/8-byte lanes, including stateful
base preservation, domain/cutoff/mask edges, owned values, accumulation past
u32 and modulo-u64 wrap. The production header is byte-identical to that run's
candidate. [Run facts](evidence/grouped-composition/curation/run-facts.json) and the
[guard bundle](evidence/grouped-composition/curation/guard-artifact.json) retain
source identity and coverage. The promoted checks build as independent TUs;
their disabled-ISA paths also compile in the local ARM CMake build.

The [curation code-generation comparison](evidence/grouped-composition/curation/codegen.json)
has 52 exact matches: 18 isolated endpoints and eight whole benchmark functions,
each under Zen and GNR tuning, with the same current physical headers. Function
text bytes, instruction/relocation listings and all constant-data sections
match the original probe. The [code-generation bundle](evidence/grouped-composition/curation/codegen-artifact.json)
keeps the objects and inputs. This verifies preservation of the selected bodies;
it supplies no new linked-placement or hardware timing result. Updated ordinary
materialization and other widths, placements and extents remain open comparisons.

## Zen K10 executable placement

The old and new bulk executables disagreed sharply on local k10/h8/u16 decode.
Exact-binary replay on one Zen CPU reproduced about 502 versus 934 ns per 8,192
values, despite identical target decoder instructions and constants. Job
`20260910T163510Z-e1d365bb` then relinked the **same candidate objects**, moving only
that decoder within a fixed-size section. All linker inputs, 245 instructions,
1,103 function bytes apart from relocations, and loaded constants were checked.
The [placement bundle reference](../../executable-placement/evidence/zen-k10-placement.artifact.json) retains
the five binaries, scripts/maps, disassembly and six timing trials.

| Decoder start modulo 64 | u16 loop modulo 64 | AVX-512 ns/call | AVX2 control ns/call |
| --- | ---: | ---: | ---: |
| Original: 16 | 0 | 935.6 | 503.6 |
| Relinked: 0 | 48 | 504.4 | 503.7 |
| Relinked: 16 | 0 | 932.7 | 502.7 |
| Relinked: 32 | 16 | 502.7 | 503.6 |
| Relinked: 48 | 32 | 501.2 | 501.9 |
| Original repeated: 16 | 0 | 933.5 | 501.1 |

Each trial uses five sequential repetitions, 0.2 seconds minimum, CPU 0 and
fixture oracles; no source compilation or new aggregate build occurs. The
output section has the same extent across relinked variants. This isolates
executable placement sensitivity with a stable AVX2 control. It does not identify
the underlying hardware mechanism or establish a broad alignment policy. A robust
physical lowering response remains necessary; favorable results from one binary
placement cannot alone establish its competitiveness.
