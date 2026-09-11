# GNR short materialization stores

Splitting each AVX512 output store into two 32-byte stores removes the roughly
6 ns floor in the affected simple short-read callbacks while retaining ZMM
reconstruction. All 40 changed admitted/raw callbacks improve by 20.2–69.9%,
with separated six-sample ranges and agreement at both ABBA ordering edges.
This identifies a useful short-materialization implementation context. It does
not establish a blanket bulk-store rule or isolate a hardware latency.

## Matched comparison

Job `20260910T204425Z-5872e4fa` reused the GNR worker on pinned CPU0. Its
baseline is the scalarized callback program from the earlier
[ABI comparison](../ikea-composition/call-boundaries/decoder.md). The runner reproduced both its
benchmark and guard binaries byte for byte from the cached link inputs before
building the replacement region TU. All 486 cached source files remained
unchanged; the runner recorded and rehashed 28 explicit/implicit link inputs.
The independently checked returned files, identities and limitations are in
the [audit](evidence/gnr-split-stores-20260910/audit.json).

Only the benchmark-local AVX512 materializing store helper changes. It keeps
source reads, original coordinates, independent admissions, working values,
output extent/alignment and the immediate scalar checksum consumer. Each ZMM
store becomes a low-half YMM store and a high-half `vextracti64x4` memory store,
with an empty compiler barrier preventing remerging. The extraction and changed
scheduling are part of the measured implementation. No inner call is added.
Exactly 40 AVX512 callbacks change; all 40 AVX2 callbacks and the selector have
identical instruction/relocation listings. All four before/after native guards
pass, each checking 20 descriptions, 400 protected placements and 6,744 queries.

The [compact evidence](evidence/gnr-split-stores-20260910/provenance.json)
retains every block and repetition across 375 resident and 218 bulk cases,
including unchanged controls. Order is before/after/after/before, three
sequential 30 ms repetitions per block. Resident iteration times divide by
256 queries, bulk by 8,192 values. Sample extrema describe the measured runs;
they are not confidence intervals. The recovery bundle holds the measured
binaries, source, commands and full logs.

| Shape | Admitted before → split | Raw before → split |
| --- | ---: | ---: |
| Local1 | 6.500 → 1.959 | 6.233 → 1.907 |
| Local7 | 6.243 → 1.983 | 6.225 → 1.901 |
| Local56 | 6.245 → 2.396 | 6.276 → 2.277 |
| Striped1 | 6.224 → 1.937 | 6.243 → 1.890 |
| Striped5 | 6.777 → 4.871 | 6.881 → 5.492 |
| Striped12 | 6.758 → 2.493 | 6.729 → 2.510 |

Numbers are pooled median ns/query. All shapes and their block/sample ranges
are retained in the [comparison table](evidence/gnr-split-stores-20260910/comparisons.csv).
The remaining striped costs belong to these concrete leaf/store combinations;
there is no universal residual constant to subtract.

## Limits and implementation direction

The nearest Local1 controls stay near their previous levels: ordinary AVX512
7.27048 → 7.27069, predecessor 6.23899 → 6.26204, ordinary AVX2
3.66040 → 3.66375 and admitted AVX2 2.15410 → 2.14603 ns/query.
The unchanged public AVX512 endpoint retains its floor. This experiment changes
only the diagnostic store body; the ordinary callback still needs the targeted
implementation and whole-caller comparison.

Other unchanged controls move materially. Ordinary AVX2 Local16 improves 25.4%,
while AVX2 Local1/u8 bulk encode regresses .011000 → .013147 ns/value (19.5%)
with separated ranges and consistent ordering edges despite unchanged encoder
source/object. There are 28 bulk cases with separated loss ranges, all retained
in the [summary](evidence/gnr-split-stores-20260910/summary.json). These effects
cannot be corrected with one normalization factor or attributed to changed
decode execution.

The retained ZMM work completing below 2 ns rules out an unconditional ZMM
computation floor in this workload. Source isolation, all-40 directional gains
and stable nearest controls support curating the short output store body while
keeping working lanes independent of output policy. Store forwarding remains
a plausible mechanism rather than an isolated measurement. A subsequent
ordinary runtime-range comparison must retain its actual stores and scalar
consumers, while native consumers and long bulk output remain separate useful
contexts. The experimental compiler barrier itself is not a production policy.

The [completed ordinary-store follow-up](ordinary-stores.md) confirms
the small existing seam and whole-call gains, while retaining short scalar-tail
regressions and the exact candidate’s uninstalled status. It is separate from
the rejected general materializer.
