# Delivery, derived outputs and retained material

Delivery tracks logical work. A packet arriving, a recipient retaining its
input, and that recipient completing the work are different events. The models
check those distinctions using actual commands, messages, replayed recipient
records and, in the retention composition, actual recoverable material.

The selected families are [delivery-cases.json](delivery-cases.json),
[derived-delivery-cases.json](derived-delivery-cases.json),
[delivery-retention-cases.json](delivery-retention-cases.json) and
[delivery-pressure-cases.json](delivery-pressure-cases.json). Exact captured
sources and expected result classifications belong to their runner receipts.
A `No...` invariant in a witness config is deliberately expected to fail.

| Requirement | Independent properties | Selected cases / exact configs |
|---|---|---|
| D1: stable logical identity | `Canonical`, `EffectMultiplicity` | [Delivery-leaf-duplicates](configs/Delivery-leaf-duplicates.cfg), [Delivery-route](configs/Delivery-route.cfg), [Delivery-bad-identity](configs/Delivery-bad-identity.cfg), [Delivery-bad-duplicate](configs/Delivery-bad-duplicate.cfg), [Delivery-reach-route](configs/Delivery-reach-route.cfg) |
| D2: transport/custody versus completion | `ProcessedBeforeRelease`, `Coverage`, `RetainedWhileNeeded` | [Delivery-ordinary](configs/Delivery-ordinary.cfg), [Delivery-bad-transport](configs/Delivery-bad-transport.cfg), [DeliveryRetention-transport](configs/DeliveryRetention-transport.cfg) |
| D3: independent committed derivation | `IndependentOrigins`, `OnlyCommitted`, `FailureJustified`, `EffectMultiplicity` | [DerivedDelivery-basic](configs/DerivedDelivery-basic.cfg), [DerivedDelivery-mismatch](configs/DerivedDelivery-mismatch.cfg), [DerivedDelivery-tentative](configs/DerivedDelivery-tentative.cfg), [DerivedDelivery-dedup](configs/DerivedDelivery-dedup.cfg), [DerivedDelivery-reach-abort](configs/DerivedDelivery-reach-abort.cfg) |
| D4: discovered children and recipient coverage | `Coverage`, `Completes` | [Delivery-two-part](configs/Delivery-two-part.cfg), [Delivery-child](configs/Delivery-child.cfg), [Delivery-child-cancel](configs/Delivery-child-cancel.cfg), [Delivery-bad-child](configs/Delivery-bad-child.cfg) |
| D5: cancellation preserves other users | `CancellationAuthority`, `RetainedWhileNeeded`, `PhysicalClosure` | [Delivery-shared-cancel](configs/Delivery-shared-cancel.cfg), [Delivery-bad-cancel](configs/Delivery-bad-cancel.cfg), [DeliveryRetention-cancel-release](configs/DeliveryRetention-cancel-release.cfg), [DeliveryRetention-reach-cancel](configs/DeliveryRetention-reach-cancel.cfg) |
| D6: atomic source/output and recipient replay | `SourceCheckpointOutbox`, `RecoveredSource`, `Checkpoints`, `EffectMultiplicity` | [DerivedDelivery-source-reset](configs/DerivedDelivery-source-reset.cfg), [DerivedDelivery-source-reset-identity](configs/DerivedDelivery-source-reset-identity.cfg), [DerivedDelivery-reach-source-reset](configs/DerivedDelivery-reach-source-reset.cfg), [Delivery-custody-restart](configs/Delivery-custody-restart.cfg), [Delivery-bad-checkpoint](configs/Delivery-bad-checkpoint.cfg), [Delivery-bad-output](configs/Delivery-bad-output.cfg), [Delivery-reach-restart](configs/Delivery-reach-restart.cfg) |
| D7: separate late-subscription debt and representations | `JoinedCoverage`, `Canonical`, `RetainedWhileNeeded` | [Delivery-late-join](configs/Delivery-late-join.cfg), [Delivery-transform-branch](configs/Delivery-transform-branch.cfg), [Delivery-bad-transform](configs/Delivery-bad-transform.cfg), [Delivery-reach-join](configs/Delivery-reach-join.cfg), [DeliveryRetention-late](configs/DeliveryRetention-late.cfg), [DeliveryRetention-reach-late](configs/DeliveryRetention-reach-late.cfg) |
| C3: execution→publication→outbox→replay | `IndependentOrigins`, `OnlyCommitted`, `FailureJustified`, `NoC3Seam` witness | [DerivedDelivery-basic](configs/DerivedDelivery-basic.cfg), [DerivedDelivery-join](configs/DerivedDelivery-join.cfg), [DerivedDelivery-reach](configs/DerivedDelivery-reach.cfg), [DerivedDelivery-reach-join](configs/DerivedDelivery-reach-join.cfg) |
| D↔R: logical closure drives actual physical release | `RetainedWhileNeeded`, `PhysicalClosure`, `NoCollected` witness | [DeliveryRetention-basic](configs/DeliveryRetention-basic.cfg), [DeliveryRetention-transport](configs/DeliveryRetention-transport.cfg), [DeliveryRetention-cancel-release](configs/DeliveryRetention-cancel-release.cfg), [DeliveryRetention-reach-gc](configs/DeliveryRetention-reach-gc.cfg) |
| D↔P: counted delivery work and backend debt | `PhysicalLifetime`, `RecordedCommands`, `DistinctLogEntries`, `SingleEmission`, `ResourcesBounded`, `Completes` | [DeliveryPressure-plain](configs/DeliveryPressure-plain.cfg), [DeliveryPressure-child](configs/DeliveryPressure-child.cfg), [DeliveryPressure-join](configs/DeliveryPressure-join.cfg), [DeliveryPressure-reset](configs/DeliveryPressure-reset.cfg), [DeliveryPressure-short-recipient](configs/DeliveryPressure-short-recipient.cfg), [DeliveryPressure-no-worker](configs/DeliveryPressure-no-worker.cfg), [DeliveryPressure-cancel-debt](configs/DeliveryPressure-cancel-debt.cfg), [DeliveryPressure-NoPhysicalAfterLogical](configs/DeliveryPressure-NoPhysicalAfterLogical.cfg) |

## Delivery compositions

`DeliveryDataflow` is the small delivery leaf. Each fixture origin evaluates its
own input and proposes an outbox command. Recipients decode actual payloads,
journal custody, journal an application effect with its checkpoint, and emit
processed evidence. The owner journals acknowledgements and may close only when
coverage and discovered children are discharged. A recipient reset clears its
volatile inbox and rebuilds custody, effects and checkpoints from its actual
journal prefix.

The ordinary chosen-journal provider is `DurableLog`; the family does not assert
that an arbitrary packet is already chosen. The smallest full-service leaf
explores the real provider's message/delivery choices. A second unrestricted
leaf folds chosen commands directly to explore duplicate origins and duplicate
contributions economically. The larger combinations use authored service cuts:
route replacement follows a real transport receipt and precedes final ACK;
recipient reset follows real durable custody and precedes its processing;
the transformed branch reaches custody before its raw duplicate. Ordinary
protocol service is drained between these cuts. These are explicit workloads,
not reductions claimed equivalent to every possible schedule.

`DerivedDelivery` is C3. Two separately stepped `TxProgram` instances start from
actual `TxKernel` inputs and its journal-derived context, including source/profile
and verification policy. Both complete their real query/extension/write/output
instruction sequence. Their full reports and the selected main outcome pass
through the transaction's actual journal and decision gate. Only a locally
received committed publication message enables that origin to submit its own
computed outbox to delivery. The harness never supplies an accepted result or a
whole epoch. `TransactionOracle` independently checks observed values, results
and failure provenance.

In the application fixture, key1 is the source progress cell: its increment and
the outbox belong to one actual durable transaction outcome.
`SourceCheckpointOutbox` independently checks the shared checkpoint value,
output identity and captured cut. The source-reset case replaces the driver
after outcome persistence and before decision. It clears the actual driver
state and local executions, recovers from the journal, re-executes from recovered
inputs/context, and continues delivery under the same identity.
`RecoveredSource` requires the actual recovery snapshot to contain that outcome;
a mutant adds the replacement incarnation to the identity and is rejected.

The joined case then changes a route before final ACK, delivers raw and
transformed representations, resets a recipient after custody, and recovers it
from retained records. `NoC3Seam` requires those concrete events to occur for the
completed contribution. `origin-mismatch` changes an actual instruction result
in one execution; agreement must abort before any origin output reaches delivery.
The tentative-output control removes the commit gate and must violate
`OnlyCommitted`. This links the executable application semantics used by the
epoch model to delivery; it is not an unrestricted product of every epoch,
transaction and network transition.

`DeliveryRetention` connects delivery to the existing `RecoveryKernel`. Its
origin obtains an actual root grant and reconstructed material before deriving
the contribution. Closing delivery emits a `delivery.release` notice. The
recipient of that notice requests `root.close`; Recovery's chosen metadata,
holder terminal records, registered backend operations and physical deletion
then perform the release. No wrapper reads an observer's inventory to manufacture
a completion or custody receipt.

For a late subscriber, the original finite coverage remains unchanged. The
release notice starts a successor root at the same context and cut; the original
root names that successor, and cannot release until it receives actual durable
custody evidence. Enrollment waits for the successor's grant and material. The
late subscriber receives separate completion debt and emits a separate release
notice when that debt finishes. Thus the initial source may be released while
the late subscriber still has a live root. The final witness requires physical
copies to be deleted after all required work finishes. This tests the finite
cut-transfer case, not unrestricted continuous tail enrollment.

`DeliveryUnderPressure` adds the actual delivery-to-capacity connection. It
reserves a control worker while ordinary memory and workers remain occupied.
Every owner/recipient command consumes a named retained-record lease, including
recovery barriers. Every emitted protocol message is a real Views registry send.
Backend completion permits receiver consumption; the buffer remains charged
until both receiver consumption and backend retirement. Recipient cancellation
cannot retire that buffer or another subscriber's use.

The selected finite workloads include a duplicate, a discovered child, a late
subscriber, cancellation after custody, and recipient replay. Removing the
reserved worker or one recipient's completion-record space causes a real
completion stall while unrelated fair work continues. Premature cancellation
release violates `PhysicalLifetime`. Witnesses reach logical completion while a
physical send is still outstanding. The wrapper asserts that the selected kernel
transitions emit at most one message and that each owner's log contains distinct
command IDs: neither limitation is silently assumed by its adapter or charging.

This is an authored service family: enabled local service precedes deterministic
backend service, with the cancellation crossing holding one actual send until
its logical cancellation arrives. An unrestricted physical-schedule pilot did
not finish its budget; the selected finite family is not its claimed quotient.
Units are conservative symbolic message/record/worker slots, not byte sizing or
CPU costs. `DurableLog` remains the chosen-journal interface; concrete witness
resource consumption is C7, not an extra consensus implementation in delivery.

## Shared mechanisms and distinct evidence

Delivery owns contribution identity, required recipients, child obligations and
application completion. Recovery owns durable roots, reconstruction closure,
transfer, tombstones and collection. The shared backend registry supplies
registration, lifetime and drain; its operators currently live in
[ViewsKernel](ViewsKernel.tla). The journal provider owns chosen ordering. The retention wrapper
reuses those mechanisms; it contains no second hold registry or independent
copy-deletion protocol.

The evidence types deliberately remain distinct. Durable *delivery custody*
means a recipient has retained its input; it does not mean the application has
processed it. Durable *root custody* permits a predecessor retention owner to
release after its successor accepts the same cut and interpretation. A shared
message envelope does not make those promises interchangeable. Logical root
release also does not retire physical borrowers, as checked by C5/C7.

## Findings and limits

The exercises sharpen several contracts without selecting a new contention or
network architecture. A completed original subscription does not retroactively
include a later subscriber: the latter needs its own captured-cut obligation.
Checkpoint/effect coupling must survive replay. Closing logical delivery must
invoke retention's actual release protocol, rather than merely delete an outbox
entry. These are concrete interface choices to carry into implementation.

The child negative initially went undetected because the authored scheduler
always enrolled the child before trying parent closure. That was an experimental
coverage defect, not evidence that premature closure is safe. The revised cut
allows parent acknowledgements to arrive before child enrollment; correct
closure waits, while the mutant can close early. The first C3 implementation
also built sets of entire product successor states before choosing a local
transition. Selecting the real local transition first preserves the chosen
service policy while avoiding expensive, irrelevant TLC comparisons. That is
a modeling representation fix, not a distributed algorithm improvement. The
source-replacement crossing then exposed a real adapter error: every recovery
request was being relabeled as a delivery recipient, including transaction-fold
requests. The provider correctly rejected the unknown actor and the case
deadlocked. Routing now preserves transaction recovery actors and only maps
delivery recipients. This is a composition fix, not new consensus machinery.

A recipient rejects conflicting duplicate content already known under one ID.
This is not Byzantine consensus and cannot undo a bad first output already
published. C3's required checks prevent the modeled unapproved divergent origin
from entering delivery; approved/WASM determinism remains the stated execution
contract. The toy reversible encoding tests semantic identity across physical
representations, not a production codec or hardening implementation.

Bounds are one or two base contributions, two origins, two initial recipients,
at most one late recipient, two routes, one reset and one dynamically discovered
child. There is no proof of arbitrary recursive dataflow termination, unlimited
subscriber enrollment, packet routing performance, or an infinite workload's
storage reclamation. The original leaf and C3 retain finite journal histories.
`DeliveryRetention` adds actual collection for its captured source closure.
`DeliveryUnderPressure` supplies actual message/command capacity mapping; it
does not infer physical capacity from the abstract event set in the other
delivery families. Progress assumes
weakly fair enabled service and finite modeled application work.

The transaction reviewer independently checked C3's actual execution, report,
commit, outbox, transformed-custody and replay paths, finding no blocking causal
or oracle hole within these bounds. The review specifically required the
mismatch-abort and tentative-origin witnesses, and cautioned against claiming
continuous enrollment or physical GC from C3 alone. Final-source classifications
and the retention review remain part of the selected campaign evidence.

The retention reviewer checked the actual notice-to-close path and same-cut
successor custody. This family has one source/successor generation and no
simultaneous view rebinding or backend borrower (those are C5/V crossings).
The small decoded value remains in delivery control state. Its lifecycle
invariant requires a live recoverable root while delivery still needs it, but
this is not an implementation proof that every later large-payload send rereads
its storage pages correctly.

The journal reviewer also checked the delivery capacity mapping: actual command
creation is charged, sends remain charged through both consumption and retirement,
and ordinary service requires the reserved worker. The resulting explicit
`SingleEmission` and `DistinctLogEntries` properties check the finite adapter
boundaries rather than silently relying on them.
