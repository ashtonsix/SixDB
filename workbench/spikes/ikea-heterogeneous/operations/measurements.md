# Whole-bitset operation measurements

The full analyser can avoid substantially more encoding work than it costs on
the dense and mixed timing inputs, but adds cost when most windows compress.
The mask skips BEC work successfully; metadata strategy and native output grain
still matter. Plain/plain remains much cheaper on these repeated inputs, so
smaller storage alone does not establish a Boolean-operation speedup.

## Campaign and evidence

Clang 21.1.8, Release, independent translation units and no LTO. Each case pins
one CPU, warms once, calibrates its own pass count to at least 5 ms, then records
five sequential repetitions. Calls, rather than raw pass totals, normalize
comparisons. All rows in the final three-target campaign have zero timed page
faults and available PMU counters with running time equal to enabled time.
Cache residence is **unestablished**; these are repeated operation timings, not
cold random-access or measured cache-line counts.

| Target | Captured run | Compact evidence |
| --- | --- | --- |
| Zen 5 | `20260910T014653.848404Z-zen5` | [repetitions and reports](../evidence/operations-zen5/) |
| Granite Rapids | `20260910T014655.520320Z-granite-rapids` | [repetitions and reports](../evidence/operations-granite-rapids/) |
| Neoverse V2 | `20260910T014658.578977Z-neoverse-v2` | [repetitions and reports](../evidence/operations-neoverse-v2/) |
| Neoverse V2, grain one | `20260910T015156.275970Z-neoverse-v2-grain1` | [controlled algebra comparison](../evidence/operations-neoverse-v2-grain1/) |

Each target has 19,200 algebra rows and 720 analyser rows. Algebra covers both
operators, eight masks, sixteen datasets, thirteen complete-output configurations
and two selected-only controls. The compact selection preserves five repetitions
for six datasets, five masks and eight configurations, plus all analyser rows.
The full sweeps, source snapshots, executables and disassemblies remain in the
linked artifact bundles. Offline `report.py EVIDENCE` verifies retained hashes
before regenerating its tables.

The timing inputs contain eight triples per RealRoaring archive (twelve
archives) plus eight MS MARCO triples: 104 coordinate-matched triples total.
For Roaring, four per archive have both operands present and four are one-sided
windows, balanced between absent left and absent right. MS MARCO uses eight
distinct term pairs covering all sixteen prepared terms; all have common
nonempty high16 windows, so no absent side is invented. A distinct third source
supplies candidate-slice masks. Exact source identities, high16 values and
absence evidence are in the shared prepared object's `lineage.json`, referenced
by each capture. Three synthetic datasets add nonterminal dense, structural,
and empty/full cases. This is balanced coverage, not an observed query mix.

The analyser quality census is separately occurrence-weighted and much larger:
see [accuracy findings](analyser-findings.md). The sampled timing workload must
not be used to estimate corpus-wide decision accuracy. All four operand
representations coexist during setup; reported representation byte counts are
logical extents, not total process working set or allocated resident bytes.

## Analyser cost

Median nanoseconds per 8,192-byte input, rounded to the nearest ns:

| Dataset / stage | Zen 5 | Granite Rapids | Neoverse V2 |
| --- | ---: | ---: | ---: |
| Random dense: cheap full prediction | 718 | 939 | 907 |
| Random dense: quadrant full prediction | 1,306 | 2,059 | 1,741 |
| Random dense: cheap sample32 prediction | 124 | 178 | 156 |
| Random dense: body encode | 3,625 | 6,194 | 6,419 |
| MS MARCO: body encode | 3,080 | 5,193 | 5,638 |
| MS MARCO: cheap full predict then encode | 2,362 | 3,778 | 4,088 |
| census-income: body encode | 1,679 | 2,828 | 3,274 |
| census-income: cheap full predict then encode | 1,903 | 3,084 | 3,473 |

The timed decision uses the explicit **readable-extent** predicate, predicted
body +512 metadata +64 suffix <8,192. It includes prediction and native body
emission when chosen, with an observable actual-byte checksum. It excludes
directory emission, allocation, admission and downstream query cost. The public
storage helper instead defaults to aligned allocation; the two scopes are
both evaluated by the quality report. This pipeline therefore measures a
particular policy's avoided body work, not a complete conversion implementation.

On the MS MARCO timing selection, cheap full prediction reduces that body-work
pipeline by 23–28% versus encoding every input. On census-income it instead
adds 6–13%. The much faster fixed sample also has the severe aliasing failures
in the accuracy report. These results support a scalar estimate that an
enclosing analyser can use with workload information, not a blanket rule to
analyse before every encode.

## Masked work and metadata strategies

Complete-output intersection of two random dense BEC sources, median ns. Both
compressed inputs use LocalPack metadata16; this first comparison permits two
adjacent output slices per native region.

| Mask | Selected slices | Zen 5 | Granite Rapids | Neoverse V2 |
| --- | ---: | ---: | ---: | ---: |
| All | 256 | 13,824 | 13,085 | 40,401 |
| One cluster | 32 | 1,785 | 1,689 | 5,115 |
| Dispersed | 32 | 2,115 | 2,084 | 5,903 |
| Slice 255 | 1 | 128 | 157 | 254 |

Clustered and dispersed selections have the same cardinality but different
metadata reuse and adjacency opportunities. All-selected plain/plain takes
179/228/399 ns; the single-slice complete result takes 56/141/61 ns. In sparse
cases the output clear is a substantial obligation. Selected-only controls in
the evidence leave inactive bytes untouched and expose this separately.
The logical counters count selected slices, needed decodes, plain loads and
metadata refills; they are calculated from the fixture and execution rules,
not hardware traffic measurements or instrumentation inside the kernel.

Cached metadata is useful for dense selections but is not universally better.
For census-income all-selected intersection, LocalPack point lookup takes
4,283/4,580/11,954 ns versus 3,546/3,443/10,017 ns with metadata16. For one
random-dense slice, point lookup takes 120/147/245 ns versus 128/157/254 ns.
The curated LocalPack/LocalPack inline control is close to factored metadata
on dense random bodies, but improves census-income full intersection by about
3–6% and dispersed intersection by about 5–10%. Cheap terminal bodies make
the metadata seam more visible.

Mixed LocalPack/ScanPack layouts also support independent point/frame choices.
For full census-income intersection on Zen 5, frame/frame takes 3,554 ns,
point/frame 4,023 ns and frame/point 3,921 ns. The same authored decoder family
serves these choices; the native boundary has an actual cost rather than an
assumed zero-cost abstraction. [Assembly inspection](../notes/operations-boundaries.md)
checks which regions remain inline and where live metadata is stored.

## Native grain: the smaller kernel loses throughput here

The grain-one control ran next on the same V2 worker, with the same input object,
compiler, independent source strategies and output policies. It has another
19,200 algebra rows, all fault-free with complete PMU coverage. It changes only
whether adjacent selected ordinals in BEC-containing operations use a two-output
region; plain/plain keeps its pairs. The one-output BEC/BEC region still uses
the native two-input decoder for `A[s]` and `B[s]`.

Reducing output grain markedly reduces text size and stack pressure, but the
measured dense and mixed cases get slower. Complete intersection with two
LocalPack metadata16 sources, median ns:

| Dataset / mask | Grain two | Grain one | Grain one change |
| --- | ---: | ---: | ---: |
| Random dense / all | 40,401 | 44,156 | +9.3% |
| Random dense / cluster32 | 5,115 | 5,584 | +9.2% |
| census-income / all | 10,017 | 10,591 | +5.7% |
| MS MARCO / all | 32,283 | 35,107 | +8.7% |
| MS MARCO / dispersed32 | 4,702 | 4,699 | −0.1% |
| Terminal mixture / all | 3,642 | 3,575 | −1.8% |

The dispersed case already uses one output at a time; terminal cases fall back
to one-slice shortcuts. This limits the work that the control changes. The
grain-two path remains the default. Its larger live state is a real cost, but
fewer spills and a smaller frame did not compensate for the lost throughput in
these cases. The boundary audit records both actual executables, rather than
promoting the plausible assembly hypothesis to a performance conclusion.

## Correctness and limits

The [grain-two](../evidence/operations-native-sanitize/operations-checks.txt) and
[grain-one](../evidence/operations-native-sanitize-grain1/operations-checks.txt)
ASan/UBSan captures each cover 39,260 algebra cases over 302 masks, all four
independent source-strategy combinations and both output postconditions.
Checks include terminal bodies, ordinal 255, checkpoint/mask-word crossings,
alias rejection, admission, owner lifetime and poisoned inactive resources.
Native checks separately exercise exact plain load/store boundaries, nonadjacent
and reversed BEC pairs, all populations, model extraction, whole-window samples
and reference encoded sizes. Both providers and the previous range operation
also pass in every captured run. The extracted timing helper was smoke-checked
against the previous range benchmark.

[report_check.py](report_check.py) also regenerates raw and compact captures in
scratch directories and rejects deliberately corrupted accounting. The quick
smoke, first campaign, four final raw captures and four final compact exports
regenerated byte-identically. Twenty malformed inputs per combined capture
(fourteen for algebra-only) exercise missing cases/repetitions, inconsistent
work, impossible mask/frame counts, bad extents and invalid PMU records. No
integrity error was found in the actual final campaign. This report hardening
followed capture; it changes validation, not the retained measurement bytes.

This exercise does not encode Boolean results, implement raw escapes, establish
a confidence bound for predictions, validate a cold-resident workload, or build
Engine's conjunction planner. Such additions would change the operation and
its cost model; the present results should remain identifiable when they do.
