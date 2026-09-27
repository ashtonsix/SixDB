# Execution, extensions and restoration checks

This report maps the execution obligations in [PLAN.md](PLAN.md) to executable
checks. The catalogs are [tx-cases.json](tx-cases.json),
[epoch-cases.json](epoch-cases.json), [recovery-cut-cases.json](recovery-cut-cases.json)
and [effect-cases.json](effect-cases.json). Exact completed runs and source
hashes belong to the campaign receipts; an older successful receipt is not evidence
for a subsequently changed dependency.

These are three different kinds of evidence:

- Transaction and epoch families exhaust their finite state graphs, with the
  named abstractions below. Independent expected observations and outcomes do not
  authorize production-model transitions.
- `TransactionRefinement` checks every prefix of a concrete delayed-fact execution
  against an eager-fact execution. This is a bounded simulation check under its
  healthy-service preconditions, not an unbounded refinement proof.
- C2, C6 and C9 join real kernels at authored causal cuts. Ordinary service drains
  between those cuts; the remaining races are explored. They establish the named
  compositions, not an unrestricted Cartesian product of every model.

## Mechanisms and bounds

`TxKernel` contains reservation queues, generated positions, registered read bounds,
pending outputs, logical versions, private execution, verification reports,
outcomes, decisions and installation. Reservation and read-invalidation relations
are distinct application-supplied opaque sets. A transaction reserves its output
shards in a common order, obtains one position, and releases each local reservation
when that shard durably fixes it. Execution waits for all required fixes. Reads
select by logical position and wait for unresolved predecessors unless a committed
replacement covers the required dependency. Publication follows the transaction's
required verification and installation.

The core has three transactions on two shards: two overlapping writers and a
reader. Variants include a third source shard, read-only checks, a tombstone,
causal lower bounds, pre-position cancellation, scope migration, and asynchronous
integrity checks. The four-transaction family puts a slow no-effect predecessor,
an RMW, a later replacement and a reader on one owner. Its unrestricted schedules
check the version chain; the three-transaction/two-shard family and C1 check
cross-owner partial progress. Co-locating the four-transaction chain is an explicit
finite-family choice, not a hidden state constraint.

Healthy families can use the eager-fact provider: only delivery of positive facts
is folded into the durable command step. Application, decision and publication
steps remain delayable. The two-transaction writer/reader refinement case uses the
actual `DurableLog` provider and delayed messages, comparing every prefix's state,
commands and observable emissions. Cancellation/migration retain explicit fact
delivery. Recovery uses actual journal snapshots; C1 replaces an owner and driver
through `JournalKernel`, rather than assuming an immortal transaction cache.

`TxProgram` is the shared application/extension interpreter used by transaction
execution, both epoch consumers and C2. Requests insert query continuations;
conditional results determine later query nodes. Calls, query responses, private
writes and output IDs retain the parent's context. `TransactionOracle` independently
interprets the fixture programs and their serial inputs; it does not invoke this
interpreter. The epoch oracle separately states expected results, query traces,
ordering metadata and outputs.

`EpochExecution` has two independently scheduled consumers, three transactions,
and two or three agreed epochs. Capture, materialization and execution are separate
steps. Declared observation/write conflicts derive the logical reference order;
comparing final deltas is insufficient. Logical closure waits for in-epoch work
regardless of physical readiness. A checked transaction carries forward until a
later agreed verification input; an independent writer publishes in the first
epoch. Both conditional branches are exercised by the matching and mismatching
verification families. C2 uses actual transaction positioning, read registration,
verification, decision and publication with two separately stepped `TxProgram`
executions. Its forward/reverse protocol schedules are authored families, while
the consumers' physical schedules remain independent.

`EpochTransactionFolds` closes the remaining metadata seam: two actual `TxKernel`
consumers fold the same immutable journal batches, including grants, read bounds,
verification, decision and installation. Their complete kernel states and every
full logical event are compared at epoch closure. Stable event slots serialize
logical outputs; repeated identical transport events coalesce by logical ID, while
conflicting content under one ID is an invariant failure. Physical message-arrival
order is not the epoch output order.
The three-transaction/two-owner cases cover selected opposing owner orders and
arbitrary eligible-owner order, always preserving each owner's prefix. Local
read/compute/materialization steps remain independently enabled.

The pure `EpochFoldKernel` reads only its own consumer record and frozen input.
There is no shared capacity, transport queue or clock. The large family explores
one consumer to completion and then the other: commuting independent steps
preserves every pair of local traces and closed results. A smaller unreduced
one-transaction/two-owner product checks mutual step enabledness and exact
successor commutation, and witnesses actual physical skew. This reduction says
nothing about cross-consumer real-time progress or shared-resource performance.

## Obligation map

Configuration names below are under `configs/`; braces expand to the listed
exact filenames (for example `Transaction-{small,pair}.cfg` names two configs). “Joined” identifies authored seam
families; references to another report/model assign that part of the obligation to
its actual owner rather than silently assuming it here.

| ID | Independent observation / executable evidence |
| --- | --- |
| T1 | `Transaction-invalidation.cfg`: `SerialReads`, `SerialOutcomes`, `Envelope`; opaque read dependencies extend beyond the reserved output relation. |
| T2 | `Transaction-small.cfg`: `ExclusiveReservations`, `GrantOrderPositions`, `NoEarlyExecution`, `PositionImmutable`; `Transaction-witness-partial-fix.cfg` reaches partial fix; `Transaction-early-release.cfg` and `Transaction-early-execution.cfg` break the respective properties. |
| T3 | `Transaction-small.cfg`, `Transaction-causal.cfg`: `UniquePositions`, `PositionImmutable`, `Causality`. `HistoricalSnapshot-{retained,refuse}.cfg`: `RequestedCutPreserved`, `RequestedFreshness`, `HistoricalResult`; upgrade/downgrade controls reject changing the selected cut or violating the supplied requirement. Requested freshness is an input lower bound, not a wall clock. |
| T4 | `Transaction-own-read.cfg`, `Transaction-three-shard.cfg`: `RegisteredBeforeRead`, `OnlyDeclaredBounds`, `OwnEffects`; `Transaction-witness-late-input.cfg` reaches a second source at the original c; `Transaction-private-bound.cfg` breaks `OnlyDeclaredBounds`. |
| T5 | `Transaction-{small,pair,checked,chain}.cfg`: `SerialReads`, `SerialOutcomes`, `JustifiedOutcome`, `Publication`; `Transaction-{always-abort,spurious-mismatch,fabricated-mismatch}.cfg` break independent failure justification. |
| T6 | `Transaction-small.cfg`: `SerialReads`, `SerialOutcomes`, `Publication`; `Transaction-witness-partial-install.cfg` reaches partial installation; `Transaction-last-arrival.cfg` breaks `SerialReads`. `EpochTransactionFolds-self-source.cfg` catches reading one's already-installed output as original source input. |
| T7 | `Transaction-{chain,mismatch,tombstone}.cfg`: `SerialReads`, `SerialOutcomes`, `JustifiedOutcome`; `Transaction-skip-pending.cfg` breaks `SerialReads`; `Transaction-witness-mismatch-abort.cfg` reaches agreed mismatch abort. |
| T8 | `Transaction-chain.cfg`: `SerialReads`, `SerialOutcomes`; `Transaction-witness-replacement.cfg` returns 9 while the earlier no-effect transaction and RMW remain unresolved. Replacement must cover the opaque read dependency. |
| T9 | `Transaction-undeclared.cfg`: `Envelope`, `JustifiedOutcome`; `Transaction-migration.cfg`: `MapTransferSafe`. C8's exact authority/migration configurations are in [JOURNAL.md](JOURNAL.md): `ScopeReopening` additionally imports into a different logical owner, activates its new map, rejects stale plans and executes new work; `ScopeRetention` preserves an outstanding old-cut view across that move. |
| T10 | `Transaction-cancel.cfg`: `NoLocalResurrection`; `Transaction-witness-crossed-cancel.cfg` reaches cancel with a crossed reservation; `Transaction-resurrect.cfg` breaks the fence. Post-position mismatch resolves outputs in `Transaction-mismatch.cfg`. C1 supplies actual owner/driver loss, snapshot replay and the replay-drop-bound control. |
| T11 | `Transaction-progress.cfg`: finite `Completes`; missing-resolver is a temporal control. `IndependentTransactions-basic.cfg`: `IndependentCompletes`, `RemoteStillPending`, `IndependentMeaning` require a real client commit while a genuine remote read remains withheld forever; barrier is a temporal failure and witness reaches that client result. `Reservation-{ordered-progress,eligible-starvation,eligible-narrow,ordered-bridge,eligible-bridge,broad-witness,bridge-witness}.cfg` separately checks continuing local arrivals using the shared grant predicate. `reservation-cases.json` adds the revised drain rule and alternative-policy boundaries; `policy-binding-cases.json` checks its actual transaction/replay binding. Actual capacity pressure belongs to P/C7. |
| T12 | `Transaction-three-shard.cfg` and `Transaction-witness-late-input.cfg` retain c. Core source material is explicitly unreclaimed; `Retained-late-source.cfg` checks actual exact-cut data or evidence-backed failure. `HistoricalSnapshot-{retained,missing}.cfg` consumes actual old bytes or reaches an agreed unavailable outcome at the selected cut; latest-material control breaks `ExactMaterial`, old/missing witnesses distinguish both paths. C5 supplies physical views. |
| E1 | `Epoch-{basic,three,mismatch}.cfg`: `Deterministic` compares full semantic projections under arbitrary permitted consumer schedules. `EpochTransactionFolds-{basic,remote,mismatch,any-order,any-mismatch}.cfg`: `ClosedAgreement`, `UniqueOutputSlots`, `SerialMeaning` compare complete actual kernel state and serialized logical events. `EpochTransactionFolds-correspondence.cfg`: `ProductCorrespondence` checks the unreduced product; `EpochTransactionFolds-drop-bound.cfg` breaks agreement. |
| E2 | `Epoch-basic.cfg`: `CorrectPrograms`, `OrderingMetadata`, `OutboxSemantics`; `Epoch-commuting.cfg` breaks independent program semantics despite compatible final deltas. Actual fold cases additionally check `SerialMeaning`. |
| E3 | `Epoch-three.cfg`: `LogicalClosure`, `ContinuationRetained`; `Epoch-{physical-close,drop-continuation}.cfg` break those rules. `Epoch-witness-resume.cfg` and `EpochTransactionFolds-witness-resume.cfg` reach later input resuming the same context/position; `EpochTransactionFolds-physical-close.cfg` breaks real-kernel logical closure. |
| E4 | `Epoch-basic.cfg`: `IndependentPublication`, `NoSpeculativePublication`; `Epoch-early.cfg` breaks early publication; `TransactionEpoch-progress.cfg` reaches unrelated publication. `Epoch-witness-skew.cfg` and `EpochTransactionFolds-witness-skew.cfg` reach independent physical progress. |
| X1 | `Transaction-small.cfg`: `JournaledProfile`, `PolicyContext`; `Transaction-source-profile.cfg` breaks recorded-profile comparison. `VersionedExecution-checked.cfg`: `CodeVersion`, `JournaledProfile`, `SerialOutcomes` bind actual retained program/decoder observations to the source version selected by the journaled profile; its wrong-version and missing-fallback controls break `CodeVersion`. |
| X2 | `Transaction-{checked,mismatch}.cfg`: `AllChecks`; `Transaction-{final-only,missing-checker}.cfg` break it. `TransactionEpoch-{basic,reverse}.cfg`: `SameProgram`, `VerifiedPublication`; `VersionedExecution-checked-disagree.cfg` exercises actual differing code execution reports. C5 tests two checkers agreeing with each other but disagreeing with the selected main output. |
| X3 | `Transaction-{readonly-check,checked}.cfg`: `AllChecks`, `Publication`, `OnlyDeclaredBounds`; `Transaction-private-bound.cfg` fails a checker-created ordering bound. C5 supplies actual separate private physical views. |
| X4 | `Transaction-{approved,small,checked}.cfg`: `PolicyContext`, `Publication`; `Transaction-witness-approved.cfg` reaches publication without synchronous reports. `VersionedExecution-approved-audit.cfg` executes the approved path using retained code. ViewsRuntime/C5 checks actual data/I/O authority independently of approval. |
| X5 | `Transaction-async.cfg`: `AsyncStable`, `IncidentSound`; `Transaction-witness-async-incident.cfg` reaches disagreement. `VersionedExecution-{approved-audit,approved-incident}.cfg`: `CodeRetention`, `AuditMeaning`, `RootClosure`; `VersionedExecution-early-release.cfg` breaks audit retention, and its reach-incident/reach-retained cases witness both milestones. |
| X6 | `RestoredEffects-{normal-dedup,normal-unknown,pitr-dedup,pitr-unknown}.cfg`: `CommittedOnly`, `AtMostOnce`, `KnownResults`, `NoNewIntentIdentity`, `AmbiguityPreserved`, `FreshIntentCompletes`; new-identity/retry-unknown controls break `AtMostOnce`. A5 is a distinct early channel. |
| X7 | `Epoch-{basic,three,mismatch}.cfg`: `CorrectPrograms`, `ContinuationRetained`, `OutboxSemantics`; `Epoch-witness-dynamic.cfg` reaches dynamic query discovery. `TransactionEpoch-{basic,reverse}.cfg`: `ExactContext`, `SameProgram`, `SemanticRefinement` join actual transaction protocol and two independently stepped interpreters. |
| C2 | `TransactionEpoch-{basic,reverse}.cfg` checks actual T-to-program/report/outcome composition; `EpochTransactionFolds-{any-order,any-mismatch}.cfg` checks the two actual transaction folds, and `EpochTransactionFolds-correspondence.cfg` checks the independent-product reduction. |
| C6 | The four `RestoredEffects` positive configs above use actual C9 restoration and effect-owner replay. `RestoredEffects-witness-{dedup,unknown}.cfg` witnesses applied/lost-reply restoration and explicit Unknown. `Effects-{dedup,unknown}.cfg` independently explores the sink leaf; its retry-unknown control and lost-reply/unknown witnesses exercise ambiguity. |
| C9 | `RecoveryCut-{normal,pitr}.cfg`: `CheckpointDurable`, `CursorExact`, `NormalRestoration`, `ClosedCut`, `PhysicalFence`, `Abandonment`, `NoHistoricalEmission`; unpersisted/cursor/torn/fence/old-lineage/abandonment/reemit controls and partial/restore witnesses target those rules. `RestorationMaterial-{normal,pitr}.cfg` adds `MaterialMeaning`, `PhysicalClosure`, `ReadBeforeWrite`, `ActualRedundancy`; early-write/early-redundancy controls and readable/degraded/repaired witnesses separate the three recovery milestones. |

Approved execution is permission to omit synchronous agreement checking, not a
claim that arbitrary native code is deterministic or safe. Determinism-hardened
WASM, hash soundness, faithful sandbox enforcement, and the application's correct
opaque plan/dependency declarations are explicit external contracts. The fixture
programs terminate and have bounded output. No progress claim covers an infinite
application, endlessly generated children, permanent loss of required service, or
an unavailable externally promised source.

## Guarantee inputs and useful progress

Known causality and requested freshness enter as an agreed lower bound. Writer
minima and the default readonly snapshot selection respect it; there is no
wall-clock or global real-time order between independent clients. C1 compares the
exact recovered position and positive source read floor, normal C9 restoration
compares positions/fixes/bounds, and scope reopening imports that floor. Late
source reads and resumed epoch continuations keep their captured position and
profile instead of asking for a fresher snapshot.

The default kernel's readonly entry chooses a generated source-owner minimum.
`HistoricalSnapshot` separately checks a candidate mode in which the application
selects an already-retained older cut after a newer writer publishes. It replaces
only that readonly position proposal; it has no reserved output scopes whose
minimum it could ignore. The requested cut must satisfy the actual supplied
causality/freshness requirement; an incompatible request uses the existing agreed
pre-position cancellation. Normal bounds, reads, outcomes and publication then
run through the same kernel and durable log. Actual retained bytes return 0 while
the newer value is 7; collected old bytes instead cause an agreed unavailable
outcome at the unchanged cut. The retained-positive family keeps the prior domain
uncollected (`gc=FALSE`); the missing family collects before the new request.
Admission cannot recreate an already-lost historical version. This mode changes
neither the default selector nor
the system's consistency promise. Its age/performance tradeoff remains empirical.

`IndependentTransactions` holds an actual source owner's read response forever,
then admits a disjoint transaction on the stalled transaction's output shard.
Fair local kernel/journal/delivery service must produce its actual client-visible
commit and value. The blocked transaction retains its position and missing input;
there is no fairness assumption on the unavailable remote response. A shard-wide
barrier mutant fails that temporal requirement. This is useful transaction work,
not a heartbeat, and it does not claim progress for the genuinely dependent work.

Finite workload drain does not establish fairness under continuing arrivals.
The maintained `ReservationKernel` preserves original enqueue order through both
waiting and holding. A waiter becomes a barrier only after all older conflicting
requests have locally fixed or cancelled. `TxKernel` closes eligible grants in
that order after each agreed record; replay uses exactly the same fold. Physical
delivery can lag without changing the chosen grants.

The [33 policy checks](reservation-cases.json) exercise continuing narrow arrivals,
original and chained waiter bridges, an unrelated older holder, the younger-WAN
drain boundary, replay/duplicate/cancellation histories and common-order
acquisition on two shards. Weak fairness applies to actual arrivals, releases and
service, not an assumed eventual broad grant. Eligible-first has a starvation
cycle; protecting only the oldest live request makes an unrelated old holder
suppress useful protection. Drain requires a finite older conflict prefix and
eventual release of that prefix and any already-held younger conflicts. The cyclic
local slots are fresh requests after local release, not recycled full transaction
identities. These finite checks support that conditional argument rather than an
unbounded full-protocol theorem.

`PolicyTxBinding` adds actual Tx/DurableLog composition: X retains its local group
while a real remote request is delayed; B queues broadly behind it; C must commit
on B's other scope before X's remote request is released. An owner-cache-loss cut
recovers through a real barrier snapshot. Full cached replies and logical outputs
match replay of the locally delivered prefix, and an independent oracle checks
client value and installed version. Ordered is a deliberate stalled comparator;
deferred grants, missing cached grants and missing replayed grants fail separate
properties. The authored service history complements the wider transaction/epoch
families. The seven older `ReservationPolicies` checks retain their explicitly
ordered/eligible policies as historical controls.

## Joined restoration and external effects

C9 imports actual transaction replay into `RecoveryCut`. It checkpoints after only
one participant has installed the first transaction, persists the checkpoint bytes
and decoder before the pointer, then runs the head forward. Recovery obtains actual
`DurableLog` barrier snapshots, reconstructs checkpoint plus suffix, and compares
positions, bounds, versions, decisions and image with independent pre-disaster
observations. PITR reconstructs complete transactions at the selected cut, records
the original later outbox IDs as abandoned, and requires a separate deployment
fence before reopening. Choosing a journaled lineage cannot physically isolate the
old deployment by itself.

The 11 `RecoveryCut` cases check normal/PITR reopening, durability-before-pointer, exact replay
cursor, closed-cut reconstruction, old-lineage rejection, explicit abandonment and
replay emission suppression. Each has a targeted defect or witness. The family has
one checkpoint, one restoration attempt and a completed head; repeated/concurrent
restore generation handling and unresolved-at-head recovery are not established by
this wrapper. C1 supplies unresolved authority/transaction recovery. C9 models local
checkpoint material persistence; distributed custody and GC use R/C4/C5.

Seven `RestorationMaterial` cases feed the actual reconstructed image and retained
decoder into an initially empty physical holder. Actual import, backend persistence,
held recipe, journaled registration, grant and material delivery establish
readability. The C9 deployment fence and lineage opening establish write authority.
Only a later real copy/hold/registration on a second holder establishes restored
redundancy. Independent expected bytes and actual holder custody check each claim.
The three milestones have separate witnesses; neither journal authority nor a
readable first copy is treated as proof of two-copy durability.

C6 `RestoredEffects` extends the actual C9 module rather than synthesizing a restored
flag. The first committed effect is applied, its reply is lost, and the later
transaction's effect remains durably pending across the disaster. The effect owner
replays its real journal. Normal recovery delivers the previously undispatched
intent; PITR suppresses the abandoned later intent. The context is installed before
redispatch. Deduplicating sinks accept a retry under the original effect ID;
non-deduplicating sinks record Unknown for an old ambiguous dispatch. A fresh
post-recovery dispatch cannot become Unknown without a new ambiguity event.

The 13 effect cases include normal/PITR × deduplicating/non-deduplicating sinks,
independent sink application counts, committed-only eligibility, fresh-intent
completion, two unsafe-retry/identity controls and lost-reply/recovery witnesses.
There is one authored owner loss; only the new driver incarnation retries its old
inflight dispatch. No arbitrary retry-attempt cap closes the graph. The leaf's
single reply loss precedes that loss; same-generation timeout policy and repeated
outages are outside this finite family. Delayed replies acknowledge the same
immutable logical effect and value, regardless of driver incarnation. Exactly-once
application at an arbitrary non-deduplicating external service is not claimed.

## Progress, controls and review findings

Transaction progress uses weak fairness of kernel, input and durable-service action
families in a finite workload. Epoch progress separately makes each consumer's
capture/materialize/execute/decide and close/advance actions weakly fair. C2 adds
fairness for actual consumer/report steps. C6/C9 use fair service at their authored
cuts. Terminal states explicitly stutter. The missing-resolver control retains
unrelated ongoing work, so its failure is a temporal stall, not a deadlock invented
by ending the trace.

The models and reviews changed concrete mechanisms:

- Verifier agreement must match the selected main execution output. Two matching
  checker results cannot authorize a different main result. Failure provenance
  compares against retained original primary execution evidence, not the produced
  failure outcome itself; changing that outcome cannot manufacture disagreement.
- Epoch context capture includes declared logical inputs. Capturing an entire
  physically installed local image made independent consumers disagree because
  unrelated work could install at different times.
- Replaying an old reservation request must regenerate the later durable grant;
  replaying only its original empty enqueue reply stalled a recovered driver.
- A delayed consumer must exclude the parent transaction's installed output from
  its original source view. Otherwise replaying after local installation changes
  the input. The shared source abstraction was fixed; the physical retained-source
  provider already excluded the parent. Explicit private program effects still
  supply reads of the transaction's own tentative writes.
- Source retention requests carry the original execution profile/cut, and profile
  replay consumes the journaled source/policy record.
- Recovery installs abandoned-effect context before redispatch and retains stable
  effect IDs. A new driver does not turn a potentially applied non-deduplicating
  request into a safe retry.

The journal owner independently reviewed the positive-fact quotient, C9 restore
semantics and C6 sink/replay boundaries. The runtime owner reviewed and exercised
actual physical observations and output publication in C5. C9 review added
independent abandonment and historical-emission controls. C6 review replaced an
unjustified retry cap with one retry opportunity derived from the authored owner
loss. The journal peer also reviewed the pure epoch-consumer independence reduction and
the remote-withheld/historical-entry wrappers. Their bounds depend on immutable
input batches or finite authored work; they do not transfer aggregate fairness to
unbounded arrivals. These reviews narrow the evidence claims; they do not replace the recorded
TLC checks or establish an unbounded theorem.

## What changes the architecture

The [campaign retrospective](https://github.com/ashtonsix/SixDB/blob/e40e87c/orbital/spec/ARCHITECTURE.md#classifying-what-changed)
distinguishes protocol findings, model defects and abstraction limits across the families.
These source-view and scope-movement findings retain their specific boundaries:

| Finding | Classification and consequence |
| --- | --- |
| Scope transfer carried a state payload without applying it | A missing composition step in the model. C8 consumes actual source-owner bounds, versions and immutable command/reply records; its later reopening/retention wrappers exercise a different logical owner, new-map activation and an outstanding old-cut read. This remains one authored scope movement, not arbitrary repartitioning. |
| A slow consumer read its own installed output as its original input | Source-abstraction defect and implementation precision for the existing captured-input contract. Source selection excludes the parent output, while the private program separately supplies its own tentative effects. The concrete retained-source provider already made this distinction. |
