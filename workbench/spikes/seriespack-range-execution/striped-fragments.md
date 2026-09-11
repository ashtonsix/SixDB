# One clipped fragment loop for striped reads

Retire this candidate. Replacing the striped boundary's point/16-value/8-value
peel logic with one native-window loop improves all four measured aligned
resident cases, but loses in 51 of 72 other striped cases. The experiment
identifies some removable traversal cost without establishing a good shared
replacement. It does not justify width or size rescue policies, and no reader
change is installed.

This follows the rejected [ordinary materializer integration](integration.md)
and the [performance/maintenance direction](../ikea-composition/design.md#performance-and-maintenance).
The accepted [encoder ABI](../ikea-composition/call-boundaries/encoder.md) is the baseline on both
sides; the head-projection and earlier reader candidates are absent.

## The shared mechanism

The candidate replaces only `decode_boundary`'s striped arm. Each iteration
reads a 16-value residual window inside its current 32-row group, then writes
the requested contiguous interval. Complete fragments retain the existing fixed
store. Partial fragments use a target-local clipped sink; body/head expansion
and joins share the same source implementation as complete reconstruction.
All legal striped widths, head layouts and output carriers use this mechanism.
The existing empty/complete-tile/single-tile/mixed admission remains outside it.

For tile-local position `i`, `lane=i%32`, the window begins at
`i-lane+min(lane,16)` and consumes `min(remaining,16,32-lane)` requested values.
The residual window stays inside the actual tile and residual group. Body and
head dependencies retain their own independently resolved bases and strides.
The ordinary operation permits neighboring encoded values and initialized
physical slack within the tile; it grants no foreign gap or extra output access.
The sink forms pointers only at selected output objects, including when the
read window starts before the request. Physical padding remains inactive.

The [headed-load inspection](evidence/striped-fragments-20260911/20260911T023824Z-887879d5/headed-load-review/review.md)
checks six W5/W6/W12, H16, u64 callbacks. Despite source order, the compiler
already places inactive parts' body/head loads and joins behind interval checks.
Shared residual reads and the first residual expansion remain common work.
This observation does not extend read permissions to arbitrary substituted
sources or prove other carriers' lowering.

## Bounded ordinary-call comparison

Zen job `20260911T023824Z-887879d5`, capture
`08ffa4173a07c809788f7704df8d5961706fac0887842e134f6bc9365e6743d2`,
freshly builds the accepted baseline and replaces only the native archive
member. Benchmark and check objects are common; all four baseline programs
relink byte-identically. Both labelled x86 families use the full
AVX512/VBMI/VBMI2/GFNI feature ceiling, not a pure AVX2 build.

The custom caller covers W5/W6/W12 with dense H0 and independently gapped H16,
plus Local7 controls, all with u64 output. Six shapes are full16, short3,
eight values, group-crossing16, complete-tile-plus-one and last-lane-plus-one.
Each case cycles 512 independently checked queries in batches of 128. Group
positions vary at a 32-row grain; complete-plus-one uses the actual tile grain.
The output address is 8 modulo64; exact prefix/suffix canaries and each result
are checked outside timing. Plane strides, extents and addresses modulo4096,
output addresses, query hashes and checksums accompany the samples.

Crossing/last-lane cases mix within-tile and cross-tile calls. For example,
H0 W5/W6/W12 cross a physical tile in 54/124/252 of their 512 queries;
headed counterparts do so in 55/99/243. The
[query classification](evidence/striped-fragments-20260911/20260911T023824Z-887879d5/query-shapes.json)
keeps this distinction. Existing resident controls use a different query stream
and output alignment; their absolute timing difference from the custom caller
is not a separable estimate of boundary cost.

| Scope | Cases | Separated wins / losses / overlaps | Median after/before |
| --- | ---: | ---: | ---: |
| Affected custom striped calls | 72 | 13 / 51 / 8 | 1.1282 |
| Custom Local7 controls | 12 | 0 / 3 / 9 | 1.0022 |
| Affected aligned resident reads | 4 | 4 / 0 / 0 | 0.8778 |
| Resident Local7/prior controls | 5 | 0 / 0 / 5 | 1.0013 |
| Complete bulk decode/encode controls | 12 | 1 / 0 / 11 | 0.9997 |

“Separated” means all six samples on one side lie beyond all six on the other;
it is not a confidence interval. All cases and both ABBA ordering edges remain
in the [paired table](evidence/striped-fragments-20260911/20260911T023824Z-887879d5/paired.csv).
There are 66/72 agreeing ordering pairs in the affected custom calls.

The [shape summary](evidence/striped-fragments-20260911/20260911T023824Z-887879d5/shape-summary.json)
shows the tradeoff. All six AVX512 crossing cases win (median ratio 0.837),
while all twelve last-lane cases lose (median ratios 2.171 AVX2, 1.981 AVX512).
Eleven of twelve custom full16 cases lose. AVX2 W6/H0 eight-value calls rise
2.828→7.152 ns/query (2.53×); AVX512 W12/H0 last-lane calls rise
4.396→10.219. A useful crossing win is AVX512 W5/H16,
12.505→9.109 ns/query. These are whole calls, not isolated sink cycle costs.

Aligned resident W5/W6 improves 8.7–14.7%, but the remaining W5 gap is still
large. The fastest W5 family changes 6.041→5.514 ns/query; the same-wire
ScanPack control is 1.803 before and 1.793 after, giving ratios 3.350→3.075.
These are this linked run's samples, not replacements for another program's
historical ratio. The candidate W6 beats the sampled ScanPack control, but
this bounded run contains no Calico comparison and does not establish parity
with the strongest alternative representation. The
[same-wire table](evidence/striped-fragments-20260911/20260911T023824Z-887879d5/same-wire-resident.json)
retains both variants and their explicit implementation choices.

## Maintenance, checks and decision

The candidate adds 102 private sink lines and changes native.cpp by +32/−24.
Worker text grows 5,409 bytes, from 3,050,091 to 3,055,500; rodata grows 2,664.
Only the striped-boundary family's total text changes. That family grows
114,489→119,898 bytes. The
[worker code accounting](../ikea-composition/archive/validation-20260911.md)
also records actual ordinary dispatchers and u64 boundaries: neither version
has inner calls or SIMD stack traffic in those selected boundaries. W5/u64
AVX512 shrinks 581→446 bytes but saves two registers instead of one; AVX2
grows 629→731 and saves four instead of one. These observations explain the
structural tradeoff without serving as an acceptance rule by themselves.
Baseline and candidate native compilation take 59.371 and 59.54 seconds on the
worker; these are observed sequential costs, not a controlled build-time gain.

A separate, unmeasured local revision moves complete-fragment admission before
clipping and explicitly skips inactive parts before reconstruction. It
duplicates residual reconstruction and grows total text by 41,810 bytes instead.
Its [patch](evidence/striped-fragments-20260911/20260911T023824Z-887879d5/refined-local.patch)
and [code accounting](../ikea-composition/archive/validation-20260911.md)
are retained separately; none of its timings are in the measured comparison.

All scalar/AVX2/AVX512 public wire checks and 3,737,352 exact guarded ranges per
target pass on both sides before timing. The initial candidate also passes
scalar/NEON checks locally, and both local caller variants pass 42 independent
preflights. The [audit](evidence/striped-fragments-20260911/20260911T023824Z-887879d5/audit.json)
verifies the 582-file capture/archive, source and common-harness changes,
compiler arguments, link inputs, archive replacement, 1,260 timings and 210
preflights. It independently reconstructs query hashes, checksums, strides,
extents, output alignment and shape classifications. CPU0 cases/repetitions
run sequentially, ABBA with three 30 ms repetitions, without background uploads.
[Provenance](evidence/striped-fragments-20260911/20260911T023824Z-887879d5/provenance.json)
contains recovery locations and compact hashes.

The bounded question is answered: one clipped native loop can remove some
resident traversal cost, but its general clipping cost makes this replacement
unattractive. The remaining primitive gap is not fully explained. This result
revives the notebook's [consumer-driven execution-grain question](../../notebook/ideas.md#plans-that-keep-improving):
price SeriesPack inside a useful compound consumer with equivalent work and a
strong materialized control, instead of continuing to rescue individual
primitive ratios. Compound recovery remains to be demonstrated in that context.
