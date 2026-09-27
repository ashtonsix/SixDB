# Admission renewal reduction and its evidence

The last historical baseline case combines generation renewal with two storage
losses and delayed protocol messages. It is an exhaustive finite graph, not a
depth-limited search or a composition of the entire database. The original graph
spent most of its exploration distinguishing stale bookkeeping and permanently
inert messages. Earlier worker interruption and checkpoint replay added elapsed
time. No protocol violation had been reported when the reduced run began.

The reduced graph completed 925,794 distinct states and 9,580,369 generated
successors at depth 64, with all five safety properties and `Completes` passing.
The run took 1,741.687 seconds with four TLC workers and a 1 GiB heap on the shared
local Linux/ARM guest. The original reference had already exceeded 27.2 million
states with 5.64 million still queued. This is a state-space comparison, not a
controlled runtime benchmark or a database performance measurement.

The historical 485/486 selection and dated 599/600 design selection remain records
of their original inputs. The [current selection](README.md) closes their open
item without rewriting those archives.

## What the case establishes

There is one stream, one entry, two holders, two protection generations and a
budget of two cumulative storage-domain destructions, at most one per holder.
Body, interpretation and protection generation publish atomically for a new
package; renewal of an existing package is a separate metadata step. The journal
is folded, the producer survives, and messages can be delayed and reordered.

The five safety properties check refinement to the certificate service, actual
certificate evidence, contiguous admission, exact stored copies and protection.
`Completes` checks eventual final-generation certification and the promised
frontier under the original kernel/input action-family weak fairness. Completion
does not require both holders to have fresh final copies after every later fault.
The separate authored two-loss witness does require both repaired generation-2
copies at its final cut.

The protection allowance is one loss since a generation began. Generation 2 can
begin between the two cumulative losses. This is not a promise to survive any two
simultaneous losses of two copies. Ordinary `Lose` clears storage, inbox and
pending writes, but retains authorization and peer knowledge. `AdmissionCold`
separately checks reconstruction after those volatile facts disappear.

## The representation change

`AdmissionInertNetwork` uses the existing kernel and certificate observer. Its
configuration is byte-identical to the original two-loss configuration. The
physical copies, loss history, generation transitions, certificate identities,
meaningful messages and completion predicate remain represented.

`AdmissionRelevant` extends the existing proof-normalization and obsolete-flag
maps. It removes an atomic inbox package once the holder already has a complete
copy at the same or a newer generation. A later loss cannot reactivate that inbox
entry because loss clears the inbox in the same step. It retires received/issued
receipt proof only after the immutable key/generation certificate has been
validated and issued. The stale-renewal mutant retains that proof because its
deliberately defective transition still consults it.

The network map discards only permanently inert delivery: already-installed
authorization, already-learned storage incarnation, already-received certificate
or receipt, an already-applied journal command, and discovery that adds neither
knowledge nor evidence. A duplicate payload is special: it remains deliverable
while its destination can lose storage again. It becomes inert only when the
destination has a complete same/newer copy and has exhausted its permitted loss,
or the whole loss budget is exhausted. The unsafe earlier-drop control fails.

`AdmissionInertSends` also forgets suppression IDs whose newly enabled send would
produce only such an inert delivery. Authorization IDs include generation;
copy-resumption IDs include storage incarnation; receipt IDs include generation
and storage; forwarding IDs include generation and both endpoint incarnations.
The producer submission flag is retained: its identity omits protection
generation, so forgetting it could enable a new generation-2 submission. Its
adversarial control fails exact correspondence.

These maps choose a smaller model representation. Their use of the finite fault
budget does not prescribe a packet-dropping policy for a running database.

## Why safety and completion survive the map

`AdmissionInertCorrespondence` compares complete normalized non-stuttering
successors in both directions, separately for kernel and input action families.
It compares state, network, emitted-evidence validity, transition tag and actual
input identity/kind. Removed deliveries must leave normalized state unchanged,
emit nothing and remain inert after every enabled successor. Checking just
currently inert messages would miss the delayed-payload repair counterexample.

The liveness argument additionally needs finite hidden work. The concrete model
has bounded generations and storage identities, monotone suppression sets, and
finitely many envelopes that are each consumed once. It cannot run forever by
changing only erased bookkeeping. Conversely, an abstract resend enabled by a
forgotten inert-send ID is a whole-variable stutter after normalization. TLA weak
fairness uses non-stuttering actions, so those resends cannot discharge a kernel
fairness obligation while meaningful work is withheld. The same completion
predicate observes only retained facts. Raw TLC deadlock formatting need not be
identical: an inert self-loop can replace a concrete terminal. `Completes` stays
enabled so a non-completing stuttering trap remains a failure.

Independent source review accepted this argument and the exact correspondence
comparison for the maintained atomic, folded, finite-fault case. This is a source
argument with finite audits, not a machine-checked arbitrary-size refinement
proof. Cold knowledge loss, generation rollback, repeat loss of a domain, split
publication and different receipt-reuse rules require renewed reasoning.

## Audits and adversarial controls

| Check | Result |
| --- | --- |
| Fine graph, two generations and no loss | 2,197 states; complete correspondence, inertness and permanence checks |
| Fine graph, one generation and one loss | 2,515 states; complete correspondence, inertness and permanence checks |
| Authored two-loss crossing | 90 states checked; separate 78-state witness reaches `renewal-between-two-losses` |
| Reduced two generations, no loss | 285 states; five safety properties and completion |
| Reduced two generations, one loss | 40,433 states / 344,302 generated; five safety properties and completion |
| Reduced two generations, two losses | 925,794 states / 9,580,369 generated; five safety properties and completion |
| Forget still-needed send suppression | Expected `ExactRepresentation` failure, 172 states |
| Forget producer submission suppression | Expected `ExactRepresentation` failure, 91 states |
| Forget live inbox / current receipt evidence | Expected `ExactRepresentation` failures, 17 / 71 states |
| Drop currently duplicate payload before future loss | Expected `UnsafeStable` failure, 497 states |
| Stale-renewal / one-copy certification | Expected `CertificateEvidence` failures, 67 / 40 states |

The authored crossing uses real kernel messages: a delayed holder-2 payload
repairs holder 1 after its loss without producer resubmission; generation 2 begins
between the losses; a later duplicate to now-unfailable holder 1 remains inert;
holder 2 loses storage and is repaired by actual copy resumption. Its schedule is
restricted, but every visited state audits all enabled successors. It is distinct
from the unrestricted reduced two-loss check.

The fine correspondence wrapper uses distinct numeric owner/producer addresses
(-2/-1), retaining positive holder addresses. This avoids a pinned TLC evaluator
type-comparison error when heterogeneous event records are ordered by interned
field-name order. These role addresses are opaque: they occur only in envelopes,
command routing and an unused full-journal delivery guard. They do not occur in
package bytes, receipt evidence, holder arithmetic, suppression IDs or persistent
admission state. The renaming is consistent with the abstract commands and fixed
journal record. Full endpoint values are still compared; the actual reduced
wrapper keeps its original string role addresses. Independent review checked
this address bijection. Earlier type-error receipts are diagnostic failures.

The prior network-only reduction completed the one-loss graph with 88,306 states
and 456,643 generated successors. The additional inert-send map removes 54.2% of
those states and 24.6% of generated successors. The prior network-only two-loss
graph also completed, with 2,541,177 distinct states; the final inert-send map
removes 63.6% of those states. The prior run resumed 659,938 checkpoint states and
completed in a 3,587.587-second continuation after an earlier stopped attempt.
Its source, raw result and verified recovery lineage are retained with the
comparison. Generated counters across recovery and elapsed times across different
resources/shared-host loads are not a controlled solver benchmark.

## Reproduce and retain

The catalog keeps `Admission-renewal-capability` as the two-loss case and adds
named `Admission-inert-*` comparisons, audits and controls. Use the normal
[checker and evidence collector](../../README.md); receipt acceptance requires exact
parsed source/configuration bytes, the pinned TLC build and raw diagnostics.
The [selection](README.md) preserves the exact completed inputs and diagnostics.
Stopped prefixes, syntax/type errors and the earlier timed-out candidates are
diagnostic history; none are classified as a completed graph.
