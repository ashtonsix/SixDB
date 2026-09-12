# Ikea2 replacement candidate — 11 September 2026

This is the pre-switchover assessment and measurement account. The implementation
now lives in [Ikea](../../../../ikea/README.md); [retirement and recovery](../seriespack-predecessor/README.md)
own the source checkpoints and transition. Historical case names and source
identities below describe the measured candidate, before its namespace change.

Ikea2 is ready to evaluate as the replacement candidate described by its
[capabilities and limits](../../../../ikea/docs/capabilities.md). Ordinary construction,
point/range mutation, whole-operation writable composition, physical recovery
descriptions, shared-body CPS and owner interoperation now have coherent caller
surfaces and executable guides. This account closes the campaign snapshot; it
does not promote Ikea2, implement Loom/Engine/Orbital, or promise parity in every
primitive case.

## Measured snapshot and validation

All three full sweeps used source digest
`b7be48ac4314bfe5313ccf1dc2290aec52fa3e21b51bc4f72fcc3f0163a99d85`,
captured as `build/captures/ikea2-candidate-v2`. The final packaging pass afterward
collapsed construction presets to `compact`, `bulk_x86` and `bulk_arm`, making H
independent. It changed the preset alias, its ordinary example and descriptor
static assertions; physical formats, kernels, operation bodies and benchmark
workloads are unchanged. The updated preset surface passed the local NEON
validation target, independent header checks, and AVX2/AVX-512 compilation of the
ordinary example and descriptor checks. Reader documentation was also completed
afterward.
Each evidence directory retains source/host provenance, repetitions,
comparisons, validation output and a recoverable S3 artifact containing source,
binaries and logs.

| Profile | Completed job | Evidence |
| --- | --- | --- |
| Zen 5, AVX-512 + VBMI/VBMI2/GFNI | `20260911T133645Z-f0473e27` | [Full sweep](evidence/20260911-candidate-zen5/summary.md) |
| Zen 5, AVX2 | `20260911T134212Z-c105fe6a` | [Full sweep](evidence/20260911-candidate-avx2/summary.md) |
| Neoverse V2, NEON | `20260911T134212Z-74118caf` | [Full sweep](evidence/20260911-candidate-neon/summary.md) |

All passed independent wire checks, the 76-headless/206-format placed and mutation
matrix, substitution/boundary/effect checks, exhaustive serialized ownership
checks, four executable guides and independent compilation of 21 ordinary/author
headers. The retained wire oracle has checked hashes and independent provenance;
neither validation nor the optional predecessor comparison needs the live Ikea
module. Local ASan/UBSan checks also passed for substitution, construction and
descriptor boundaries. This is not a claim of a full sanitized native matrix or
a concurrent owner implementation.

Builds use pinned Clang 21.1.8, C++23, O3 with debug information and assertions,
separate TUs, no unity build and no LTO. Timings are per-case medians of three
sequential pinned repetitions, 0.03 seconds minimum each. Most cases use 8,192
warm rows. They do not measure cold memory, page-COW faults, concurrency, scheduler
cost, publication or complete queries. Full sweep ratios remain intact even where
the longer targeted replays below differ.

## Performance disposition

Each cell is **median / worst ratio**, with the number above 1.4× in parentheses.
Lower is faster. The case count is per profile. These are distinct control
families; there is no useful single grand median across them.

| Work and control | Cases | AVX-512 | AVX2 | NEON |
| --- | ---: | ---: | ---: | ---: |
| Additional decode / frozen Ikea | 61 | 1.016 / 1.266 (0) | 1.134 / 1.441 (1) | 1.012 / 1.307 (0) |
| Decode / specialized same-wire prior | 15 | 1.028 / 1.287 (0) | 1.010 / 1.090 (0) | 1.017 / 1.608 (2) |
| Point / specialized prior scalar call | 15 | 0.996 / 1.023 (0) | 0.997 / 1.020 (0) | 0.996 / 1.196 (0) |
| Raw full write / prior or frozen Ikea | 76 | 1.021 / 1.390 (0) | 0.845 / 1.334 (0) | 1.159 / 1.509 (4) |
| Replacement + sum + coverage / materialized maintenance | 18 | 0.303 / 1.181 (0) | 0.498 / 1.059 (0) | 0.578 / 0.959 (0) |
| Placed native sum / native-read materialization | 18 | 0.890 / 0.957 (0) | 0.996 / 1.202 (0) | 0.955 / 1.443 (1) |
| Placed mutation / materialized maintained replacement | 18 | 1.033 / 1.503 (2) | 0.895 / 1.178 (0) | 0.879 / 1.038 (0) |

The placed set covers 20/H8, 31/H8 and 63/H16, each separated, interleaved and
substituted, with all/partial selection. Its materialized controls use the same
native read grain, a prepared physical bulk writer, and matching summary/coverage
obligations. They are stronger than the early campaign controls.

The ordinary bound-versus-concrete comparison needs separate interpretation.
Reads and bulk maintained writes are close to their concrete drivers. Four of
four repeated-point loops on each x86 profile, and one of four on NEON, exceed
1.4×; the worst ratios are 2.855×, 3.342× and 1.658× respectively. A concrete
point loop exposes the loop to optimization; an erased scalar endpoint pays the
call boundary for every value. The separate matched prior scalar-call comparison
above remains near parity. Retain both interfaces: use concrete composition or
one bulk binding for a known loop, and the ordinary scalar endpoint for point
calls. Do not portray this control difference as either a same-wire regression
or a free erased interface.

### Retained primitive and consumer exceptions

These are explicit maintenance choices for this candidate, not user acceptance
of a new universal slowdown budget. Exact case names and controls are in the
linked CSVs. No broad family remains at the previously unacceptable 1.7× median.

| Scope | Full-sweep exceptions above 1.4× | Interpretation and decision |
| --- | --- | --- |
| AVX-512 Local7 overwrite | 1.403× raw, 1.509× with coverage; 81.66/87.34 ns per 8,192 rows | A very fast specialized primitive magnifies small fixed driver/coverage costs. The 76-case raw sweep reports 1.390× for the same format. Retain the shared traversal and coalesced coverage instead of another per-width policy. |
| AVX-512 20/H8 partial maintained mutation | Separated 1.498×, interleaved 1.503×; 2 of 18 placed mutations | The headed partial driver maintains 16-row native groups; the materialized control combines wide reads, scratch maintenance and fast physical encoding. The cost is about 0.096 ns/row in this warm workload. Retain explicit traversal; larger headed maintenance grain is a focused future optimization if a compound caller makes it material. Substituted and all-selection cases do not share this gap. |
| NEON raw full writes | Striped4 1.476×, Striped10 1.410×, Local28 1.509×, Local36 1.477×; 4 of 76 | Retain the small exact-store/transpose families. Wide non-power-of-two bodies and mixed residual stripes are bounded primitive weaknesses; adding a separate encoder dialect for each would expand the maintained matrix. Do not attribute every ratio to a proven instruction-level cause. Maintained compound writes already recover the cost in this measured set. |
| Striped12 all-write coverage | NEON 1.591×; AVX2 1.472×; one of nine coverage comparisons on each | Coverage is coalesced outside the hot tile loop. Inspection of NEON code still shows repeated global-row/stride address arithmetic in body chunks; an explicit redundant alignment mask produced identical assembly and was discarded. A tile-local address redesign is plausible, but more local mask tuning is not. Retain the bounded gap; the all-width raw and sum-maintained results are reported separately. |
| NEON specialized reads | Striped5 decode 1.430×, Striped7 decode 1.608×; endpoint-only Striped7 range 1.524× | Shared reconstruction and range-entry costs are visible on these narrow specialized controls. Every-lane get16 comparisons stay below 1.4×. Retain the bounded primitive exceptions and the endpoint stress control, with longer replay evidence below. |
| NEON 63/H16 substituted sum | 1.443×; one of 18 placed sums | This deep wide-value reduction is the remaining placed read exception. Keep native substitution as the default authoring capability, with concrete inline/materialized alternatives available to a plan; no new width-specific execution policy. |
| AVX2 additional decode / endpoint-only read | Local28 decode 1.441×; Striped3 endpoint-only region 1.523× | One of 61 additional decodes and one of 15 endpoint-only regions. All-lane region cases stay below 1.4×. Keep the narrower source families and distinguish sink-sensitive microbenchmarks from full consumption. |

### Targeted replay

The hash-pinned replay specifications select the full sweep's non-CPS,
non-ordinary exceptions after observing them: [AVX-512](replay-candidate-zen5.json),
[AVX2](replay-candidate-avx2.json), [NEON](replay-candidate-neon.json). Each runs the
exact retained binary twice, with seven sequential 0.1-second repetitions per
case and matching controls. This tests repeatability of selected exceptions; it
is not a second independent coverage survey or a replacement for the full sweep.

The [AVX-512 replay](evidence/20260911-candidate-replay-zen5/summary.md) put Local7
raw/coverage at 1.295–1.330×, and the two headed partial mutations at 1.416–1.482×.
The [NEON replay](evidence/20260911-candidate-replay-neon/summary.md) reproduced
the raw-write, Striped12 coverage and wide substituted-sum gaps closely. Striped5
decode fell to 1.318–1.328× and Striped7 to 1.439–1.460×; its endpoint-only range
remained 1.543–1.563×. The [AVX2 replay](evidence/20260911-candidate-replay-avx2/summary.md)
kept all three selected exceptions: coverage 1.449–1.463×, Local28 decode
1.433–1.437× and the endpoint-only Striped3 region 1.558–1.566×. Thus the largest
short-sweep read/raw ratios are not all stable, but the named maintenance and
sink-sensitive costs cannot be dismissed as noise.

## A useful CPS operating point

Five representative source/map/predicate/sink recipes share bodies across inline
and CPS execution. Compare the best measured grain for each style, rather than
forcing both into the smallest packet. This selects from these measurements;
there is no held-out tuning claim.

| Recipe | AVX-512 CPS / inline | AVX2 | NEON |
| --- | ---: | ---: | ---: |
| Striped12 → add → upper bound → sum | 1.145 | 1.043 | 1.564 |
| Striped12 → xor → range → maintained write | 1.209 | 1.290 | 1.361 |
| Local23 → add → upper bound → maintained write | 1.233 | 1.099 | 1.287 |
| Local23 → add → actual empty result → maintained write | 1.388 | 1.215 | 1.431 |
| Local23 → multiply → low-bit predicate → sum | 1.216 | 1.252 | 1.294 |

The measured choice is 32 rows on NEON, and 64 on x86 except the first CPS recipe,
which prefers 32. These are useful working grains, not new wire formats. The
tested 32-bit carrier at 64 rows exceeds the NEON ABI's eight-vector budget.
For the especially cheap NEON reduction, inline or fusion remains the sensible
runtime choice. CPS earns its place when repeated stage reuse and dynamic plans
outweigh that boundary cost. It has not earned a blanket promise for every tiny
pipeline: the seven-stage AVX-512 stress case still reaches 3.594× inline and
remains explicitly labeled as stress.

The separate 36-recipe, 32-row catalog measures the intended compilation/size
tradeoff. It uses the same source/map/predicate/sink choices and reuses CPS stages.

| Profile | Compile seconds, inline → CPS | Code bytes, inline → CPS | Max process RSS KiB, inline → CPS |
| --- | ---: | ---: | ---: |
| AVX-512 | 1.560 → 0.748 | 46,876 → 5,014 | 209,640 → 181,740 |
| AVX2 | 1.775 → 0.790 | 52,849 → 5,698 | 220,592 → 181,360 |
| NEON | 2.920 → 1.202 | 47,476 → 5,064 | 242,080 → 172,068 |

That is roughly 9.3× less catalog code and 2.1–2.4× less serial compilation time.
CPS increases writable table data from 288 to 4,608 bytes. Debug information also
falls, but these are catalog objects/archives, not guaranteed whole-application
savings after linking and dead-code removal. The recipes demonstrate an operating
point for shared-body CPS; persisted plans and owner scheduling remain outside
the candidate.

## Source boundaries and edit cost

[Source responsibilities](../../../../ikea/docs/source.md) separate ordinary headers,
authoring headers, internal bodies and three compiled TUs. Journals, cold alias
analysis, erased endpoints and typed adapters no longer occupy one prepared
mutation header. Construction and admission share physical-field descriptions;
issued-store coverage has its own law. Physical and substituted bulk traversals
remain explicit because replacing them with a generic per-region visitor lost
good kernel performance at ordinary boundaries.

The [Zen incremental probe](evidence/20260911-candidate-zen5/incremental.json)
warms maintained validation/examples/benchmarks, then touches one file at a time
with two build jobs. It restores mtimes; source bytes do not change.

| Representative edit | Compile / other completed edges | Wall seconds | Max process RSS KiB |
| --- | --- | ---: | ---: |
| Unchanged build | 0 / 0 | 0.00 | 18,328 |
| Compiled admission rule | 1 / 8 (archive and relinks) | 0.58 | 334,636 |
| AVX-512 native write header | 24 / 7 (relinks) | 131.02 | 1,743,372 |
| Teaching integration adapter | 1 / 1 (relink) | 2.68 | 331,432 |

All tracked artifact hashes were unchanged after the touch rebuilds. A native
write edit still recompiles a substantial template validation matrix; it does
not rebuild the compiled dense reader or read-only tests. This cost is deliberate
and visible, not solved by moving declarations between headers. Width sharding
and test-only noinline wrappers bound compiler memory without changing measured
kernel inlining. The final Zen build, including historical comparisons, took
289.73 seconds with 2,580,400 KiB maximum single-process RSS in its existing build
directory; it is not a clean-build baseline or aggregate concurrent RSS.

The AVX-512 compiled library has 398,188 code bytes (4,939,996-byte archive with
debug information); the broad validation executable has 12,512,202 code bytes.
The latter reflects many explicit instantiations and is not consumer binary size.
No reduction relative to old Ikea is inferred from formatted line counts or from
this unpaired build. The narrow cold-edit boundaries and catalog results are the
measured maintainability gains.

## What remains outside this candidate

The maintained entry points are now reader-oriented guides and executable
examples, supported declarations with borrowing/failure contracts, behavior-led
tests, and selectable routine benchmarks. Three presets are the construction
starting surface; 206 validation formats are not 206 named promises. Historical
controls and execution alternatives remain recoverable here, behind an opt-in
comparison target.

Future work follows consumers: TuplePack/StreamPack formats, real owner adapters,
semantic containers, progressive filtering/planning, and consumer-driven tuning
of the named exceptions. It need not reopen the completed SeriesPack campaign
checklist. The next engineer can construct and mutate data, substitute a child,
share a native body across inline/CPS, diagnose an admission failure and validate
the change using the candidate documents and targets, without this conversation.

## Switchover

The [retirement and recovery note](../seriespack-predecessor/README.md) owns the
authored-source checkpoints, retained references and historical workload routing.
The final active-name change preserves the measured kernel bodies; original
campaign artifacts remain authoritative for original performance numbers.
