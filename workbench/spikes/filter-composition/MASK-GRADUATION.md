# Shared mask reductions and corrected throughput traces

The production NEON u32 predicate-to-mask helper now weights its four full-word
predicates with `{1,2,4,8}` and sums them directly. It uses no narrowing and no
scalar lane reconstruction. The format, APIs, query-domain handling, activity
masks and filter-selection defaults are unchanged.

The [corresponding sixteen-slot table](seriespack-widths.md) covers all five predicates and
full/sparse activity on Zen 5 AVX2/AVX-512, Granite Rapids AVX2/AVX-512 and
Neoverse V2 NEON. It keeps dependent latency and reciprocal throughput separate.

## Why this reduction

The [original diagnosis](NEON-MASK.md) caught scalar extracts and byte inserts in
the zero-padded double narrowing. Its single-narrowing control proved the cause
and removed most of the loss. During graduation, the audit found the same bad
sequence in the independently fused NEON baseline. Repairing that baseline with
a full-word weighted sum exposed a simpler, faster reduction than the first fix.

In the native standard-width baseline consumer, the initial byte-narrowing fix
took 11.69 ns dependent latency and 2.80 ns reciprocal throughput at K32 equality;
the independently implemented word-sum control took 9.88 and 2.02 ns. This is a
paired consumer comparison, not an isolated instruction measurement. It prompted
the final word-sum implementation and a fresh NEON width sweep. The final paired
control retains the earlier byte reduction to isolate this refinement.

The shared helper is used by SeriesPack's ordinary, prepared and native-group
paths. TuplePack uses the same helper family **with byte lanes**, so its field
and native-packet paths never used the faulty u32 reduction. Their public paths
and emitted reductions are checked; no unrelated TuplePack kernel change is
needed to propagate this fix.

## Final native results

Selected default-filter equality cells from the final width sweep, **ns per
16-value window**, median of five 100,000-iteration repetitions:

| K | V2 dependent latency | V2 reciprocal throughput |
|---:|---:|---:|
| 8 | 10.65 | 1.51 |
| 16 | 10.62 | 1.64 |
| 17 | 16.35 | 4.55 |
| 24 | 10.41 | 2.43 |
| 31 | 20.84 | 5.79 |
| 32 | 9.73 | 2.72 |
| 33 | 19.72 | 7.63 |
| 64 | 13.62 | 5.84 |

The longer paired diagnostic uses seven 500,000-iteration repetitions and the
same distinct-stream trace for both reductions. It isolates the last refinement
in a separate consumer loop; its absolute numbers need not match the full sweep.

| K / predicate | First byte fix latency | Final word sum latency | First byte fix reciprocal throughput | Final word sum reciprocal throughput |
|---|---:|---:|---:|---:|
| 17 == | 18.56 | 17.40 | 5.98 | 4.70 |
| 24 == | 12.78 | 11.28 | 3.26 | 2.53 |
| 32 == | 12.06 | 10.35 | 3.00 | 2.62 |
| 32 < | 12.11 | 9.82 | 2.97 | 2.48 |
| 32 <= | 12.13 | 9.77 | 2.93 | 2.49 |
| 32 > | 12.16 | 10.45 | 2.99 | 2.64 |
| 32 >= | 12.01 | 10.43 | 2.98 | 2.61 |

K32 equality throughput spans 2.52–2.81 ns in the short full sweep and
2.57–2.70 ns in the longer final-word control. Raw repetitions are retained;
hundredths of a nanosecond are display precision, not a claim of that accuracy.
The original pre-fix table used a different stream trace, so its old-versus-new
differences are not isolated kernel speedups. The original and final paired
controls establish the two reduction improvements.

## Harness correction

Monomorphic and mixed-catalogue throughput now use `i + stream*0x9e37` as their
per-stream salt. Stream zero preserves the serial trace. Equal masks no longer
lock all eight recurrence registers to one shared address; occasional window
collisions remain. Every result still selects its own next window, and all eight
filters execute. The base packed-filter benchmark receives the same correction
and warms every stream. The early-exit harness already used distinct salts.

These remain deterministic, warmed, finite-working-set consumers. Distinct salts
are not a claim of uniform sampling or DRAM residence. Salt arithmetic, query
loads, result/address dependency and loop control remain inside timing. In the
mixed catalogue, schema routing and code placement also affect the result.
Historical CSVs retain their original traces and are labeled accordingly.

The correction also changes the mixed-catalogue conclusion. In the current
320-entry catalogue, prepared fusion beats selective sharing in both metrics on
all five profiles. These are complete operations, in ns/window:

| Platform | Fused latency | Selective latency | Fused reciprocal throughput | Selective reciprocal throughput |
|---|---:|---:|---:|---:|
| Zen 5 AVX2 | 30.78 | 33.01 | 22.84 | 27.90 |
| Zen 5 AVX-512 | 27.72 | 31.78 | 20.24 | 27.48 |
| Granite AVX2 | 34.69 | 37.52 | 24.65 | 31.61 |
| Granite AVX-512 | 30.47 | 34.60 | 19.40 | 28.27 |
| V2 NEON | 37.99 | 41.72 | 22.95 | 32.49 |

The earlier x86 throughput win is withdrawn as a recommendation. Sharing remains
an authoring option for code size or an already-needed CPS boundary; it has no
automatic catalogue-size performance crossover established here. The consumer
guide and historical report now make this distinction explicit.

## Code-generation coverage

[`mask-codegen.cpp`](mask-codegen.cpp) exposes runtime-input witnesses for all
five predicates and 8/16/32/64-bit lanes at every supported vector width. It also
covers TuplePack's 8-byte and 64-byte packets. [`mask-audit.py`](mask-audit.py)
checks the emitted code for scalarized reconstruction, unexpected calls and
spills in the raw-vector helpers. It rejects the pre-fix implementation for
exactly the five NEON u32 predicates. Complete runtime consumers and CPS stages
are separately audited, so a clean standalone witness is not the only evidence.

| Family | Emitted reduction |
|---|---|
| NEON u8 | Vector weighting, two eight-byte horizontal sums, scalar result assembly |
| NEON u16 | One vector narrowing, weighting and horizontal sum |
| NEON u32 | Full-word weighting and horizontal sum; no narrowing |
| NEON u64 | Two predicate-lane extracts and scalar bit assembly |
| AVX2 u8 | `vpmovmskb` |
| AVX2 u16 | Vector packing plus `vpmovmskb`; 256-bit inputs join their halves |
| AVX2 u32/u64 | `vmovmskps` / `vmovmskpd` |
| AVX-512, all lane widths | Direct compare-to-mask plus `kmov` |

No analogous scalarization was found in the other production reductions. The
TuplePack8 witness drops the unused upper-half byte reduction. The independent
baseline's NEON u32 defect was repaired separately; baseline code still calls
no Ikea decoder or predicate kernel. Clean code generation does not establish
optimality for every processor or enclosing workload.

Some x86 CPS decode/projection bodies retain stack traffic; these are not the
raw mask helpers and are not claimed to be free boundaries. The complete-consumer
timings charge that work. The ordinary filter remains the recommended starting
point.

The final NEON full-width binary contains 355 inlined measurement consumers with
no halfword-extract/byte-insert sequences. Its K32 equality measurement function
contains 40 vector comparisons and 40 word sums across warm-up, serial and eight
throughput copies, with zero `xtn` and zero `umov`. All ten full-result address
mixing multiplies remain. Disassembly SHA256:
`2f209d07f753ff5c48bd75854175b488e9dfbb831387baac552fd51ea5699950`.

## Validation and evidence

The production filter checker now exhausts predicate patterns for reductions
with up to 16 lanes and checks wider packets across every sublane boundary.
Its independent expected values cover unsigned endpoints and all five operators.
Existing checks cover every SeriesPack width, Local/striped/head arrangements,
ordinary/prepared/wide paths, sparse activity, query-domain boundaries, TuplePack
field geometries, packet coordinates and protected-page boundaries. Native
ordinary, composed and guarded examples are also run.
All native filter checks and three examples passed on all five selected profiles.
Both runtime panels' independent oracles passed, including the TuplePack field
and composed projection cases. The final NEON paired control additionally passed
all K17–32 predicates, boundary queries and activity masks. Local Linux/NEON
checks and the repaired independent-baseline smoke run also passed.

The first five-profile cohort is `20261015T183617Z-5c7ed728`, using one captured
source. The final refinement changes only the NEON branch: x86 comparator text
and both runtime harness sources match their measured capture. Raw measurements,
all repetitions, checks, host/source receipts and mask disassembly are retained;
full binaries, complete disassembly and captured sources are recoverable from
each evidence folder's archive reference.

- [Zen 5](evidence/mask-graduate-zen5/): AVX2 and AVX-512.
- [Granite Rapids](evidence/mask-graduate-granite/): AVX2 and AVX-512.
- [Initial NEON qualification](evidence/mask-graduate-neon/): byte-narrowing fix.
  Its superseded timing CSVs live in the verified archive; compact checks and
  codegen evidence remain here.
- [Independent baseline comparison](evidence/mask-baseline-neon/): exposed the
  further full-word reduction improvement.
- [Final NEON qualification](evidence/mask-final-neon/): full-word production
  reduction, full width and TuplePack panels, and paired prior-byte-fix control;
  job `20261015T184858Z-58928354`. This supplies that table's NEON column.

Generate the width table with [`width-table.py`](width-table.py), supplying
`Platform=runtime.csv` inputs and `--output seriespack-widths.md`. It requires
every K1–64/predicate/activity/metric cell with exactly five distinct repetitions,
then emits Markdown, numeric medians and source hashes. The selected table
contains 3,200 latency/throughput pairs from 32,000 raw samples.

[`mask-workers.json`](mask-workers.json) runs the native qualification through
[`mask-graduate.sh`](mask-graduate.sh). [`mask-baseline.sh`](mask-baseline.sh)
runs the separately implemented NEON fused controls. Prefix Linux commands with
`orb -m ubuntu` from the macOS checkout.
