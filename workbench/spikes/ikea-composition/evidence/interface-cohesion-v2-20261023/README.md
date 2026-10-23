# Cohesion evidence and remaining qualification

These receipts qualify particular source cuts. The working interface now restores
TuplePack's two-pointer general reader and supplies a separately prepared
predicate. The earlier 24-byte reader was a diagnostic candidate. Its timings
must not be presented as the final endpoint's qualification.

## Correctness and actual callers

- [Final functional checks](functional-final.json): 95 selected core/owner tests and
  93 independent public headers passed on each of NEON, AVX2 and AVX-512.
  All 32 selected ARM and 26 AVX2 caller targets build, and all five StreamPack
  owner checks pass in both native jobs. Three additional Bec composition checks
  pass, including guarded writes and independent algebra. The
  [earlier R4 receipt](functional-r4.json) retains the obsolete Loom fixture
  expectation and its failed job status; the final rerun closes that issue.
- [Complete-writer API correction](tp-api-final.json): a later audit removed
  accidental inherited preserving commands from the complete-initialization
  writer. Positive mapped-writer and negative complete-writer contract assertions,
  standalone headers and optimized compilation pass on all three profiles.
  Native NEON operation checks pass. All 15 affected object records match frozen
  R4 exactly; the other 21 after objects have no changed dependency. The
  [three-file patch](tp-api-final.patch) keeps this correction and a StreamPack
  comment clarification separate from R4's exact source identity.
- [Final configured codegen](codegen-final.json): 69 freshly compiled objects
  from the frozen candidate, including literal unwind, initializer-array and
  zero-initialized storage records and distinct ABI aliases. All 490 compiled
  SeriesPack readers per profile, the TuplePack word reader and initializer
  remain exact. Ordinary/effects/predicate consumer changes are retained rather
  than normalized into equality.
- [Directory complete-object comparison](directory-codegen.json): all 702
  function-symbol, 29 constant and 183 unwind records match across three
  profiles. Every non-debug allocated section also matches. Parent Engine,
  Loom and Orbital controls are identical; this does not establish linked
  placement or a hardware speedup.
- [Settled Frame port](frame/README.md): a six-file boundary-only migration
  passed all 492 commands across three profiles and two arms. All 19,200 owner
  records verify and their paired non-time fields match. Actual native kernels,
  plane stores and the complete phase driver match; material changes only
  error-enum immediates. The separate R3 dependency proof preserves scalar-tail
  register changes explicitly. The later
  [live owner port](../../../ikea-frame/evidence/api-migration-20261023/qualification.json)
  passed 462 commands and 9,600 complete owner records at its own source epoch.
  Its qualification does not infer throughput or cover subsequent Frame algorithms.
- [Initial broader codegen receipt](codegen-summary.json): 69 objects document
  the earlier source cut, including 490 exact compiled SeriesPack readers per
  profile. It is historical evidence for that cut, with its normalizations and
  real initialization/filter changes retained.
  Its parser covered allocated `SHT_PROGBITS`; x86 unwind, initializer arrays
  and zero-initialized sections require the stronger final comparison. Empty
  historical section counts must not be interpreted as proof of those sections.

## Performance findings

[R4 native comparisons](native-r4.json) qualify the restored reader, public
prepared predicate and coverage guard. They retain every canonical-metric case
with at least a 5% paired-median loss, selected controls and all five paired ratios.
Each profile ran 96 SeriesPack, 23 TuplePack, six initialization cases and 360
filter records. Counts are separate workloads, not a combined performance score.
The threshold selects compact evidence; smaller losses are not treated as zero.

The [physical-write traversal change](mutation-lowering.json) reduces actual
caller spills and text. In R3, the largest AVX2 substituted partial-write loss
falls from roughly 76% in R2 to roughly 5%. The historical
[R3 cohort](native-r3.json) retains that intermediate candidate and its losses.
The [guard experiment](coverage-guard/README.md) identifies and removes 512
same-target helper calls per 8,192-row command in the new candidate, preserving
other-source effects. R4 closes the earlier AVX-512 k31 substituted all-row loss
to about 1.7%; AVX2 substituted all-row cases still lose about 5–7%. Two AVX-512
ordinary point cases have sharply bimodal trials, which remain explicit.

No canonical R4 NEON case has a paired-median loss of 5% or more. This does not
establish zero loss or preserve every earlier candidate win. The
[handle study](handle-boundaries.json) proves the actual smaller-reader ABI and
public predicate bridge separately from native speed. Eleven x86 filter cases
still cross the 5% retention threshold. The
[TuplePack point audit](tuple-point-codegen.json) retains the roughly 15%
AVX-512 two-field body-write loss: its timed loop and selected kernel have equal
bytes, while the 61-byte kernel moves from a 64-byte boundary to offset 48.
Instruction delivery is a hypothesis, not a measured cause or a closed regression.

[StreamPack's diagnostics](stream/README.md) preserve the original rotation
cohort, the independently checked permutation cohort and the common-alignment
control. Its repeatable AVX-512 ordinary-reader loss has equal retired work.
Common alignment produces parity by slowing the baseline, leaving the after
cost unchanged; it is rejected as a production fix. The typed checked route is
already faster in the measured reused-binding cases, with one-shot/mixed-block
costs still a separate question.

[Bec's fixed-iteration diagnostic](bec-diagnostic.json) retains the original
negative timings and their reversal/control results. The AVX2 scalar encoder has
identical linked bytes in both arms; each arm's preparation and scalar encoder
also retains exact bytes and addresses across the original and diagnostic
cohorts. Random-encode rankings reverse under the fixed-iteration/common-argv0
control in both ASLR modes. Process-wide PMU counts and unavailable/incomplete
events retain their limits. This weakens an API-work explanation without
establishing a general speed gain or dismissing the original loss.

## Reusable controls follow-up

The [cross-module survey](../../reusable-execution.md),
[Frame mechanism census](frame/mechanisms.md) and
[dense runtime-control proposal](frame/runtime-control.md) connect these findings
to small bindings, shared operation columns and cheap block updates. They keep
shape reuse separate from content proof and retained owner generations.

The [cached-stride predicate experiment](../../handle-boundaries/PREDICATE-NATIVE.md)
was rejected after full native commands on all three profiles. The
[protocol and provenance](predicate-native.json), [all 1,404 canonical case metrics](predicate-native-cases.csv)
and [actual linked ARM block audit](predicate-blocks-linked-arm.json) retain the
preparation/one-shot losses, duplicate control and emitted copies. This is a
one-header comparison against R4, separate from the old-to-new cohesion cohort.
The candidate was never applied to live Ikea.

A subsequent access-contract audit found that R4's promoted NEON sparse packet
read can fetch inactive interior units, despite public active-only promises.
The [execution survey](../../reusable-execution.md) connects the original measured
window permission to an explicit native read grant. The separate
[access-contract qualification](access-contract/README.md) includes a passing
fully instrumented poisoned-interior test and a failing restored-old-behavior
control, plus preserved granted codegen and explicit benchmark cases. R4's
value/tail tests do not qualify that stronger physical-access promise; these
historical receipts are not relabelled as proof of the correction.

The [copied-projection candidate](predicate-projection-final.json) subsequently
removes the predicate's plan borrow and irrelevant packet-capacity parameter.
Its named view remains borrowed. On the three inspected targets the predicate
shrinks from 24 to 16 bytes, while its checked result still uses return memory.
The receipt retains 18 optimized producer/caller/maintained-fixture objects and
the independent native NEON boundary proof, including actual protected expired
plans, all 11 valid packet shapes, exact tails and unsigned query boundaries.
Both final arms share the corrected strict access contract. Selected full-tight
AVX-512 complete-caller work grows from 303 to 322 instructions. That includes
eight rematerializations of the fixture's hash multiplier and changed mask
extraction, while activity loads are hoisted across commands. It is not 19 extra
predicate comparisons or a measured timing cause. Smaller state does not
establish a speedup. The [earlier diagnostic](predicate-projection.json) used historical
R4 access behavior and remains a separate source cut. The
[native protocol](../../handle-boundaries/README.md#copied-projection-qualification)
adds all core/header checks before timing; no native result or live adoption is
claimed by these static receipts.

The [unique local object archive](local-objects-artifact.json) preserves 195
objects and their source/dependency/record context. Its
[recovery account](local-objects-context.json) identifies the already durable
R3/R4 source snapshots instead of duplicating them. Both the full compressed
object and every member hash were verified after publication. This is recovery,
not an additional correctness or performance qualification; native worker
artifacts retain their own executables and source captures.

The final source's functional and object qualification includes the restored
reader/prepared predicate, coverage guard and corrected Loom fixture. Native
losses and their diagnostic limits remain explicit; these receipts do not
establish a blanket regression-free claim. Later live Frame owner qualification
uses its own source epoch. A final API audit also removed accidental inherited
preserving commands from the complete-initialization writer; its separate delta
qualification keeps that correction distinct from frozen R4.
