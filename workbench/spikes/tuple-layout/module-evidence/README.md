# Initial Ikea TuplePack implementation evidence

The maintained [TuplePack module](../../../../ikea/docs/tuplepack/usage.md) now supplies
manual descriptions, prepared maps, construction, checked point/range mutations,
native composition and owner effects. Engine analysis remains in the spike.
These measurements concern the Ikea endpoints, separately from the
[earlier 1,028-case experiment](../viability.md).

## Captured operating point

The [routine suite](../../../benchmarks/tuplepack/README.md) has 37 cases per
profile. Each has three sequential repetitions, pinned to one allowed CPU, with
Google Benchmark's minimum time of 0.03 seconds per repetition. Zen 5 AVX2 and
AVX-512/VBMI ran sequentially on one c8a.medium Spot worker; NEON ran on a
Neoverse V2 c8g.medium Spot worker. Both use pinned Clang 21.1.8, Release, no LTO,
and one build job. ASLR remained enabled. These are short microbenchmarks,
not full transactional workloads or PMU measurements.

[Zen repetitions](zen5/cases.csv) and [V2 repetitions](v2/cases.csv) retain every
sample and its item normalization. Their provenance and artifact references
retain source archives, measured binaries, flags, hardware, raw JSON and logs.
Jobs `20260912T100312Z-8474ceea` and `20260912T100312Z-716f4fed` captured the same
source archive. Subsequent changes only clarify documentation/comments and apply
formatting; the reformatted implementation passes `ikea_validate` again.

Median CPU ns per operation (per row for scan):

| Operation | Zen AVX2 | Zen AVX-512 | V2 NEON |
| --- | ---: | ---: | ---: |
| Ordinary scalar read, 1 / 2 / 4 codes | 1.34 / 1.18 / 1.74 | 0.91 / 1.18 / 1.74 | 2.18 / 2.36 / 3.70 |
| Ordinary scalar write, 1 / 2 / 4 codes, with effects | 2.02 / 3.16 / 3.71 | 2.07 / 3.16 / 3.70 | 3.46 / 5.56 / 6.18 |
| Same-wire effects control, 1 / 2 / 4 codes | 1.34 / 2.22 / 2.92 | 1.34 / 2.22 / 2.92 | 2.13 / 3.71 / 5.11 |
| Ordinary 64-code read / native body | 3.27 / 3.17 | 1.79 / 1.87 | 8.76 / 8.71 |
| Ordinary 64-code write / native body | 5.80 / 5.15 | 3.66 / 2.94 | 12.92 / 10.02 |
| Compound 128-code checked write / two native bodies | 13.99 / 9.47 | 10.50 / 8.35 | 26.50 / 19.87 |
| Four-row projection, 16-byte plane, 1,048,576 rows | 0.62 | 0.50 | 1.04 |
| Same projection, 64-byte rows | 1.51 | 1.36 | 1.94 |
| Decode/reduce/repack, inline / CPS | 8.88 / 9.36 | 4.85 / 5.76 | 17.51 / 20.56 |

The scalar controls use the same wire, widths, stride and repeated random trace.
The effects control includes range/width/capacity checking and the same qualified
journal, but knows the fixture's footprint rather than consuming a general bound
writer. Wide/native and compound controls are the module's own bodies; they are
boundary comparisons, not independent prior implementations. Ratios below one
do not establish a general intrinsic advantage: code placement, inlining and
control history differ. All comparisons exclude owner acquisition/publication.

## Remaining costs and their disposition

Small checked writes are the clearest remaining boundary cost: 1.21–1.62× their
effects controls, adding 0.68–1.85 ns. The module looks up the bound view, prepared
writer and variable issued-span list, then calls its selected point kernel.
The single-code case makes that fixed work particularly visible. Keep this
general binding/coverage contract for the initial module; do not add a parallel
fixture-specific ordinary API to erase fractions of a nanosecond. Larger or
diverse-map consumers should determine whether further preparation pays.

The V2 four-code scalar read is 1.40× its selected-byte control (3.70 vs 2.64 ns).
The prepared count-specialized body accounts for most of that cost (3.55 ns);
the ordinary shell adds about 0.15 ns. It is a bounded primitive gap, not hidden
per-field composition dispatch. Keep it with this limitation recorded.

The 128-code compound call is 1.26–1.48× the two unchecked bodies. Unlike that
control, it validates both native packets before writing either child and emits
qualified effects. The inspected NEON dense path accepts eight vector arguments,
performs native admission and reaches the stores without a required payload
round trip through an array. Sparse general fallback branches do use such a
bridge; they are not evidence of dense-path spills.

The two useful CPS stages cost 5–19% above inline, including the ARM entry shim.
This is a useful runtime operating point for shared bodies. It is not a new
large-catalog build-size experiment or a promise about tiny-stage pipelines.
The shared mechanism retains SeriesPack's execution model and existing evidence;
future consumers still choose stage grain and manual/inline fusion.

Wide sparse native writes, maps outside compact batch windows, many simultaneously
live prepared maps and contended/cold owner operations have correctness or prior
research evidence, not a tuned performance promise from this routine set.
Normalized routes retain their independent bit oracle and the compound evidence
in the spike; this suite does not re-run that historical matrix. Distribution
patching, JIT, SVE2, layout search and automatic selection remain research.

## What changed after measuring ordinary operations

Early module measurements repeated two familiar mistakes. Small writes traversed
general byte/contribution machinery; later, point calls still entered a one-row
range shell. The retained family now has a direct single-code writer, a bounded
word writer for nearby scalar codes, and a direct point admission shell. Wide
value admission uses a fixed reduction instead of a runtime early-return loop.

The batch reader also outlined a helper once per gathered row. Explicitly keeping
that small operation inline brought the four-row scan back near the prior spike's
operating point. The previous module's 16-byte-plane scan at 1,048,576 rows was
1.45/1.32/2.94 ns on AVX2/AVX-512/V2, versus 0.62/0.50/1.04 ns here. These are
different source captures, not paired measurements of one binary. The broad
lesson is to inspect and measure the ordinary operation before accumulating
format-specific exceptions.

Selected earlier captures remain recoverable through
[the Zen boundary reference](earlier-zen.artifact.json) and
[the V2 boundary reference](earlier-v2.artifact.json). They establish that
progression; current capability and performance claims use the final captures.

## Validation, ABI and compilation

All three measured profiles passed four independently compiled checks. Each wire
check reports 5,301,385 comparisons against a description-bit reference; each
ownership check reports 43,137 checks. Tests cover descriptors, exact guarded
extents, 128-code construction, whole-call rejection, shared-byte writes,
substitution, original-row masks, independent maintenance projections, and
exact/conservative/bypass publication with retained in-place work. The local
combined Ikea target also passed SeriesPack's independent reference, 206-format
composition/mutation checks and both modules' executable guides.
The [final formatted-source validation](final-validation.txt) records that last pass.

ASan/UBSan at `-O1` passed the four TuplePack checks and SeriesPack's pipeline
example; the native execution check also passed five consecutive reruns. All
35 ordinary/author headers compiled independently. The
[local bundle](local.artifact.json) retains validation, sanitizer binaries,
compile commands, the compiler reproducer and incremental-build logs.

Clang 21 AArch64 can retain a realigned caller frame base in x19 across a
`preserve_none` call even though a stage may clobber x19. A minimal optimized
release reproducer segfaulted; sanitizer-generated stage frames first exposed
the problem. The shared chain now enters through a fixed-frame AAPCS shim with
flattened vector arguments. Its prologue/epilogue saves caller registers once;
the payload stays in vector arguments. Only this entry shim excludes sanitizer
instrumentation to keep its frame fixed. Authored stages and completion remain
instrumented. A maintained regression test combines a realigned/dynamic caller
stack with deliberate stage clobber. Revisit the workaround with a toolchain change.

[Build/check details](builds-and-checks.json) distinguish module text from the
benchmark executable (which includes Google Benchmark and inline authors):

| Profile | Library text | Full routine build wall time | Peak process RSS |
| --- | ---: | ---: | ---: |
| Zen AVX2 | 35,926 B | 28.98 s | 258 MiB |
| Zen AVX-512 | 35,359 B | 28.12 s | 226 MiB |
| V2 NEON | 30,575 B | 34.43 s | 225 MiB |

The build includes the library, four checks, benchmark and its framework; it is
not an isolated library compile. Text excludes the small shared core and author
instantiations. [Incremental probes](incremental.json) used warm local OrbStack
ARM targets, one build job, and an mtime edit restored without changing source
bytes. Unchanged build: 0.02 s. A preparation edit rebuilt one TU plus relinks in
1.31 s; a native shuffle edit rebuilt seven TUs plus relinks in 11.50 s; an owner
example edit rebuilt only that example in 0.45 s. These establish responsibility
boundaries, not a paired pre-refactor speedup.

Reproduce performance with the documented routine runner. Recover either worker
bundle using `workbench/tools/artifacts.py fetch`, then use its source archive,
compiler flags and binary for a code-generation question. Raw sweeps and binaries
remain outside Git; the CSVs can be read without recovering a worker campaign.
