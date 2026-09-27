# Journal authority and its application boundaries

These models check the candidate protocol bound in `PLAN.md`. They do not prove
an arbitrary implementation, arbitrary cluster size, or a universal composition
of all models. `journal-cases.json` is the runnable case catalog; source-captured
TLC receipts distinguish complete graphs, deliberate counterexamples, reachable
witnesses and incomplete sizing runs.

## What is represented

`JournalKernel` has separate per-witness durable promises and accepted prefixes,
queued writes, backend completion, process-incarnation callbacks, phase-one
reports, proposals, forwarded submissions, acceptance receipts and local learned
certificates. A higher ballot belongs to one named leader and configuration. Its
promise quorum selects the highest accepted ballot, then the longest prefix among
reports at that ballot. The leader can extend only that recovered prefix. A
terminal record closes the old configuration and names its successor. Successor
voters persist the certified inherited prefix before voting.

The network set represents persistent idempotent retransmission obligations, not
packet counts or send-buffer occupancy. Choosing, learning and application replay
are separate transitions. No protocol guard can read the independent chosen or
accepted history used by invariants. Reset clears local learned certificates and
quorum knowledge; pending backend work remains ordered and may complete after the
process dies. Media loss removes the old voter's durable state and excludes that
storage identity from future voting. Ordinary submissions and recovery queries
can enter at a surviving witness and follow locally learned authority hints.

A recovery query submits a real barrier command. Its reply contains an actually
learned prefix ending at that query's barrier. A consumer must fold the whole
prefix before serving; it cannot reopen from an arbitrary stale snapshot.
`DurableLog` exposes this same actor-specific interface. It permits two authored
submission occurrences to append identical command bytes at different positions.
`JournalKernel` may suppress the same command ID as an optimization. Consumers
must still deduplicate logical application effects, including distinct command
IDs that carry the same logical contribution.

## How the tractable models connect

1. `JournalPersistence` checks local write ordering and exact reply snapshots.
   Request registration and callback dispatch stutter in the abstract durable
   acceptor; physical completion is its only changing transition. Joint cases
   keep two voters' pending operations and process resets independent. A separate
   suffix case adds a same-ballot extension. Completed writes may have callbacks
   delayed across reset. The `overtake` and `early_reply` controls break these
   independent checks.
2. `JournalOrder` checks quorum agreement, terminal closure, named successor
   initialization, suffix retention and certificate authenticity. It retains
   exact phase-one reports, including stale reports and every quorum subset.
   Cumulative sent/acknowledged/certified prefixes replace collections of their
   shorter comparable prefixes. Any implied shorter prefix may be delivered;
   accepted proposals cannot fall below the prefix recovered by that ballot.
   This deliberately overapproximates possible message schedules for safety.
   It is not an exact equivalence of wire traces. Omitting the recovered-prefix
   floor produced an abstraction counterexample and was corrected before use.
3. `JournalAuthority` executes the concrete kernel and checks its observable
   `DurableLog.Contract`: monotonically chosen prefix, proposed-record validity,
   actor-local ordered delivery and barrier-bound recovery snapshots. Its
   `JournalOrderContract` additionally checks each concrete durable-state and
   phase-one-selection boundary against the quorum model's rules. That is a
   checked boundary correspondence, not a TLAPS proof of arbitrary-size weak
   simulation. The local and order abstractions require the stated correspondence
   argument; their safety checks do not by themselves transfer liveness.

Selected concrete cases retain all voters' request/persist/callback boundaries.
Other cases fold callback dispatch into completion for nonfault voters, or use the
checked atomic acceptor service for witnesses outside the designated fault cut.
Those quotients retain delayed network delivery. They must not be used to infer
backend occupancy; `CompletionUnderPressure` retains the actual pending and
callback leases. Opaque certificates erase the redundant signer subset only after
an actual local quorum has learned an exact configuration/ballot/prefix. A
small explicit-signer case checks the unreduced representation too.

The peer owning transactions reviewed local completion/callback correspondence,
certificate erasure, and the cumulative-prefix quotient and its recovery floor.
The capacity peer reviewed the shared event interface; this owner reviewed its
actual journal/capacity integration. Neither review substitutes for a TLC receipt.

## Requirement mapping

| Requirement | Actual evidence and boundary |
| --- | --- |
| J1: one prefix across leaders and recovery | `JournalPersistence` plus `JournalOrder` check joint pending-write crashes, exact phase-one snapshots, competing ballots and independently observed `Agreement`; `JournalAuthority` checks `PrefixAgreement`, `LearnedEvidence`, `ProviderRefinement` and `OrderRefinement`. |
| J2: healthy service and follower ingress | Concrete initial-leader and follower-ingress cases; `NoEarlyFollower` witnesses follower learning before the leader. `JournalIngress` sends two distinct envelopes with one immutable frontier command through different followers and checks actual provider deduplication plus a reachability witness; it does not strengthen the abstract service contract. `slow-third` checks `Completes` with the third voter's persistence disabled. Missing-quorum control keeps unrelated work running and fails target completion. |
| J3: durable evidence before propagation | Explicit request, persistence and callback transitions; `RepliesFromDurableSnapshots`, `PromiseDurable` and `LearnedEvidence` reject volatile or early replies. Delivery and payload survival beyond these journal bytes belong to Admission/Delivery models. |
| J4: exact handoff and closed old authority | `JournalOrder` tests overlapping/disjoint memberships, two candidate successors and repeated transfer; independent `Closure`, `NamedSuccessor`, `Agreement`. Concrete C1/C8 really recover and choose the terminal suffix, initialize successor witnesses, then serve the new owner. |
| J5: history plus required material before serving | `AuthorityBootstrapSOS` joins actual J authority with actual RecoveryKernel Hold/Register/Grant/material transitions. Bootstrapping acquires material before admission authority. Its consumer admits only with a locally learned active successor and the matching retained material. `BootstrapDiscovery` checks the inventory/readiness boundary; broader independent unavailable scopes belong to RecoveryMaterial. |
| J6: disposable driver and owner fold | C1 loses driver and owner fold after one participant fixed/released and an owner record was accepted without application acknowledgement. Variants cut at one durable accept, quorum acceptance, or a chosen decision before learning. Actual barrier replay preserves the position, earlier completed read bound, result and publication semantics. |
| J7: scope migration | C8 first completes a readonly transaction that raises the source read floor, then joins the actual TxKernel map-close/old-plan enrollment with actual J handoff and owner-cache loss. It checks both a late new plan's refusal and a pre-close old plan's continued drain. `RestoredMap`, `EnrollmentFence`, `MapTransfer` and the serial oracle are independent observations. The owner-local transfer command consumes actual bound/version/dedup payload; positive-floor and drop-floor cases check substance. `ScopeReopening` then moves the actual closure into a different logical owner, journals map activation, rejects stale begins/enrollments and imports addressed to another owner, and executes a new-map increment above the old floor. `ScopeRetention` keeps an independent actual root/holder obligation and performs its old-cut read after that increment; C4 owns physical custody transfer when ownership of material itself changes. |
| J8: compacted history | Actual checkpoint bytes/base plus retained accepted tail; `Retained`, `ChosenRetained`, `MaterialPresent`. The discard-suffix control loses previously accepted content. Arbitrary encoders/blob stores and journal-material GC are RecoveryMaterial/physical obligations. |
| R1/R4/C4: pending result retention across transfer | `PendingCutTransfer` captures the actual fallback and unresolved source obligation, transfers chosen control plus real physical custody, deletes old copies, erases/replays the new cut fold, and receives the same cut after source result or no-effect. Named crossings include a tail before import and a stale destination across retirement. Exact read, chosen source-record derivability, material authenticity and terminal-source monotonicity are checked independently. |
| R7: SOS control | Actual local pause persistence without quorum, optional admission-controller restart, export of local accepted/learned prefix and material provenance, then chosen resume and barrier reconciliation. Separate controls require quorum to pause, treat export as authority, or lose the durable pause. |
| R8: bootstrap discovery and control | `BootstrapDiscovery` starts with empty receiver definitions, copy IDs and hold inventory. Actual owner barrier snapshots disclose Begin/Register records; actual holder namespaces disclose pinned copy IDs, and exports supply the actual immutable bytes. A second root begins while the first export is incomplete; an initially empty destination really copies, holds and registers its recipe. A later barrier snapshot discovers this root/tail, including after observer restart. Serving readiness requires complete held recipe closure and independent expected reconstructed bytes; activation still requires an actual J-chosen record. Stale-inventory, early-readiness and export-as-authority controls fail separately. |

C1/C8 and bootstrap/SOS are authored causal history families. `JournalSchedule`
drains ordinary service through shared kernel transitions between named cuts.
This removes uninformative normal-path interleavings; it does not establish an
unrestricted product of every J/T/R schedule. `BootstrapDiscovery` varies export order while ordinary shared services drain. It
checks two scans around one late root and one optional observer restart, not
arbitrary repeated joins or failures of every discovery namespace. Namespace
exports preserve holder identity and exact copy IDs; incomplete pieces remain
non-serving until both the journal obligation and material closure arrive.
No harness inserts accepted records,
prepared leaders, repaired bounds or precompleted snapshots. The isolated models
check their wider local interleavings; the joined histories check these actual
records and their consumers, including their specified crossing alternatives. C1 additionally records the actual
accepted-voter count at the fault: `ExactCut` requires exactly one for the minority
case and a quorum for the quorum/decision/media cases. That observer exposed an
initial schedule that accidentally waited until all three voters accepted; the
authored sequence now releases the other participant first, then cuts at the
requested persistence boundary. Old receipts for that mislabeled history are
not minority-cut evidence.

## Fault and progress envelope

All quorum cases have three voters per configuration. Separate dimensions cover
two conflicting values, competing ballots, up to two terminal transfers,
overlapping/disjoint membership, explicit backend boundaries, one permanently
lost voter and two simultaneous pending-write reset targets. Finite authored
inputs bound the work, not a retry counter or state constraint. The compact order
model is safety-only and permits stopping after any prefix. It does not claim a
leader is eventually selected.

Concrete progress assumes the authored workload, surviving communicating quorum,
eventually stable eligible leader and available modeled backend service.
`slow-third` does not assume delivery or persistence by the third voter.
The missing-quorum control demonstrates that fair unrelated activity is not
completion. C1/C8 use fairness of their finite authored service schedule.
Bootstrap/SOS gives each relevant action weak fairness, plus an independent
unrelated-work loop. Its partition deliberately blocks J service while local
pause/export still complete. Resumption requires healing and a valid chosen
reconciliation record. The local pause persistence step is an atomic local storage
primitive; this model checks its independence from quorum, not storage hardware.

The trust boundary is crash faults, authenticated protocol messages, unique
configuration/ballot leader identities, exact atomic durable record completion,
and stable immutable command bytes. No Byzantine quorum, torn record decoder,
arbitrary network forgery or malicious storage is claimed. A permanently erased
voter may return only under a new storage identity; it cannot forget its promise
and keep voting as the old identity. Recovery from simultaneous permanent losses
beyond the named budget belongs to forensic reconstruction, not normal quorum
availability. Restored redundancy is separate from authority and readability.

## Controls, witnesses and retained sizing evidence

The catalog records exact expected property names. Controls cover promise
persistence, write overtaking, early accept evidence, wrong highest-prefix
selection, extension past terminal closure, wrong successor, erased identity
reuse, discarded compaction suffix, recovered read-bound loss, late enrollment,
missing quorum, quorum-dependent SOS pause, export-as-authority, lost local
pause, stale discovered inventory and premature serving readiness. Witnesses establish joint pending-write reset completion, duplicate raw
command positions, actual recovery snapshots, follower-first learning, leader-loss
completion, repeated handoff, compaction, completed C1/C8 histories, material before
authority, SOS export without quorum, late-root discovery after observer restart,
and retaining incomplete evidence without authority. A witness is reachability evidence, not
a completed safety graph.

Initial all-in-one concrete multi-ballot/multi-entry/transfer graphs did not finish
within their local sizing budgets. Their partial receipts remain incomplete and
are not acceptance evidence. The replacement separates physical ordering from
quorum order while retaining actual joined recovery histories. With one 512 MiB
worker, reviewed local joint persistence completes 90,481 states in about four
seconds; the two-ballot/two-entry compact order case completes 33,877 in about
12 seconds, and repeated transfer 13,757 in about 17 seconds. These are local
model-checking runtimes, not database performance estimates. The two-scan discovery family completes 4,675 states, or 4,544 with the authored
observer restart. A three-ballot/two-entry sizing run reached 2,380,304 distinct
states with 307,226 queued at its 900-second budget and remains incomplete.
Use the final frozen
campaign receipts for authoritative counts and source identity.

Model development found a real fold-recovery issue: replaying an already-enqueued
reservation returned its old empty response instead of reconstructing later grant
evidence, so a recovered driver could stall. TxKernel now derives the grant from
replayed held/announced/fixed/resolved state. It also erases and rebuilds migration
state on owner loss. Controls separately show that omitting a recovered read bound
or allowing post-close enrollment is observable. A TLA assignment-parenthesization
error could accidentally turn accumulated checker results into action guards; all
such observer assignments were corrected before acceptance runs. These are model
and candidate-protocol findings, not evidence that an implementation exists.

## Composition boundaries

The [architectural account](ARCHITECTURE.md#the-foundation-worth-sharing)
collects the shared mechanisms and their distinct completion meanings. The
following boundaries belong to these particular compositions.

Finite input occurrences, two authored inventory scans, one observer reset,
retained immutable export pieces across that reset, and rank-scheduled normal
service are model conveniences. They must not become production retry limits,
fixed rescan counts or a central scheduler. RecoveryKernel's one-reset snapshot
consumer is correspondingly narrower than a reusable concurrent recovery API:
that API needs request/generation correlation and supersession fencing. The model
catalog makes these finite histories visible instead of treating successful state
counts as an architectural completeness argument.

`BootstrapUnderPressure` and `SOSUnderPressure` make the remaining controller
resource arrow explicit. `RecoveryServiceKernel` fills ordinary memory and worker
capacity, then obtains a real CapacityPool lease for the control buffer, worker,
metadata, I/O, send and decoding workspace before registering a ViewsRegistry
operation. A captured query, received snapshot, namespace/copy export, recipe
classification, local pause or SOS export changes visible application state only
when that backend operation completes. Its lease survives callback completion and
optional observer cancellation until actual backend retirement. Missing control
reserve must fail target progress despite unrelated work; early retirement must
fail physical-lifetime accounting. The SOS witness exports while quorum service
is blocked. Provider-local journal/holder buffers are checked by C7, rather than
silently included in this controller's bundle.

The capacity peer independently reviewed this mapping. The wrapper serializes
controller actions while one captured job is outstanding, including its observer
restart; it does not add crash-during-staged-controller-work coverage. Physical
registry/reset crossings belong to Views/C7/C4. The bundle is a conservative unit
reservation for one bounded operation, not a claim that arbitrary recipes fit one
buffer. The ordering of captured operations is an authored service schedule; wider
export interleavings remain in the unwrapped discovery family.

C8 moves a stable logical owner to a new physical authority and replays its
certified prefix before applying the scoped transfer record. That prefix already
contains its metadata. The extra scoped payload therefore tests the advertised
record interface; it is not an argument for a second production transfer system.
An implementation may reconstruct from the certified prefix and keep only an
activation/fencing marker. Moving a subset into a different logical journal owner
is checked separately by `ScopeReopening`. It records a source scope seal naming
exact source/target owners and map versions after local old plans drain, then
carries that chosen record and the actual scoped payload into the destination's
journal. The destination validates the envelope before mutating application
state. A later journaled activation opens map 2. Its map adapter refuses stale
plan begins independently of whether a plan writes, and refuses stale scope
enrollment even after the destination opens. A new increment reads the imported
value 1, chooses a position above the old positive floor, and publishes value 2.
A destination restart reconstructs import and activation from an actual barrier
snapshot. Wrong-target delivery is a separate safe-refusal case, not a claim that
the absent intended destination completes.

This is one scope moving once between two logical owners, with the original
source's physical authority first transferred across disjoint witness sets. Old
work is drained; partially executed transactions are not retargeted. The source
and destination have one authored recovery each. Stale-plan arrival before versus
after activation is a named family dimension. The wrapper's inert command/event
sum tags prevent TLC from sorting unlike payload types; they do not prescribe
production wire formats. The fixed-parameter Tx kernel owns transaction ordering,
while the map adapter owns versioned admission and activation. Neither should
silently inherit the other's responsibility.

Retention ownership need not follow execution ownership. `ScopeRetention` starts
with an empty holder and imports the actual source version and interpreter
package through RecoveryKernel's physical write operations. At the real old read
bound it durably holds/registers an independent history-reader root before source
execution resumes. Resumption requires receipt of the actual matching ViewGrant;
an independent invariant ties its cut/context to the actual chosen bound record.
That retention owner stays in place while the map changes.
Only after the new-map increment publishes does the holder serve the pending old
cut, reconstructing byte 1 from actual held copies while the new result is 2.
The omission control closes/releases that root through its real journal and
physically deletes the copies; it must fail physical closure and target progress.
This composition checks the retention arrow directly rather than treating C4 as
a substitute. Its one-holder copy has no new storage failure; wider redundancy,
owner recovery and custody transfer remain the R families' obligations. Keeping
the retained-cut owner stable avoids inventing a second material-transfer protocol
for an execution-map change. The Tx peer independently reviewed both the map
adapter and the actual retained-read composition.

The strengthened C8 family completes 518 states for a new-plan refusal and 630 for
an old-plan drain. Its actual positive transferred-floor witness is reachable;
clearing the imported floor fails `RestoredBounds`. Corrected C1 minority-cut
completion takes 488 states with `ExactCut` asserting one durable voter. These
figures describe the reviewed finite histories, not an unrestricted migration
product. The optional two-ballot/transfer Cartesian sizing run was stopped after
830,798 states with 182,673 still queued to return capacity to required cases;
that receipt remains incomplete. The maintained catalog includes the separate
completed order dimensions and the concrete joined recovery/transfer histories.

The frozen journal campaign in `build/orbital-spec-restart/journal-frozen-final`
completed all 71 then-selected cases: 30 complete graphs, 24 expected violations,
and 17 witnesses. Its concrete two-ballot provider graph completed 214,989 states;
the selected three-ballot/one-entry order graph completed 28,420. The separate
two-entry graph, successor alternatives, repeated transfer and compaction graphs
are completed dimensions; the unfinished Cartesian sizing runs are not their
substitutes. Later map/retention additions and any changed shared Tx dependency
require their own source-captured receipts, listed by the maintained catalog.

The twelve `ScopeReopening` cases completed against the frozen corrected Tx
kernel: five complete histories/refusals (528, 529, 533, 534 and 420 states), six
intended invariant violations and one successful reopening witness. These include
stale begins/enrollments on either side of activation, destination replay,
missing floor/data, premature activation and mutation of a rejected import.
The retained-read composition is checked separately, including actual recipe
loss and target stall. Two 512 MiB stall attempts found the intended `Completes` failure, then
exhausted their heap reconstructing its 588-step temporal counterexample. They
are retained diagnostic failures, not accepted evidence. The exact-source 1 GiB
rerun completed the expected violation at 589 states; neither graph nor property
was weakened. Its maintained configuration uses TLC's diagnostic `ALIAS`
rendering to keep traces readable, with no VIEW, state constraint, or change to
the checked transition/temporal graph. Large joined command payloads make trace
size a sizing concern even when the graph has only hundreds of states.


The current-source C1/C8 refresh completed all fourteen selected cases. The
retained-read baseline completes 572 states, actual premature release fails
physical closure at 586, and the retained-read witness is reachable at 569. The
concrete duplicated follower-ingress graph completes 66,480 states; its two
submission-envelope witness is reachable. These receipts are in
`journal-current-k-final`, with the exact-source higher-heap stall receipt in
`scope-retention-stall-1g`. Current dependency hashes, rather than these directory
names alone, determine which receipts qualify.

## Pending source obligations through retention-owner movement

`PendingCutTransfer` joins the actual transaction fold, cut-material fold,
RecoveryKernel holder operations and DurableLog control journals. A source is
fixed below a reader's cut but has not yet resolved. The old root therefore owns
both fallback bytes and a pending source obligation. Its chosen seal transfers
the actual source-event prefix, exact reader context and symbolic closure. The
successor begins with no material: fallback and interpreter bytes are copied,
held and registered through the existing root protocol. Its custody receipt
also names the chosen control import and the surviving transaction owner that
owes future source results. Only received custody releases the old holder; its
copies are actually deleted.

The successor cut fold is then erased and rebuilt from an actual barrier
snapshot. Reader-visible material requires received matching ViewGrant and
Material records, as well as the real retained copies. Replaying control before
those bytes arrive retains the symbolic fallback requirement and cannot produce
a reader result. Source resolution comes from an actual chosen transaction
installation record; its result bytes acquire a new durable holder obligation.
The source producer survives this particular controller loss. Its outbox is
checked against its retained installation records; this family does not claim
producer-counter recovery or arbitrary simultaneous loss of both providers.

Named crossings place resolution after old-owner loss, before prefix import,
and in an old-address envelope spanning retirement. The producer learns the
successor from the actual chosen-seal route message and retains its original
source identity when resubmitting. A later duplicate prefix cannot erase newer
tail facts. An independently newer replacement lies above the reader's cut;
collection pressure therefore cannot justify substituting that head. The same
reader must return 7 for the qualifying source result, or its original fallback
0 for a no-effect resolution, and then write 8 or 1 respectively.

Peer review exposed an intake error that final-byte checks alone missed:
replaying unordered tail arrivals directly could turn an already resolved source
back into an announced or fixed source. A complete authoritative Resolve fact
contains its final position, decision, scopes and effects, so the adapter keeps
that source's terminal fact when lower-phase evidence arrives later. Announce is
only a lower bound and may differ from the final position. This per-source rule
preserves unrelated-source independence; it does not impose a global tail
barrier. A reverse-arrival control checks the distinction. Missing pending-control,
missing source-resolution tail and premature custody are separate controls.
The transfer history remains authored and bounded, with one source controller
loss and one successor-fold reset; it is not an unrestricted J/T/R product.


The final review accepted the source intake only for one immutable complete
outcome per source. It is not a generic merge for mutable IDs or partial results.
The surviving transaction owner and holder are assumptions of this seam; one cut
controller is lost and one successor cut fold is reset. Ordinary service drains
between authored fault cuts, and the early-resolution/custody predicates that
inspect Tx progress select those histories rather than authorize production
operations. The `sealed` flag selects the relevant subset of locally derivable
source records retained in the outbox; it does not represent a producer learning
remote authority. The producer learns its route through the actual chosen-seal
message. Rehydration is a checked projection of received grant/material evidence
onto local held copies, not permission for a consumer to read remote state.

This seam materializes only the pending source and newer replacement needed for
the old-cut comparison. The reader's resulting write remains in the actual Tx
installation and control records; its own output-retention lifecycle is outside
this particular transfer history. It is not assigned another source's root ID.


The frozen fourteen-case pending-cut campaign completed in
`build/orbital-spec-restart/pending-cut-current`: six complete graphs, four
expected violations and four reachable witnesses. Healthy result/no-effect
histories complete at 10,420/6,390 states after loss, 9,784/3,537 with resolution
before import, and 29,875/11,620 with an old-address message across retirement.
Missing pending control fails `NoPrematureRead`; dropping only source 1's
resolution fails `Completes` after the newer head and collection-pressure step
have succeeded; premature custody fails `CustodyControl`; naive tail replay
fails `SourceTerminal`. The reverse-tail witness completes with exact old-cut
semantics. The final source hash is
`dfd36deab7473f0b5227c2b3ba9c35c507d8854da97a2e7468898cc3c1273da6`.
All these checks use the pinned TLC build and explicit finite history bounds.
