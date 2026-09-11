# V2 runtime ranges across the scalar callback ABI

V2 does not reproduce [Zen's universal runtime gains](runtime.md).
Of 34 runtime cases, **19 improve and 15 lose by median CPU time**. Seventeen
improvements and twelve losses have separated raw sample ranges; 30 of 34
median directions agree at both ABBA edges. The strongest gain is 9.51%, while
the largest loss is 1.82%. Those small losses remain observations to resolve,
rather than being waived by the x86 benefit.

## Provenance and complete repetitions

Job `20260910T204747Z-aadd1c29` ran on a new c8g.large, CPU 0, Clang 21.1.8,
armv8-a with Neoverse V2 tuning. It used isolated capture
`seriespack-runtime-ranges-isolated-20260910`, digest
`2feb899a298657d5b59aa580dfedf371e61ee21200b6f53442198fdacf713cba`.
That capture changes only the runner's build-directory choice from the Zen
capture; the benchmark, independent wire oracle, query streams, checks and
three-file ABI candidate are identical. Each candidate build changes only
`operations.h`, `native.cpp` and the historical `physical.cpp` path.

The [audit](evidence/runtime-ranges-20260910/v2/audit.json) verifies all 490
source hashes against capture/manifest/archive, exact candidate sources,
35 fresh objects per variant, measured binary hashes and every command/check.
The scalar and NEON runtime checks each passed 34 cases and 279,552
guarded/oracle queries per variant; the original public wire, guarded range
and admitted checks also passed. Exact inventories are 34 runtime, 46
fixed-16/control and 10 bulk cases. All 1,080 raw samples and every runtime
query hash, stride, byte counter and complete timed checksum validate.

[Ordered individual repetitions](../ikea-composition/archive/validation-20260911.md)
retain every CPU and real-time sample, iteration count and requested counter
using the shared `evidence.summarize` helper. Labels preserve the four
before/after/after/before blocks; each case has three individual repetitions
per block, with a 0.03 s minimum. The
[provenance](evidence/runtime-ranges-20260910/v2/provenance.json) records exact
execution order and hashes, and the [artifact reference](evidence/runtime-ranges-20260910/v2/artifact.json)
recovers raw JSON, source, binaries and logs. Normalized
[comparisons](evidence/runtime-ranges-20260910/v2/comparisons.csv) retain all
medians, extrema, block medians and edge agreement. Times below are CPU ns/query,
using six raw samples per variant and 256 queries/benchmark iteration; extrema
are sample ranges, not confidence intervals. The exact
[caller/placement contract](runtime.md#exact-caller-workload)
is unchanged, including real u32/u64 output and mixed counts.

## All separated runtime losses

The twelve separated losses are:

| Shape / sink / stream | Before | Candidate | Change |
|---|---:|---:|---:|
| Local7 dense / u32 / `(1,17)` | 15.777 | 15.828 | +0.32% |
| Local7 dense / u64 / `(1,16)` | 15.684 | 15.766 | +0.52% |
| Local7 dense / u64 / `(1,17)` | 16.754 | 16.926 | +1.03% |
| Local7 dense / u64 / mixed | 13.015 | 13.158 | +1.09% |
| Striped12 dense / u32 / `(17,16)` | 10.784 | 10.944 | +1.48% |
| Striped12 dense / u32 / `(17,17)` | 11.657 | 11.738 | +0.69% |
| Striped12 dense / u64 / `(17,16)` | 12.166 | 12.388 | +1.82% |
| Striped12 dense / u64 / `(17,17)` | 13.093 | 13.161 | +0.52% |
| Striped12 dense / u64 / mixed | 10.776 | 10.876 | +0.92% |
| Striped28 independently gapped / u32 / `(17,16)` | 14.485 | 14.587 | +0.70% |
| Striped28 independently gapped / u64 / `(17,16)` | 17.040 | 17.330 | +1.70% |
| Striped28 independently gapped / u64 / `(17,17)` | 18.337 | 18.431 | +0.51% |

All twelve loss directions agree at both ABBA edges. For the largest loss,
Striped12/u64 `(17,16)`, before samples span [12.162,12.173] and candidate
[12.387,12.391]. The remaining three median losses have overlapping ranges:
Local7/u32 `(1,16)` +0.93%, Striped28/u32 mixed +0.16%, and Striped28/u64
`(16,16)` +1.45%. They remain in the evidence, as do the two overlapping
median improvements in Striped28/u32 `(17,17)` and u64 mixed.

## Wins, boundaries, sinks and placement

All twelve headed Local23 cases improve: u32 by 2.12–4.07%, and u64 by
0.80–1.32%. These gains are much smaller than Zen's. The strongest wins occur
at Striped origin16/count16 with u32 output: Striped12 changes 7.700→6.968
(−9.51%), and headed Striped28 changes 9.771→8.858 (−9.35%). The latter has
wider candidate samples [8.523,9.233], all below the before range
[9.769,9.774]. Its point estimate should not conceal that variation.

The sign depends on the boundary and sink. Striped12/u32 improves strongly at
`(16,16)` but loses at `(17,16)` and `(17,17)`; its u64 mixed stream also
loses. Candidate Striped12/u32 times are 6.968, 10.944, 11.738 and 9.361
for `(16,16)`, `(17,16)`, `(17,17)` and mixed, versus u64 7.186, 12.388,
13.161 and 10.876. For independently gapped Striped28, those sequences are
u32 8.858/14.587/15.767/12.743 and u64 12.420/17.330/18.431/15.874.
The unchanged lowering uses native16 at origin16 and scalar edges plus native8
at origin17. Boundary work and output carrier remain substantial costs after
the ABI change; this does not assign individual cycle costs to those operations.

Adding payload gaps to the already gapped Local23 heads makes candidate mixed
reads **faster** here: u32 15.304→14.635 (−4.37%), u64 15.897→14.995
(−5.68%), both with separated ranges. The baseline already has the same
direction. This differs from Zen's mixed-stream penalty. Fixed `(1,16)` and
`(1,17)` placement changes stay within 0.49%; the small u32 `(1,17)` increase
of 0.027 ns/query (+0.13%) has separated ranges, while the other three overlap.
These are the particular physical placements and streams measured, not evidence
that padding universally helps or hurts.

## Controls and limits on the ABI explanation

All nine ordinary fixed-16 cases improve 0.73–8.37%, with separated ranges,
despite the new runtime-range losses. Unchanged controls still move: predecessor
Striped7 changes 4.417→5.426 (+22.9%), predecessor Striped6 5.757→6.266
(+8.84%), Calico Striped5 7.557→8.171 (+8.13%), and admitted Striped5
5.902→6.128 (+3.83%); each has separated ranges. No raw-dense control has a
separated change. These controls have their original fixed-16 contracts and
are not arbitrary-range alternatives.

No retained bulk case has a separated loss. Local6/u8 decode improves
0.168221→0.165788 ns/value (−1.45%), the one separated bulk win. The other
nine bulk/control ranges overlap; the largest median loss is Local64/u64
encode +0.112%. Full block samples are retained rather than filtered by sign.

The actual linked NEON runtime callers load both runtime endpoints and retain
one indirect call, but their useful loop counts grow 21→22 instructions for
u32 and 20→21 for u64. The candidate preserves both endpoints across the call
for the final-element checksum index. This does not reproduce the x86 caller's
aggregate stack-marshaling reduction. It is code-generation evidence, not a
cycle explanation or a reason to ignore the observed losses. The paired
experiment also changes linked context, as the unchanged controls demonstrate.
The evidence supports an architecture- and regime-dependent outcome; the
private ABI should not be judged from aligned fixed-16 or Zen alone.
