# Physical views and completion capacity

The runtime models test whether logical promises survive their physical
implementation: reads use the captured bytes, page reuse respects every user,
and admitted work retains a path to an outcome. They model byte sequences,
physical slots, backend operations and named resource leases. They do not model
Linux or an allocator implementation.

The selected cases are listed in [views-cases.json](views-cases.json),
[capacity-cases.json](capacity-cases.json),
[checked-views-cases.json](checked-views-cases.json) and
[completion-pressure-cases.json](completion-pressure-cases.json), including
intentionally broken policies and reachability checks. Additional crossings are selected in
[reopened-writer-cases.json](reopened-writer-cases.json),
[delivery-pressure-cases.json](delivery-pressure-cases.json) and
[versioned-execution-cases.json](versioned-execution-cases.json). A reachability check deliberately falsifies a
`No...` invariant; that outcome is evidence that the interesting crossing occurs.
The captured runner receipts, rather than this document, identify exact source
hashes, TLC version, configuration, explored graph and classification.

## What is checked

| Requirement | Independent properties | Selected cases / exact configs |
|---|---|---|
| V1: exact object/version/rights/lifetime | `ExactObserved`, `MaterializedIdentity` | [ViewsReuse](configs/ViewsReuse.cfg), [ViewsPrepared](configs/ViewsPrepared.cfg), [ViewsReachLateOld](configs/ViewsReachLateOld.cfg) |
| V2: no-COW and reset reconstruction | `PublishedBytes`, `BorrowedBytes`, `BindingsHeld`; joined `ReopenedBytes`, `DurableReplay`, `OutcomeJustified` | [ViewsReuseAbort](configs/ViewsReuseAbort.cfg), [ViewsSkipRestore](configs/ViewsSkipRestore.cfg), [ReopenedWriter-commit-early](configs/ReopenedWriter-commit-early.cfg), [ReopenedWriter-abort-early](configs/ReopenedWriter-abort-early.cfg), [ReopenedWriter-trust-cache](configs/ReopenedWriter-trust-cache.cfg), [ReopenedWriter-skip-replay](configs/ReopenedWriter-skip-replay.cfg), [ReopenedWriter-reach-early](configs/ReopenedWriter-reach-early.cfg) |
| V3: independent scopes on shared pages | `PublishedBytes` compares scoped effects with actual page bytes | [ViewsSharing](configs/ViewsSharing.cfg), [ViewsUndo](configs/ViewsUndo.cfg), [ViewsTwoPages](configs/ViewsTwoPages.cfg), [ViewsWholeInstall](configs/ViewsWholeInstall.cfg), [ViewsWholeUndo](configs/ViewsWholeUndo.cfg), [ViewsReachSharedPage](configs/ViewsReachSharedPage.cfg) |
| V4: CPU and asynchronous permissions | `ReadOnlyProjection`, `AuthorizedAsync` | [ViewsReadOnly](configs/ViewsReadOnly.cfg), [ViewsRights](configs/ViewsRights.cfg), [ViewsCPUWrites](configs/ViewsCPUWrites.cfg), [ViewsAdjacent](configs/ViewsAdjacent.cfg), [ViewsAsyncExtent](configs/ViewsAsyncExtent.cfg), [ViewsReachCPUProtect](configs/ViewsReachCPUProtect.cfg) |
| V5: backend lifetime and stale completion | `PhysicalCharges`, `CallbacksFenced`, `RegistryDrainSound`, `RegistryMirrorsBackend` | [ViewsLifetime](configs/ViewsLifetime.cfg), [ViewsHostReset](configs/ViewsHostReset.cfg), [ViewsCancelRetires](configs/ViewsCancelRetires.cfg), [ViewsOldCallback](configs/ViewsOldCallback.cfg), [ViewsEarlyDrain](configs/ViewsEarlyDrain.cfg), [ViewsReachDrain](configs/ViewsReachDrain.cfg) |
| V6/V7: identity and representation | `MaterializedIdentity`, `BindingsHeld`, `ExactObserved` | [ViewsRepresentation](configs/ViewsRepresentation.cfg), [ViewsLiveRebind](configs/ViewsLiveRebind.cfg), [ViewsWrongInterpretation](configs/ViewsWrongInterpretation.cfg), [ViewsReachLateOld](configs/ViewsReachLateOld.cfg) |
| P1/P2: resolver and completion path | `ReservedCompletion`, `PhysicalLeases`, `Complete` | [CapacityDependent](configs/CapacityDependent.cfg), [CapacitySharedDependent](configs/CapacitySharedDependent.cfg), [CapacityNoResolver](configs/CapacityNoResolver.cfg), [CapacityHeldLatch](configs/CapacityHeldLatch.cfg), [CapacityNoFaultMemory](configs/CapacityNoFaultMemory.cfg), [CapacityNoControlProgress](configs/CapacityNoControlProgress.cfg) |
| P3: physical last use and durable evidence | `PhysicalMemory`, `SharedObligation`, `OutcomeDurable`, `DecisionDurable` | [CapacityShared](configs/CapacityShared.cfg), [CapacityCancel](configs/CapacityCancel.cfg), [CapacityDriver](configs/CapacityDriver.cfg), [CapacityEarlyAck](configs/CapacityEarlyAck.cfg), [CapacityOneAck](configs/CapacityOneAck.cfg), [CapacityStealControl](configs/CapacityStealControl.cfg), [CapacityCancelDebt](configs/CapacityCancelDebt.cfg) |
| P4: finite admitted work | `TasksComplete`, `Complete`; actual task1→task2 data dependency | [CapacityDependent](configs/CapacityDependent.cfg), [CapacityReachDependentPressure](configs/CapacityReachDependentPressure.cfg), [CapacitySharedDependent](configs/CapacitySharedDependent.cfg) |
| P5: bounded health work and shared pressure | `BoundedInvestigation`; counted ordinary/resolver/control pools | [CapacityAlarms](configs/CapacityAlarms.cfg), [CapacitySlowThird](configs/CapacitySlowThird.cfg), [CapacityDuplicateProbe](configs/CapacityDuplicateProbe.cfg), [CompletionPressure-alarm](configs/CompletionPressure-alarm.cfg), [CompletionPressure-both](configs/CompletionPressure-both.cfg) |
| C5: captured reads, verification and publication | `OldBytes`, `RootEvidence`, `SerialReads`, `SerialOutcomes`, `PhysicalPublication`, `VerifiedOutcome`, `FailureJustified` | [CheckedViews-basic](configs/CheckedViews-basic.cfg), [CheckedViews-prepared](configs/CheckedViews-prepared.cfg), [CheckedViews-main-divergence](configs/CheckedViews-main-divergence.cfg), [CheckedViews-unverified-main](configs/CheckedViews-unverified-main.cfg), [CheckedViews-reach-old](configs/CheckedViews-reach-old.cfg), [CheckedViews-reach-debt](configs/CheckedViews-reach-debt.cfg) |
| C7: actual J/R completion under capacity pressure | `DeliveredEvidence`, `ActualRecords`, `PhysicalDebt`, `PhysicalWrites`, `ProtectedMaterial`, `OutcomeEvidence`, `DecisionEvidence`, `Completes` | [CompletionPressure-basic](configs/CompletionPressure-basic.cfg), [CompletionPressure-both](configs/CompletionPressure-both.cfg), [CompletionPressure-short-record-reservation](configs/CompletionPressure-short-record-reservation.cfg), [CompletionPressure-no-control-worker](configs/CompletionPressure-no-control-worker.cfg), [CompletionPressure-no-fault-worker](configs/CompletionPressure-no-fault-worker.cfg), [CompletionPressure-cancel-debt](configs/CompletionPressure-cancel-debt.cfg), [CompletionPressure-reach-done](configs/CompletionPressure-reach-done.cfg) |
| D↔P: delivery work uses counted service | `RecordedCommands`, `DistinctLogEntries`, `PhysicalLifetime`, `ResourcesBounded`, `Completes` | [DeliveryPressure-child](configs/DeliveryPressure-child.cfg), [DeliveryPressure-join](configs/DeliveryPressure-join.cfg), [DeliveryPressure-reset](configs/DeliveryPressure-reset.cfg), [DeliveryPressure-short-recipient](configs/DeliveryPressure-short-recipient.cfg), [DeliveryPressure-no-worker](configs/DeliveryPressure-no-worker.cfg), [DeliveryPressure-cancel-debt](configs/DeliveryPressure-cancel-debt.cfg), [DeliveryPressure-NoPhysicalAfterLogical](configs/DeliveryPressure-NoPhysicalAfterLogical.cfg) |
| X1/X5: versioned executable and delayed audit retention | `CodeVersion`, `JournaledProfile`, `SerialOutcomes`, `MissingProvenance`, `CodeRetention`, `AuditMeaning`, `OutcomeJustified` | [VersionedExecution-checked](configs/VersionedExecution-checked.cfg), [VersionedExecution-approved-incident](configs/VersionedExecution-approved-incident.cfg), [VersionedExecution-missing](configs/VersionedExecution-missing.cfg), [VersionedExecution-wrong-version](configs/VersionedExecution-wrong-version.cfg), [VersionedExecution-missing-fallback](configs/VersionedExecution-missing-fallback.cfg), [VersionedExecution-early-release](configs/VersionedExecution-early-release.cfg), [VersionedExecution-reach-retained](configs/VersionedExecution-reach-retained.cfg) |

`ViewsKernel` exports the same registry used by recovery: submission records
operation identity, generation, target and payload; completion precedes callback
retirement. Closing prevents new submissions. Drain requires actual debt to be
retired. A process reset cannot retire a submitted backend user by itself; a
host reset has a separately modeled cancellation contract. A logical cancellation
can therefore coexist with a required subscriber and a charged physical buffer.

The small capacity leaf uses a prepared three-witness persistence service. It
checks record reservations and real outcome/decision writes, but does not itself
prove election or authority. C7 replaces that service with the actual
`JournalKernel`, starting unprepared and using its real voter writes and replies.

## Joined cases

`CheckedViews` (C5) connects Tx, Recovery, Views and the abstract chosen-journal
provider. An actual early read bound captures the old cut. A later writer changes
both physical bytes under no-COW reuse; its actual sealed bytes form its Tx
outcome, and physical installation gates publication. Recovery transfers actual
base/code material to a second holder and grants three views for the captured
read: main execution and two independent checkers. Their observed bytes supply
Tx reads and full report outcomes. The reports must agree with the selected main
result, not merely each other. Releasing the logical root does not retire a
backend send still borrowing its bytes.

`OldBytes`, `RootEvidence`, `SerialReads`, `SerialOutcomes`, `PhysicalPublication`
and `VerifiedOutcome` check those connections independently. The mismatch and
main-divergence cases agree an abort. The `unverified-main` control proves that
two equal checker reports do not authorize a different main result. A peer review
also caught an initially manufactured second writer effect: both effects now
come from their respective actual sealed bytes, and both installed bytes are
checked against the durable outcome.

`CompletionUnderPressure` (C7) connects actual J, R, V and CapacityPool. An
extension and repair fill the two ordinary worker slots. A source fault needs
reserved service; actual recovered byte 17 yields result 18. The shared journal
contains root Begin/Register, outcome, decision and root Release. Actual durable
voter histories independently justify every delivered record. Five completion
record slots per witness are reserved in addition to an explicitly counted
retained record. Pending writes, undrained callbacks, root metadata operations,
source material and a delayed shared send all consume named capacity.

The short-record-reservation control stops useful progress while unrelated fair
activity continues. The no-control-worker and no-fault-worker controls similarly
fail temporal completion rather than being dismissed as impossible admission.
Safety controls detect receipt before quorum persistence and cancellation that
erases a remaining subscriber's debt. Completed ordinary work retires its leases;
durable journal history remains counted.

`ReopenedWriter` closes the reset/reopen crossing that the original V leaf did
not establish. A real no-COW writer seals both bytes; an actual backend cache
write persists those tentative bytes. Host reset loses the mutable pages.
Recovery reads the actual chosen outcome/decision prefix and independently
retained base material, reconstructs committed effects or the original abort
base, imports the reconstructed immutable copy, and opens a fresh physical view.
Publication waits for that view. Both commit and abort are checked with reset
before or after decision durability. Trusting the tentative cache on abort and
omitting committed replay are separate failing controls. The initial writer's
zero bytes are a fixture matching its retained base; C5 covers initial material
to execution. This new wrapper proves the actual post-seal recovery connection,
not arbitrary repeated generations.

`VersionedExecution` binds the profile from the actual begin record to a recipe
containing a versioned executable literal and its decoder. The main execution
and each checker consume separately granted physical views; the observed code
parameter is interpreted by `TxProgram`. A missing decoder causes real root
acquisition failure and pre-position cancellation. The remote root owner emits
a failure notice only after folding its real abort record; the execution binding
consumes that notice before cancelling. An earlier adapter read the remote phase
directly, which was a local-knowledge defect corrected by this message path. The fallback control instead
uses a cached executable without a matching grant and is rejected. Code is
immutable, so its acquisition cut is zero and its root context contains the
transaction's journaled profile and map version; execution still uses the actual
full transaction context and fixed position.

Checked executions retain their code through the required reports and decision.
Approved asynchronous executions retain the root across publication until the
actual audit-report record is delivered. A differing native re-execution result
creates an incident before release; it does not rewrite an already published
outcome. The native discrepancy is an explicit failure injection into the audit
execution, not a claim that an approved extension is deterministic. Wrong-version
and premature-release controls exercise the two separate guards. These cases
use one immutable executable dependency and a finite audit, not arbitrary code
loading or guaranteed termination of user code.

## Bounds and interpretation

The view leaf explores arbitrary enabled transitions for one or two pages,
two byte-sized logical items per page, at most two writers, selected views and
one asynchronous operation. Its supplied immutable material is a fixture.
C5 tests actual Recovery creation of the corresponding grants and copies.
Neither family claims arbitrary applications terminate or implements runtime
hardening, UFFD, MPK/POE, DMA isolation or a production cancellation API. Those
platform contracts need implementation evidence. Cache-residence hints and
scratch allocator policy are intentionally outside logical identity; cache
performance is not a safety property here.

The basic capacity cases explore arbitrary enabled finite work. The two-task
shared-dependent case authors a concrete pressure sequence: both workers park,
ordinary record capacity fills, the shared send blocks fault I/O, then actual
send retirement enables fault resolution. This is a distinct workload, not a
state-space quotient of all independent two-task histories. The independent
`CapacityTwo` and unrestricted `CapacityCombined` pilots did not finish their
budgets and are not selected evidence.

C5 and C7 use authored causal cuts with normal protocol service drained between
them. Physical borrowing, cancellation and selected fault orderings remain
choices at those cuts. They establish concrete joined-history coverage, not an
unrestricted product proof. Weak fairness applies to enabled useful service;
permanent storage loss, permanently unavailable networks and infinite application
execution are outside their completion claims. Storage survives the modeled
process resets unless the case explicitly invokes the host-reset contract.

C7's retained `J.net` entries are symbolic retry-record slots, not the occupancy
of production network buffers. Its resource constants cannot be read as byte
sizing recommendations. All models retain bounded histories; they do not prove
sustainable throughput or garbage collection of an unlimited workload.

P5's bootstrap/SOS capacity arrow is checked separately by
`BootstrapUnderPressure` and `SOSUnderPressure`, listed in
[journal-cases.json](journal-cases.json). Their `RecoveryServiceKernel` reuses
CapacityPool and the Views registry to charge captured query, copy, decode,
pause and export work before its result becomes available. Backend completion
makes the actual captured result visible; backend retirement releases its lease.
Ordinary worker/memory remain occupied, and an optional observer cancellation
cannot free the shared operation. This supplies the controller/receiving-side
mapping that ordinary C7 alone did not establish. C7 checks the provider-side
journal/root service resources. These small controllers serialize application
steps while one operation is staged, so their reset cases do not additionally
cover a controller crash during that staged operation. See
[JOURNAL.md](JOURNAL.md) and the exact selected receipts for those cases.

## Evidence and review

Selected local runs completed their finite graphs or produced the expected
negative/witness classification. Representative healthy graphs were 20,020
states for the combined view case, 27,552 for two pages, 21,088 for dependent
capacity, 9,034 for the authored shared-dependent pressure case, 14,543 for C5,
and 1,410 for C7 with cancellation and an alarm. These are model-checking state
counts, not system performance measurements. Exact results reside under
`build/orbital-tla/` and `build/orbital-spec/`; only receipts whose actual parsed
dependencies match the final source are selected. Changes to shared Tx behavior
require rerunning the affected joined cases, regardless of an earlier pass.

The transaction reviewer checked the physical-byte/result/publication seam and
the selected-main verification gate in C5. The journal reviewer checked C7's
actual authority preparation, voter storage, delivery evidence and resource
mapping, and identified the retained-retry-record interpretation above. Neither
review substitutes for the final-source TLC results.

The runtime reviewer also inspected the emergency-service capacity wrappers:
lease acquisition and actual backend registration are coupled, captured results
remain private until completion, and optional cancellation preserves debt until
retirement. The shared foundation therefore carries both ordinary and emergency
resource lifetimes. This is an interface/composition improvement; it does not
establish production buffer sizes or collapse the distinct meaning of a local
pause record, a journal decision, and a physical send.

The transaction reviewer accepted the post-seal reset/reopen composition and the
versioned execution/audit composition. The review distinguished the immutable
code cut from the transaction data cut, checked real missing-dependency failure
provenance, and confirmed that async release follows actual report delivery.
Physical code collection reuses Recovery; the versioned wrapper specifically
checks dependency selection, granted bytes and the extended audit lifetime.
