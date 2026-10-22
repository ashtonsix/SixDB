# Raising the Intervals performance bar

Ashton asks for substantially stronger performance work than the initial local
sweep: inspect complete consumer disassembly, consider microarchitecture, close
as much of sparse G64's gap to G16 as possible, and preserve all three consumers.
Ikea LEAD has been asked for a concrete research plan and iteration techniques.

## Result and current decision

Keep serial execution by default. The measured addition is an explicit G64 prefix
throughput binding, using the same stored bytes and original inverse route. It
substantially closes the random C8 rank gap, while C1 and clustered callers should
keep serial. No universal spacing/backend choice or production migration follows.

Final native warm M255/N256 **rank→payload, eight independent chains**, ns/request:

| Host / ISA | Frozen G16 | Frozen G64 | G64 independent prefix | Speedup over frozen G64 |
| --- | ---: | ---: | ---: | ---: |
| EPYC9R45 Zen5 / AVX2 | 1.359 | 6.112 | 1.970 | 3.10× |
| EPYC9R45 Zen5 / AVX512 | 1.356 | 6.298 | 2.069 | 3.04× |
| Graviton4 Neoverse V2 / NEON | 1.917 | 6.148 | 3.316 | 1.85× |

C1 G64 instead moves8.569→11.067 ns on AVX2 and 11.966→16.501 on V2; keep serial
for that dependency topology. Clustered C8 loses10–18% on x86 u8 and46% on V2 u8.
V2 u16 random C8 improves6.985→4.792 ns; x86 u16's serial fallback loses about11%
in this binding and earns no recommendation. Random partialN63 preserves the u8
C8 benefit. The same M255 image uses 264 readable geometry bytes at G64 versus 288
at G16, with no new sidecar or change in the candidate's stored data.

The older precomputed query witness is a different workload: M255/N256 G64 rank
moves2.125→1.633 ns on AVX2 but2.515→3.021 on V2. V2 range8 improves9.794→8.966,
while AVX512 full traversal loses151.554→158.995. These losses remain in the
retained tables; the binding is not a general range or ready-request default.
The provisional local targets were not all met. The earned result is narrower
than a broadly faster G64 representation.

## Prior results change the starting point

The [initial findings](FINDINGS.md) preserve the G64 read/write tradeoff and the
losing StreamPack/tiny-run wrappers. The [Boundlist API audit](../boundlist/API.md)
found outlined callables/spills and expensive selection checks; its fixes came
from linked consumers, not isolated kernels. The [portfolio](../boundlist/PORTFOLIO.md)
found narrow address fields and cursor-reference state that lost complete-operation
performance despite kernel/construction wins. Keep those strong typed controls,
source-qualified writes, and useful payload/child consumption.

The [MCA guide](../../tools/mca.md) documents important model limits: local Apple
selection uses an older scheduling proxy, Zen5 selection uses a Zen4 scheduling
proxy, and a recognized CPU name does not establish its actual scheduling model.
MCA is a dependency/resource diagnostic, not native timing or a cache model.

## First linked-assembly diagnosis

The existing G64 u8 inverse callback routes four checkpoints through scalar loads,
comparisons and mask construction because the generic native mask has no 64-bit
vector load for four u16 fields. Its local-window walk executes a horizontal sum,
vector-to-GPR transfer and conditional branch for each skipped native window.
Rank-prefix reads likewise reduce each prior window separately. These add dependent
steps beyond the necessary extra width data and deserve targeted experiments.
The complete callback also includes query setup, bounds, native owner, child load,
accumulation, spills and loop control; preserve those costs during comparison.

First hypotheses: vector-count a small sorted checkpoint directory with an exact
read grant; combine native window sums in widened vector lanes before one horizontal
reduction; compare linear versus hierarchical/parallel inverse routing if the
remaining dependency is material. Keep checkpoint spacing independent of native
execution width, avoid new sidecars, retain strided siblings, initialized native
tails, duplicate/zero groups and wide origins. No source-byte or format change is
required to test the first two hypotheses.

Functional independent oracles precede timing. Compare frozen before/current,
maintained typed G16 and direct controls. Separate dependent useful request chains
from genuinely interleaved independent chains. Inspect each candidate's linked
loops, then use literal selected paths for scheduling diagnostics. Native tests
must include the owning consumers and losing cells; a local warm improvement is
not a Zen5/DRAM result. Record rejected candidates and follow the signal.

## Ikea LEAD's bar-raiser plan

LEAD agrees that G64's current losses deserve algorithm work before a spacing
policy verdict. Audit address waves and all three complete consumers; test bounded
parallel prefix reduction, four group totals/cumulative selection and whole64
prefix controls against the sequential route. Keep G16 and tiny Frame unaffected,
carry ranges, and explain the prior Relative Ends full-value regression with actual
linked code/layout. Provisional local M255/N256 experiment targets are G64 rank
≤1.7 ns, inverse ≤4.1 ns and eight-range ≤7 ns, while preserving full-range and
checked-write gains. These are engineering targets, not predictions or a contract.

LEAD recommends frozen before/current plus maintained typed G16/direct/fused
controls, at least4/8 genuine independent streams separately from chained latency,
random and clustered ranks, all consequential tails/zeros/boundaries, recorded
bytes and preparation. Native Zen5 first and Neoverse V2, with explicit ISA/tuning,
then Granite if useful. Complete caller compiler assembly and final linked dump
precede literal-path MCA. No fleet work overlaps from Frame; local CPU8 is reserved
for this study, Frame7 and Engine6 remain separate. The existing witness is useful
but its four accumulator lanes do not establish independent memory-request chains.

## What the first iterations rejected

Widened pairwise accumulation plus vector CP4 selection improves the old batch
loop but regresses important true chained/interleaved cases. Four-total parallel
routing followed by table-select removes local branches, but the linked body has
multiple horizontal-to-scalar-to-vector handoffs and costs substantially more C1
latency. Keeping those handoffs in vectors helps only slightly. A whole64 vector
prefix scan shortens that route's serial dependency but consumes more vector work
and loses throughput to the two-level32/16 tournament. These are retained negative
controls, not universal replacements.

The first native candidate preserved the serial source by default and offered
`for_independent_reads()` as an explicit ready-work binding over the same bytes.
It changes no portable schema or owned bitmap. At a full initialized G64 region,
its batch path read four native windows and used masked widened prefix work;
the NEON u8 owner used the tournament. A last region with at most48 assigned ranks
falls back to exact native groups. Guard-page checks cover both the32-field fallback
and63-field/full64 padded case, plus strided u8/u16 siblings. Random and clustered
patterns remain separate: independent streams do not make every wider algorithm
appropriate for every distribution.

## StreamPack instruction audit

The local full Relative Ends baseline and fused-owner callbacks have101
instructions each. After normalizing local jump targets, their instruction text
differs only in the first paired load's capture-pointer register ordering. There
is no added geometry call, stack spill or extra value/kernel instruction in that
fused path. This rules out the earlier8% cell being an added generic geometry
operation; closure/code placement and measurement variability remain explanations
to test, rather than claiming machine-byte equivalence or universal timing parity.
The generic start+length diagnostic is still a different body and retains losses.
Native cohorts keep the fused and generic arms alongside the maintained control.

## Native correction and callback-layout control

The first native sweep (Zen5 AVX2/AVX512, Graviton4 Neoverse V2) narrows the win:
independent random rank C8 improves substantially, while tournament inverse often
loses. The active independent binding now changes prefix execution only; inverse
keeps the existing serial route. Rejected vector checkpoint and owner variants
remain in `control/rejected_native.h` and the frozen worker sources, rather than
adding an unearned inverse policy to the seam.

Native linked before/current callbacks contain thousands of instructions because
one callback includes all six operation/chain loops. No per-query calls were
found. Separate noinline **complete chain loops**, called once per4096-step batch,
make the new latency/throughput witness auditable without imposing a geometry
call boundary. Earlier and revised benchmark cohorts must not be pooled.

The protected32-tail sanitizer found an existing zero-residual read: the original
prefix skipped its final native load only at `take == Spacing`, so G64/take32 read
a nonexistent third native window. The candidate now returns whenever
`take % 16 == 0`. Protected 16/32/48 tails exercise default and independent sources.
The frozen before control remains literal and runs only its original padded
benchmark fixtures, not this invalid read grant.

[Executable placement](../executable-placement/evidence/checked-point-layout-20260911/summary.md)
previously demonstrated material movement of unchanged instructions, with a
smaller unexplained residual after restoring placement. It does not license
calling every unexplained loss a layout effect. Here, native StreamPack fused
and maintained typed paths have equal instruction counts; their only substantive
instruction differences are closure capture pointer order/register assignment.
The seam witness now gives those two closures the same explicit capture order,
so a wrapper does not accidentally select a different callback layout. Compare
its separate cohort; do not subtract placement costs from any losing measurement.

The first safe-prefix correction added a zero-residual branch on every rank
lookup. A follow-up keeps the original admitted `start(rank)` path: because
`rank < count`, its final native window exists even for a zero residual. Endpoint
prefixes instead select a one-based final window (`take-1`, residual1..16), so they
never need a following-window read at an exact native tail. This separates two
existing read grants, rather than padding the owner or taxing the common start.

## Dependency and resource interpretation

For rank→payload, rank determines checkpoint and native window addresses; their
loads can overlap. The old G64 walk then performs up to three separate horizontal
sums before the masked final16 result can address payload. A full macroregion
binding instead issues four native loads, masks by rank, widens/combines their
partial sums and reduces once. It spends more loads, vector work and registers
on small prefixes in exchange for less per-request branch/reduction work when
several independent requests are ready. On NEON the u8 total fits u16, while u16
widths require u32 accumulation. On x86 u8 uses SAD with AVX2 or AVX512 masking;
strided windows still need separate loads/composition. The u16 x86 fallback is
serial and earns no speed claim.

For coordinate→child, checkpoint selection must complete before the local window
address, then the local owner must complete before the child address. The old
G64 loop's sum→scalar compare/branch can be cheaper than loading and scanning all64,
especially when C1 or clustered queries favor early windows. The tournament adds
sum32→scalar selection→vector select→sum16→scalar selection→vector select→owner16.
Removing branches does not remove that dependency. Full64 prefix/count spends
more vector execution bandwidth, and table routing adds table-select latency and
register pressure. Native losses reject those routes as defaults; lower modeled
instruction count or reciprocal throughput alone did not predict a win.

Literal V2 fixed47-prefix diagnostics model3.33 cycles/block for the old path and
2.67 for full-mask accumulation; the rejected partial accumulator models2.17 yet
loses important complete consumers. CP4 models4.0→2.17, but its real composition
also loses. These are backend reciprocal-throughput estimates, not latency.
Fixed47 specialization can eliminate constant-zero loads/masks that dynamic rank
callers retain. Synthetic repeated-region cycles have a pointer/result register
dependency absent from independent calls, so they are deliberately not quoted.
Zen5's LLVM21 selector uses a Zen4 scheduling model; native Zen5 AVX2/512 timings
own that comparison. No PMU stall attribution or cold-cache service is inferred.

## Final native audit and qualification limits

All 20 emitted StreamPack maintained/fused callback pairs have identical normalized
complete instruction bodies in **each** native profile after matching explicit
capture order. Normalization preserves registers, scalar/vector work, spills,
loads, immediate constants and exact local branch offsets; only PC-relative data
displacements and local absolute branch addresses are normalized. On V2 all 24
frozen/current serial chain pairs are equal. On x86, 12 are equal directly; the
other 12 differ only in PC-relative references to duplicated constants, whose
actual u16 bytes were checked equal in the measured ELF binaries. No per-query
geometry call was introduced. `control/audit-linked.py` makes the comparison
repeatable against recovered final dumps.

Instruction equality did **not** remove timing differences. Zen5 AVX512 Absolute
Ends+U fused/maintained remains1.17–1.18×, while AVX2 is0.85–0.86×; V2 StreamPack
is0.996–1.030× across its nine shapes. Frozen/current x86 C4 G64 inverse still moves
5.55→9.94 ns despite equivalent hot work. Linked placement, constant placement
and execution history remain possible causes; this experiment does not isolate a
specific predictor/cache mechanism or establish timing equality by inspection.
Do not call those observed losses noise or subtract them from a comparison.
Actual Directory before/after implementation bodies are namespace-only changes;
its 40 Object checks pass. Directory native logical-key seam comparisons are near
parity on V2, with x86 variation up to about8%. Frame's native-byte projection
wins strongly on V2 (about4.8× at N16), while x86 has mixed winners and losses:
keep its stronger maintained projection where that wins. There is no new generic
wrapper forced on any of the three consumers.

Three native cohorts remain distinct: first combined callbacks+inverse candidate;
rank-only/split loops+zero-residual branch; final fast admitted starts+safe
endpoints+matched captures. Each uses Clang 21.1.8, explicit ISA/tuning, pinned CPU,
two processes in reversed case order and 11 rotated trials per cell. Every useful
consumer first checks its checksum against an independent oracle. Summaries keep
all 951 cells/profile, min/median/max and sample counts, rather than only winners.
These are warm small working sets, not cold-DRAM or PMU-qualified stall results.
The raw geometry `data_bytes` counter includes native field padding plus assigned
checkpoint entries. At N63 it does **not** include inactive checkpoint entries
made readable by `CheckpointCapacity`; do not read it as the complete read grant.
That harness allocates the full checkpoint array, included in `capacity_bytes`.
For u8/u16-checkpoint N63, required padded fields+granted checkpoint backing are
96 bytes at G16 and72 at G64, versus raw `data_bytes`72 and66. The N256 comparison
above has no inactive checkpoint tail and needs no correction.
The local transient probes guided iteration; not every transient executable has
an immutable complete source/binary pair. Native archives provide that stronger
identity for the decisions above.

Final native validation passes 1920 fixture states,4536 checked edits, protected
native tails/strided siblings/filters,40 actual Directory shapes, and maintained
StreamPack histories/boundaries in all three ISA profiles. Local ASan+UBSan passes
the latest 1920-state checker and seam checker. All measured study inputs still
match final capture c4377b00a50ca04ff41b02c1702aff5af35e5ce53f000d772510569f7bce404b;
subsequent edits add documentation/diagnostic audit tools only.

## Evidence and recovery

- First native candidate: [Zen5](evidence/native-first-zen5-20261022/avx2/summary.csv),
  [AVX512](evidence/native-first-zen5-20261022/avx512/summary.csv),
  [V2](evidence/native-first-v2-20261022/neon/summary.csv).
- Rank-only/first safe-prefix iteration:
  [Zen5](evidence/native-rank-only-zen5-20261022/avx2/summary.csv),
  [AVX512](evidence/native-rank-only-zen5-20261022/avx512/summary.csv),
  [V2](evidence/native-rank-only-v2-20261022/neon/summary.csv).
- Final native candidate: [Zen5](evidence/native-final-zen5-20261022/avx2/summary.csv),
  [AVX512](evidence/native-final-zen5-20261022/avx512/summary.csv),
  [V2](evidence/native-final-v2-20261022/neon/summary.csv).

Adjacent artifact/provenance files recover full raw repetitions, byte metadata,
measured binaries, linked dumps, build/check logs and frozen source archives.
The first V2 Spot request 20261022T153449Z-53f0bb06 and second Zen5 Spot request
20261022T154926Z-3fd835ca found no capacity and launched no instance; they supply
no timings. Successful fallbacks were bounded on-demand jobs. All workers were
requested with idle 0 for cleanup after verified collection.

The [diagnostic recovery](evidence/raise-bar-diagnostics-20261022/artifact.json)
keeps complete local compiler output, literal scheduling inputs/results, transient
negative probes and native audit reports. Selected audits and authored validation
observations sit beside that reference. Native raw/binary/source identity comes
from each native cohort's own archive. Final worker status confirmed both final
instances terminated after collection.
