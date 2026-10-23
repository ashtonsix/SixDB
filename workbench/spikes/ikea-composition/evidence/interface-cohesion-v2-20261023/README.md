# Cohesion evidence and remaining qualification

These receipts qualify particular source cuts. The working interface uses
TuplePack's two-pointer general reader and a plan-independent copied predicate.
Earlier 24-byte reader/predicate representations remain separately qualified
history; their timings do not become the current endpoint's qualification.

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
Both final arms share the corrected strict access contract. In the unlinked
object, selected full-tight AVX-512 complete-caller work grows from 303 to 322 instructions. That includes
eight rematerializations of the fixture's hash multiplier and changed mask
extraction, while activity loads are hoisted across commands. It is not 19 extra
predicate comparisons or a measured timing cause. Smaller state does not
establish a speedup. The [earlier diagnostic](predicate-projection.json) used historical
R4 access behavior and remains a separate source cut. The
[native protocol](../../handle-boundaries/README.md#copied-projection-qualification)
adds all core/header checks before timing; no native result or live adoption is
claimed by these static receipts.

The completed [native comparison](../../handle-boundaries/PROJECTION-NATIVE.md)
supports the adopted plan-independent predicate. The
[source/qualification receipt](projection-native.json) and
[1,404 canonical records](projection-native-cases.csv) retain all five samples,
the duplicate control and every case. All 324 complete block cases improve;
nineteen x86 filter cases still lose over 5%, including seven predicate routes
and twelve shared-decode controls. Those losses remain explicit. The
[actual linked NEON audit](projection-blocks-linked-arm.json) verifies the
smaller fallback and removed overlapping aggregate copies. The
[actual linked x86 audit](projection-linked-filter.json) verifies less selected
work in the seven affected predicate paths and equal instructions/addressed
data in shared controls; it leaves the measured losses unresolved. Its actual
AVX-512 independent-eight path is 303 to 298 instructions, distinct from the
unlinked object's 303 to 322. The
[fresh sanitizer proof](predicate-projection-access.json) checks the candidate's
poisoned inactive interior directly, separately from the earlier 24-byte proof.

The [adoption bridge](predicate-projection-adoption.json) verifies all 193 live
Ikea header/source files against that native-qualified after tree. The regular
owning lifetime fixture matches the protected experimental fixture's body.
Nineteen current integration/core checks pass after dependency rebuilding, and
the new fixture also compiles for AVX2 and AVX-512; these two cross objects are
not presented as executed tests. The [four-file delta](predicate-projection-adoption.patch)
retains the exact header, CMake, fixture and neutral comment changes.

The [merged-allocator caller checks](merged-allocator-callers.json) refresh 16
existing owner checks at source 80b93a5f: Directory, Loom packing, metadata
growth/publication and Hashtable/Loom integration. All pass after fresh
dependency compilation. They establish local NEON behavior, not allocator speed
or a new qualification of Frame's subsequent algorithms. The frozen native
predicate comparison does not use Orbital allocation.

The [current Frame interface checks](frame-current-interface.json) separately
qualify its settled 4d0c7b32 source: execution material and four phase-owner
checks all pass after rebuilding their dependency closure. Consumed source bytes
remain stable through the run. These callers exercise SeriesPack and Intervals,
not TuplePack's predicate header. This closes current boundary integration at
that source cut; it does not qualify later Frame edits or measure throughput.

The [unique local object archive](local-objects-artifact.json) preserves 195
objects and their source/dependency/record context. Its
[recovery account](local-objects-context.json) identifies the already durable
R3/R4 source snapshots instead of duplicating them. Both the full compressed
object and every member hash were verified after publication. This is recovery,
not an additional correctness or performance qualification; native worker
artifacts retain their own executables and source captures.

The later [handle follow-up archive](handle-followup-artifact.json) retains 46
additional local objects and their raw command/dependency/assembly context,
including the compact predicate, direct sanitizer proof and rejected journal
cursor control. Its [recovery mapping](handle-followup-context.json) verifies
omitted source files against the newer native source archive. Complete remote
compressed bytes and every member were checked; this is recovery rather than
another correctness or performance result.

The R4 source's functional and object qualification includes the restored
reader/prepared predicate, coverage guard and corrected Loom fixture. Native
losses and their diagnostic limits remain explicit; these receipts do not
establish a blanket regression-free claim. Later live Frame owner qualification
uses its own source epoch. A final API audit also removed accidental inherited
preserving commands from the complete-initialization writer; its separate delta
qualification keeps that correction distinct from frozen R4.

## Final bounded comparisons and handover

Direct validated-layout predicate binding is implemented in `cd55974d`. Its
[functional receipt](predicate-direct-binding.json) covers full-size indices,
error precedence and real protected expired layouts. The separate
[codegen bridge](predicate-direct-codegen.json) retains 115 function, 30 constant
and 24 unwind records from nine paired representative objects exactly; its
[artifact](predicate-direct-codegen-artifact.json) preserves the raw context.

The [complete preparation comparison](../../handle-boundaries/direct-bind/README.md)
retains [all 81 cases](predicate-direct-native-cases.csv) and
[actual linked caller findings](predicate-direct-native.json). Direct binding
beats rebuilding generic plans throughout this cohort. Against already shared
plans, twelve ARM repeated-query cases lose over 5%; no x86 case crosses that
loss threshold. Smaller preparation and frames do not establish uniform wins.

The [SeriesPack control study](../../handle-boundaries/series-controls/README.md)
retains [900 native case-routes](series-controls-native-cases.csv) and its
[linked analysis](series-controls-native.json). Fresh, retained, direct-column
and typed operations produce different results. This remains a spike-only
prototype, with no public SeriesPack representation change.

Their [common qualification](execution-controls-native.json) records identical
source captures, all nine native oracle/lifetime checks before timing, 6,120
iteration records, all five route orders, complete verified archives and
terminated instances. The failed first runner attempt is retained separately
and produced no timings. These focused checks do not repeat the earlier full
core/header matrix.

The [one-image x86 control](../../handle-boundaries/PREDICATE-PANEL.md) retains
[its own protocol, all cases and identity checks](predicate-panel.json). Selected
losses persist at roughly 6–8%, while some duplicate controls also move. This
comparison does not provide a causal attribution or close prior regressions.

The [handover account](../../HANDOVER.md) collects outstanding measured problems,
contract questions, documentation issues and coverage limits without prescribing
their solutions. The current investigations are closed; their unresolved
observations remain explicit.
