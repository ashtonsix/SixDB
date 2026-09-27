# Orbital verification obligations

This document maps the correctness-critical design in [BRIEF](../BRIEF.md) and
[PHYSICAL](../PHYSICAL.md) to maintained model obligations and their assumptions.
[RESULTS](RESULTS.md) owns current evidence and open work; the family reports map
the requirement IDs below to concrete models, configurations and properties.
The implementation-facing [architecture](../ARCHITECTURE.md) explains how these
concepts fit together without treating formal modules as runtime components.

## Observable guarantees

1. Admitted history has one recoverable authority and interpretation. Failure,
   retransmission, relocation and restoration cannot silently choose another.
2. Published transaction observations and effects agree with one serial position,
   including overlapping writers, late reads, no-effect outcomes, partial physical
   installation, checked extensions and failed coordinators.
3. The same agreed input produces the same complete logical state, continuations
   and protocol outputs under every permitted consumer schedule. Waiting for
   remote work does not prevent independent work and metadata from progressing.
4. Live logical obligations retain a complete reconstruction path. Transport,
   physical residency, historical receipts and publication are distinct facts.
5. Physical access preserves the requested version, permitted bytes and lifetime.
   No reuse, cancellation, callback or page installation can corrupt another user.
6. Under explicitly stated service and fault assumptions, admitted finite work can
   complete or reach an agreed failure. Resources and recovery must not introduce
   a circular wait that those assumptions merely conceal.

Evidence for each requirement needs a distinguishing completed configuration,
reviewed abstraction and assumptions, and a retained result. Deliberate defects
must fail the intended property; witnesses establish that the named path occurs.
An unresolved internal dependency remains a gap even when its consumers pass.
TLC checks configured finite graphs, not arbitrary-size implementations.

## Boundary of the verification

Orbital sees stable identities, opaque scopes, deterministic overlap/invalidation
relations, permitted operations and their declared observations/effects. Engine
or another application owns what a row, index, object or reduction means. Small
integer/set fixtures give the models independent result semantics; the protocol
cannot inspect that meaning to make its decisions.

External contracts are limited to: atomic durable-record primitives under the
declared storage fault model; non-forged message identity/integrity; sound symbolic
equality standing for hashes; deterministic approved programs/runtime profiles;
correct application declarations and equivalence laws; a platform enforcing
the requested mapping/capability operations; and the explicit deployment fence
required for operator-directed PITR. That fence isolates old admission/execution
authority but cannot undo an external effect or recall an in-flight old message.
Runtime probes, native tests and
future implementation refinement own those realizations. Arbitrary application
termination, future network delivery and survival of enough material are explicit
conditions on liveness, not safety assumptions.

Journal agreement, owner recovery, fencing, logical retention, replica confluence,
verification gates and completion accounting are **internal Orbital obligations**.
They cannot be declared external merely to make a model small. A consumer may use
an abstract service only when this plan identifies its concrete provider, the
same interface state/events, and the refinement or composition check connecting
them. Safety of a service and eventual availability of that service are separate.

Physical latency, throughput, cost, warning time and the greatest distress within
a service objective remain simulator/hardware questions. Formal checks establish
the forbidden histories and conditional progress underlying those measurements.
Detailed route/placement optimizers, OS syscall implementations and arbitrary
application algebra are not silently made part of the TLA proof obligation.

## Protocol bindings used in verification

These bindings make the modeled mechanisms explicit. Some restate the briefs;
others select candidate mechanics where the briefs leave a choice open, including
scope enrollment, root messages, early dispatch ranges and the SOS pause policy.
Checking a candidate does not promote it into the production architecture. The
family reports own the exact fault boundaries and finite configuration limits.

- A crash-fault journal uses durable promises and accepted prefixes, explicit
  prepare/recovery and quorum acceptance. Prepared leadership survives ordinary
  epochs. A leader recovers accepted evidence before extending; changing physical
  routes is not a change of ballot or authority.
- A transaction has a stable owner shard whose replicated journal records its
  context, immutable position, outcome and decision. A coordinator is a replaceable
  driver of those records. Permanent loss of its local disk does not erase the
  decision. Read-only work also has a journaled context/bounds owner.
- An authority transfer is a terminal agreed record naming the successor and
  exact final prefix. Old recovery preserves closure. Successor activation requires
  recovering certified journal/protocol state; serving each scope additionally
  needs its material/control closure. Copying bytes alone grants no authority.
  Competing successor proposals and delayed old traffic remain
  possible in the model.
- Logical retention is recorded in the responsible owner's replicated journal,
  with explicit holder reservations and transfer/release acknowledgements.
  Physical writes can outlive their submitter. Immutable material/checkpoint
  writes cannot themselves create a logical root or publish a new registry;
  only an agreed record does that. Acquisition may be derived from an existing
  same-owner record under the rule below.
- Data protection names its fault interval. Historical receipts do not reset a
  destruction allowance. Repair, storage incarnation and the closure needed to
  renew protection are explicit. No actor can consult the observer's live-copy
  count as if it were local knowledge.
- Normal recovery preserves chosen history and unresolved obligations not ruled
  out by valid journal recovery; it need not preserve a ruled-out minority proposal.
  Explicit point-in-time
  restore creates a fenced lineage and records the abandoned suffix; replay does
  not independently reissue external effects. An external sink still needs its
  own deduplication/transaction contract for exactly-once application.
- Loom admission reserves a path for resolution/completion, including metadata
  and I/O, separately from ordinary workers and working bytes. Local resource
  failure requests an agreed outcome or preserves recovery; it does not invent
  an outcome privately.

**Reservation policy.** The current design uses
original enqueue order with protection after older conflicts release. Waiting or
held requests retain their local age until durable fixation or cancellation.
A waiter excludes younger conflicts only after its older live conflicts are gone;
actual holders always exclude conflicts. Every agreed local record performs the
canonical grant closure before exposing its state and outputs. Replay preserves
that interpretation, live order and full reply evidence. Continuing-arrival
progress, the admitted-younger-WAN boundary and the actual Tx/journal binding are
maintained obligations; the previous ordered/eligible projection remains a
counterfactual comparison. No in-place change of old journal interpretation is
assumed.

**Scope changes.** The modeled enrollment protocol binds a versioned scope/authority map before acquiring
any reservations. Migration closes enrollment of new plans touching the moved
scopes, drains enrolled old plans, transfers ordering/version state and preserves
outstanding retention obligations, moving their ownership only when needed, then
opens the new map. It never retargets a partially acquired plan.
Closing only the destination reservation gate can deadlock: a new-map waiter may
hold another shard needed by an old-map transaction whose completion migration
requires. That cycle is a required control.
Enrollment itself is distributed: a plan acquires no reservation until every
version-bound enrollment succeeds. Rejection or crash during partial enrollment
uses the pre-position cancellation/tombstone protocol to release earlier grants;
late replies cannot resurrect it. No atomic global enrollment oracle is assumed.

**Late source retention.** Discovering a new source retains the same `c`. Exact-cut
acquisition must establish a complete retained recipe or propose an agreed failure.
It cannot substitute the current version. Successful completion therefore requires
the discovered input still to be reconstructible. Applications may pre-retain a
broader domain for a stronger success guarantee, consuming history capacity without
adding read reservations. This accepted tradeoff must be visible in the findings.

**Protection renewal.** A durable protection generation begins before its copy
requests. Receipts name the generation and storage incarnation, after persistence
of the complete recipe. A certificate with `k` independent holders supports at most
`k−1` destructive domain losses since that generation began, including losses during
copying. It does not confer additional losses starting when the certificate arrives.
Old receipts cannot renew protection. Eventual stability permits restoring actual
redundancy; unobserved destruction after a receipt still prevents an unconditional
claim about present copies.

**Producer discovery and early execution.** Before receiving events, a durable
stream registration identifies its holders and bounded directory/tail discovery
path. A promised durable submission needs its immutable ID/body and interpretation
closure at the required holders; it cannot be an undiscoverable orphan. L frontiers
order local producer inputs; C1/C2 use stable transaction IDs without producer-prefix
ordering. The early path durably preallocates disjoint dispatch ranges binding a
stable processor, source stream/incarnation and LSN interval to a generation.
Per-event dispatch is volatile within the range; recovery retires the entire
ambiguous range. A retry cannot assign the same event to a fresh range/generation.
Each range grants dispatch to one named driver incarnation; replacement never
reuses that grant or assigns the same range to another live driver. A delayed old
driver can finish its already authorized dispatch, but no replacement repeats it.
Within a live range, claim-before-dispatch is atomic: one volatile claim creates
one pending attempt, which may arrive after driver death.
Retired events are not replayed to that processor. This
accepts missed early effects and amortizes registration; it adds no synchronous
per-event persistence before early execution. Stable event identity survives retries.
Reusing a retired generation is a required defect; this is not exactly-once execution.

**Delivery and enrollment.** Identity binds lineage, derivation/call identity,
logical contribution, input cut and interpretation; route, worker and representation
are not new contributions. Application-supplied canonical decoding/equivalence lets
raw and transformed carriers represent one logical result. Required recipients and
checkers belong to an agreed generation. Joining records a starting cut and tail
obligation before becoming required; it cannot rewrite historical completion. A
cancel releases only its authorized scoped obligation. Relays transfer custody
against durable scoped evidence. The raw journal may repeat an identical command;
the deterministic fold enforces idempotent effects, not a set-valued network.

**Restore and external effects.** Normal recovery reconstructs chosen history and
obligations. PITR requires an explicit operator fencing primitive to isolate the old
deployment before new-lineage writes. Its effect is a named external deployment
contract, not an unexplained `SafeToPromote` guard. Without the fence, restoration
permits inspection/read-only access. The selected cut closes cross-shard transaction
and dependency relationships, rather than taking min/max of shard counters. External
intent identities survive restoration. A deduplicating sink can receive a retry;
an ambiguous non-deduplicating sink retains an unknown result instead of automatic
re-execution. Both policies are checked.

**SOS and resumption.** The candidate persistent local pause stops creating new
admission, position and decision commands; submitted operations and valid old chosen
evidence may finish. Export uses the reserved service path and retains physical
lifetimes. Complementary incomplete exports preserve provenance/uncertainty and
cannot activate authority. A successful probe/export does not reopen admission;
reconciliation under valid authority does. This stronger pause policy is explicit;
the brief does not settle completion of already-admitted work during SOS.

**Bootstrap without admission.** Recovering nodes can authenticate and fetch
immutable records/material, serve retained evidence, collect promises/accepted
prefixes and persist a bounded transfer while normal admission is closed. The
known genesis/last configuration and durable owner root identify the first
namespace; validated records identify further records. No harness supplies the
missing inventory. Successor custody can be durable before successor authority
is active. Activation separately checks old terminal evidence, recovered protocol
state. Per-scope serving separately requires that scope's control/material closure;
an unavailable cold object must not prevent unrelated scopes or recovered witnesses
from progressing. Redundancy restoration is a third milestone. Required bootstrap
buffers, verification/decoding workspace,
metadata, I/O and worker capacity are counted by CapacityProgress. Under the named
fault budget, survival of required material is an invariant established by the
provider, not an extra availability premise that assumes away loss.

**Root protocol.** A holder durably reserves a complete immutable recipe before
replying with a hold bound to the root generation, physical-copy ID and storage
incarnation. Holder recovery rebuilds holds/delete intents before permitting GC.
Pending physical deletion excludes acquiring that old copy; refetch uses a fresh
immutable physical-copy ID, or waits for old deletion retirement. Ignoring an old
callback does not prevent its physical deletion from happening. The
owner journals registration only after the required holds, then issues a grant.
Logical release/transfer is another agreed owner record; holders retire only the
named hold, and physical users still delay deletion. A foreign recipe dependency
needs its own hold before publishing the descriptor. Checkpoints are immutable
versioned caches of an exact chosen prefix; publication of the cache pointer is
an agreed record. A late old write cannot overwrite that pointer or manufacture
an unjournaled root. Recovery replays actual chosen registration/release records,
including registrations persisted before their replies. Unknown hold outcomes
remain held until the owner resolves them, including abandoned pre-registration
attempts. Compaction retains a certified base plus accepted/chosen suffix and
protocol state; it never discards a possibly chosen tail at a merely learned
frontier. Source retirement waits for the destination's accepted complete closure.

A read waiting on an unresolved predecessor needs a **coverage root**, not just
a lease for an already known version: `(scope, c)` preserves its current fallback,
pending control/tail dependencies and future qualifying results. Read-bound and
coverage registration are ordered together before waiting. Once visibility is
resolved, exact recipe acquisition/materialization may proceed; the coverage
obligation cannot disappear in between. A pre-existing root transfers both current
bytes and future suffix obligations. This prevents no-effect resolution revealing
a fallback that GC already removed. Recipe graphs are well-founded; a cycle of
references with no retained base cannot count as reconstructible.

An attempt's destinations and obligation must be discoverable from agreed owner
state before any hold request. The explicit path journals
`Begin(g, holders, recipe-or-coverage)`. A prepared acquisition may instead be
derived from an existing chosen operation and its retained prefix when they fully
define that obligation under the same owner. Normal folding and replay use the
same derivation. The modeled shortcut covers a complete prepared acquisition;
later recipe or holder choices still need recoverable ownership. This removes
the separate Begin append, not the durable success/abandonment distinction.
`Register(g)` and `Abort(g)` are mutually exclusive terminal registration choices;
subsequent registered-root release has its own ordered record. Recovery reconstructs
the attempt from the actual agreed history and repeats correlated requests to
surviving holders, then completes registration or records justified abandonment.
Owner reset erases its volatile replies without resetting surviving peers. Holders
regenerate replies from durable state; idempotence suppresses duplicate effects,
not valid repeated answers. Recorded successful acquisition permits serving any
surviving complete valid copy without recollecting every original acknowledgement.
Holders persist terminal abort/release tombstones and reject crossed old requests.
Late submitted hold/cache writes cannot supersede a later terminal journal record.
A tombstone can compact into a contiguous terminal-generation floor, never a maximum
that skips an unresolved attempt. C4/R8 include lost hold reply, owner replacement,
release-before-old-hold, delayed Register after Abort and actual orphan retirement
under the counted completion-capacity policy.

**Outbox ownership.** The producing shard journals the logical outbox obligation,
required recipients/coverage, enrollment generation and completion predicate.
Receivers journal idempotent application/custody receipts; the owner journals
completion or authorized cancellation. An output ID is independently derived by
each consumer from logical lineage, source shard, agreed input identity and
canonical emission slot. Recipient obligation identity is separate. Fallback may
change the driver, not the logical effect owner or dedup history. Ring replies
can return through another ingress without losing this identity.

**Health policy.** One isolated slow response triggers a bounded probe and no
handoff/pause. Accumulating explicit evidence may trigger material copying while
the suspect store remains readable, then stronger protection or SOS. Alarm
duplication cannot multiply unbounded investigation or erase obligations. This
small qualitative policy checks the stated response classes; selecting thresholds,
warning times and cost-optimal evacuation remains empirical networking work.

## Coverage map

Each row names a requirement family. The names in the final column are conceptual
families, not proposed runtime modules. The reports for [journal](JOURNAL.md),
[execution](EXECUTION.md), [material](MATERIAL.md), [delivery](DELIVERY.md) and
[runtime](RUNTIME.md) map these IDs to the actual catalog entries and properties.

| ID | Required behavior | Owning model / composition |
| --- | --- | --- |
| J1 | One chosen prefix despite competing leaders, delayed promises/accepts, crash and recovery | JournalAuthority |
| J2 | Stable prepared leader, any-witness ingress, duplicate/frontier idempotence; a slow third voter is not a quorum barrier | JournalAuthority + AdmissionMaterial |
| J3 | Durable acceptance before propagation/ack; historical evidence remains interpretable | JournalAuthority + DeliveryDataflow |
| J4 | Exact suffix, terminal old closure and one named successor; old recovery cannot reopen | JournalAuthority |
| J5 | New authority cannot serve before its ordering history and required material are recovered | JournalAuthority + RecoveryMaterial |
| J6 | Permanent driver loss preserves c, outcome, decision and unresolved participants | JournalAuthority contract + TransactionMachine |
| J7 | Versioned scope ownership preserves old plans/bounds without mixed-map reservation cycles | JournalAuthority + TransactionMachine + RecoveryMaterial |
| J8 | Compacted log bases retain protocol state and possibly chosen suffixes; voter catch-up requires actual history | JournalAuthority + RecoveryMaterial |
| A1 | Producer LSN admission is a contiguous, monotone, idempotent range, including holes and duplicates | AdmissionMaterial |
| A2 | Eligibility requires independent durable payload and interpretation evidence, not send/intent/duplicate relay evidence | AdmissionMaterial |
| A3 | One declared domain loss and repaired/repeated loss have different obligations; no hidden renewal | AdmissionMaterial + RecoveryMaterial |
| A4 | Unknown durable tails remain discoverable after producer loss; C1/C2 do not inherit L-stream ordering | AdmissionMaterial + TransactionMachine |
| A5 | Early pre-persistence dispatch is at most once per stable event/processor despite ambiguous generation recovery | AdmissionMaterial + DeliveryDataflow |
| T1 | Effect reservation and read invalidation are separate opaque relations | TransactionMachine |
| T2 | Atomic local group grants, common shard order, protection after older conflicts drain, canonical per-record grant closure and local release after final durable c | TransactionMachine |
| T3 | Generated unique immutable positions respect relevant bounds, conflicts, known causality and requested freshness | TransactionMachine |
| T4 | Bounds register before reading/waiting; late discovered reads retain c; own tentative effects are included | TransactionMachine |
| T5 | Actual observations and outputs agree with independent serial evaluation for multiple overlapping writers and readers | TransactionMachine |
| T6 | Partial and late older installation cannot create torn observations, erase newer versions or confuse physical/logical order | TransactionMachine |
| T7 | Abort/no-effect resolves only its own outputs; absence of a new version exposes earlier unresolved effects correctly | TransactionMachine |
| T8 | Complete replacement skips only dependencies covered at the requested position, after replacement commitment | TransactionMachine |
| T9 | Undeclared effects fail without envelope expansion; scope/owner changes preserve outstanding plans | TransactionMachine + JournalAuthority |
| T10 | Pre-position cancellation fences delayed requests; post-position abort and driver replacement finish every obligation | TransactionMachine |
| T11 | Finite predecessors and fair service give progress; unrelated enabled work can proceed across a WAN wait | TransactionMachine + CapacityProgress |
| T12 | Late sources return exact-c bytes or agreed failure; retained late inputs can actually succeed | TransactionMachine + RecoveryMaterial |
| E1 | Same agreed input, independently scheduled replicas: equal complete state, ordering metadata, outbox and continuations | EpochExecution |
| E2 | Noncommuting observations/decisions follow derived reference order; commuting deltas alone are insufficient | EpochExecution |
| E3 | Epoch closure preserves pending continuations; later input resumes exactly the same transaction/cut | EpochExecution + TransactionMachine |
| E4 | Independent publication and metadata progress while remote execution/verification waits; speculative work cannot publish early | EpochExecution |
| X1 | Journaled source/code/profile/policy and captured external facts identify reproducible execution | TransactionMachine + RecoveryMaterial |
| X2 | Full interactions, effects and completion status agree across every required checker; mismatch aborts only its transaction | TransactionMachine + EpochExecution |
| X3 | Checked read-only and updating work are gated before publication; private queries do not mutate ordering metadata | TransactionMachine |
| X4 | Approved/WASM execution and synchronous native checks obey distinct policy; approval is not data/I/O authority | TransactionMachine + ViewsRuntime |
| X5 | Async disagreement records an integrity incident without changing an already committed decision | TransactionMachine + RecoveryMaterial |
| X6 | Normal transactional external effects require committed intents; replay/restore does not repeat them; A5 is a distinct early channel | DeliveryDataflow + RecoveryMaterial |
| X7 | Extension/application/extension calls share the parent transaction and never await its publication | EpochExecution + TransactionMachine |
| D1 | Stable logical identity survives duplicates, route changes, local IPC and remote transport | DeliveryDataflow |
| D2 | Completion/cancellation, transport receipt and custody are distinct; no last copy released while an obligation remains | DeliveryDataflow + RecoveryMaterial |
| D3 | Independent origins deriving the same message cannot duplicate effects or choose different authoritative contents | DeliveryDataflow + EpochExecution |
| D4 | Split/replica coverage counts logical contributions, not packets; dynamic children/feedback prevent premature completion | DeliveryDataflow |
| D5 | Shared results remain until all required users finish; optional cancellation does not cancel another user's obligation | DeliveryDataflow + RecoveryMaterial |
| D6 | Source checkpoint and output publication are coupled; replacement/reassignment preserves contribution identity and cut | DeliveryDataflow + TransactionMachine |
| D7 | Enrollment establishes cut/tail coverage; alternative representations preserve canonical contribution identity | DeliveryDataflow + RecoveryMaterial + EpochExecution |
| R1 | Every live reader/replay/head/checker/delivery root has a complete reconstruction path, including code/dictionaries | RecoveryMaterial |
| R2 | Root registration precedes granting a view; crash-before-receipt and late physical completion remain recoverable | RecoveryMaterial + ViewsRuntime |
| R3 | Checkpoint durability precedes publication; replay cursors include accepted state and never outrun recoverable effects | RecoveryMaterial |
| R4 | Distributed root/custody transfer preserves material until successor acceptance; GC uses authority evidence | RecoveryMaterial + DeliveryDataflow |
| R5 | Blob/consumer material loss is independent of healthy witnesses; peer repair restores actual dependencies | AdmissionMaterial + RecoveryMaterial |
| R6 | Normal reopening recovers ordering constraints; PITR records abandonment and fences old lineage before new writes | RecoveryMaterial + JournalAuthority |
| R7 | SOS pauses local admission, exports partial useful evidence, and does not manufacture quorum authority | RecoveryMaterial |
| R8 | Bootstrap discovery finds unknown tails and late roots without a harness-supplied inventory | RecoveryMaterial + AdmissionMaterial |
| V1 | Every memory access binds object, version, rights and lifetime; lazy and prepared materialization agree | ViewsRuntime + RecoveryMaterial |
| V2 | No-COW reuse excludes every physical user, admits late old access through reconstruction, survives abort/reset | ViewsRuntime |
| V3 | Independent writers on one physical page cannot overwrite/undo each other's logical changes | ViewsRuntime + TransactionMachine |
| V4 | Read-only projection exposes only permitted bytes; async host operations enforce extents independently of CPU rights | ViewsRuntime |
| V5 | Queued, active and undrained backend users outlive cancellation/owner death; old callbacks cannot change a new incarnation | ViewsRuntime |
| V6 | Logical identity/meaning is independent of address, allocation hints, layout and backend platform | ViewsRuntime + EpochExecution |
| V7 | Stale bindings keep their dependencies or rebind before use; published representation changes preserve interpretation | ViewsRuntime + RecoveryMaterial |
| P1 | Fault resolution has runnable worker, resident metadata and needed memory/I/O despite parked ordinary workers | CapacityProgress |
| P2 | No lock held by a parked worker is needed by its resolver; no commit/recovery metadata capacity cycle | CapacityProgress |
| P3 | Resources remain charged until actual last use; refusal/cancellation does not forge replicated outcomes | CapacityProgress + ViewsRuntime + TransactionMachine |
| P4 | Finite admitted obligations finish or agree failure under explicit service/fault assumptions | CapacityProgress + protocol progress configs |
| P5 | Isolated alarms trigger bounded probes only; catch-up/repair/SOS use counted capacity without blocking the healthy admission pair | CapacityProgress + JournalAuthority + RecoveryMaterial |

## Shared model structure

Kernels use explicit state records; wrappers own variables and choose enabled
transitions. A transition carries `[tag, next, emissions]`; commands identify
`[owner, id, kind, body]`, and ordered emissions identify their source, destination
and evidence. These are model representations, not a proposed implementation ABI.
Actual records and bytes produced by one component feed the next. Observer state
can check global truth but cannot authorize a protocol action.

Shared identities distinguish lineage, transaction/context, command, scope-map
revision, cut/position, call/emission slot, retention/enrollment generation,
recipe/interpretation, physical copy, storage incarnation, view and submitting
incarnation. Journal inputs can repeat an identical command; idempotence belongs
to the fold. Finite authored workloads and any retry reduction must preserve the
particular duplicate, delayed-receipt and recovery interactions being checked.

The cut interface separates `RetainCut(context,c,scope,map)` and
`CutProtected(root,evidence)` from `ExactRecipeReady` and `ViewGrant`. Protection
preserves fallback and pending/future qualifying results before actual bytes are
available. Read bounds and coverage registration share the source owner's order.
Independent implementations of similar-looking state flags do not establish this
connection; composed models reuse the actual providers or a checked mapping.

Each family retains distinguishing obligations beyond simple state invariants:

- **JournalAuthority:** durable promises and accepted prefixes, highest accepted
  prefix recovery before extension, and evidence of persistence before replies.
  Messages bind configuration, ballot and exact prefix; choosing and learning
  are separate. Successors persist certified state before voting, and erased
  voters cannot reuse old storage incarnations. Check prefix/successor agreement
  independently of leader guards. Progress needs a stable eligible leader,
  communicating quorum and recovery material, not delivery from every witness.
- **TransactionMachine:** actual queues, generated positions, bounds, versions,
  tentative effects, decisions and installation feed an independent program
  evaluator. Local release, all-fix acknowledgement, checks, commitment and
  installation remain separate. The pending/no-effect, RMW, replacement and reader
  chain must preserve each real dependency through late installation. Valid work
  with matching checks and no authorized failure must commit; an always-abort
  implementation is a defect. Claimed failures need actual program, envelope,
  verification, retention, resource or cancellation evidence.
- **EpochExecution:** independently scheduled consumers use the transaction
  semantics and compare complete logical state and outputs. Dependencies come
  from declared operations and reference order, not eventual matching results.
  Closure uses logical readiness under agreed input. A missing remote fact can
  leave a continuation; local CPU, disk or page readiness cannot choose carryover.
  In-epoch work reaches its normal form before closure, while deferred work resumes
  through later agreed input. Dynamic calls use captured inputs and stable parent
  identity. Independent work and alternative legal schedules must be reachable;
  final-value agreement and one imposed physical schedule are insufficient.
- **AdmissionMaterial:** independent holder incarnations and separate payload and
  interpretation persistence supply actual receipts for contiguous frontiers.
  Historical eligibility, present reconstructibility, the failure allowance and
  renewed protection are distinct. Actors never consult global survivor counts.
  Histories include noncoexistent receipts, holes, correlated loss, repair and
  renewed incidents; controls reject duplicate-domain evidence, early replies,
  missing interpretation and stale renewal. Unknown tails remain discoverable.
  Early-dispatch observations count immutable event/processor identity, excluding
  generation or driver, so reassigning an old event cannot evade at-most-once.
- **RecoveryMaterial:** reader, replay, head, checker and delivery roots refer to
  actual base, suffix, result, decoder and executable material through transfer,
  checkpointing, restart and deletion. Conversion creates another recipe for the
  same version. Cold-owner loss removes its volatile knowledge while survivors
  retain theirs. Progress reacquires actual evidence and a surviving recipe.
  Controls expose missing dependencies, premature publication/collection, stale
  caches, forgotten pending-source duties and unfenced restoration.
- **DeliveryDataflow:** stable contributions and recipient obligations survive
  duplicate origins, retries, reassignment and representation changes. Two-part
  aggregation, dynamic children/feedback, shared results and checkpoint-plus-output
  cases check application-defined completion. An empty network queue, tentative
  result or transport receipt cannot discharge that completion. Cancelling one
  subscriber cannot release another's retention or rewrite enrolled coverage.
- **ViewsRuntime:** symbolic bytes distinguish versions and independent items on
  one page. Exercise late readers/checkers against no-COW reuse, reconstruction,
  two-writer installation/undo, stale bindings and callbacks after cancellation
  or restart. Check actual bytes and permitted extents, including asynchronous
  operations, independently of CPU mapping rights. Pin counts alone are not
  evidence of correct access or safe reclamation.
- **CapacityProgress:** explicit worker, memory, metadata and backend resources
  connect admission to resolution, recovery and retirement. Ordinary work can
  occupy capacity while fault/completion service remains runnable. Controls
  exhaust that path or retain a latch its resolver needs. Unrelated activity
  stays enabled in liveness controls so contradictory fairness cannot conceal
  the target's stall. SOS progress additionally needs valid reconciliation/unpause;
  capacity cannot enable an action the pause policy forbids. Arbitrary application
  demand is outside these finite completion claims.

## Composition evidence

The interface graph has specific obligations. Each arrow requires a shared
definition and checked mapping, or an executable composed configuration:

- JournalAuthority → all replicated commands: append-only chosen records, durable
  identity and closure; distinct receipt/recovery visibility and conditional service.
- TransactionMachine ↔ EpochExecution: identical command semantics; all permitted
  physical schedules preserve the transaction state, not only values at epoch end.
- TransactionMachine ↔ RecoveryMaterial ↔ ViewsRuntime: a captured cut grants
  exact bytes through overwrite/GC/reset; tentative and verified effects publish
  only through the same decision, while physical borrows can remain outstanding.
- AdmissionMaterial ↔ JournalAuthority ↔ RecoveryMaterial: eligibility evidence
  admits only complete ranges; authority transfer carries the exact obligations;
  material loss may prevent serving even with an intact quorum.
- DeliveryDataflow ↔ TransactionMachine/RecoveryMaterial: contribution identity,
  committed output/checkpoint and retained custody survive retry/reassignment.
- EpochExecution ↔ DeliveryDataflow: independently emitted logical IDs/content
  become actual routed obligations and receiver effects; no harness supplies the
  equal outputs whose agreement is being checked.
- CapacityProgress ↔ all admitted obligations: the resources consumed by recovery
  and completion are represented, with no progress assumption that requires the
  very capacity being checked.

The assessment distinguishes machine-checked correspondence, executable joined
histories, reviewed abstraction arguments and implementation obligations. These
connections do not constitute an automatic assume-guarantee or arbitrary-size
composition proof. A missing connection remains explicit in RESULTS.

## Required joined histories

These histories define required interactions. A cross-reference alone does not
exercise them: the preceding transition must create the records/bytes consumed
next, through shared operators or an explicit checked refinement.

| Case | Joined execution and independent observation |
| --- | --- |
| C1: owner and shard replacement | A transaction releases one participant, loses its driver, and a shard changes authority while an earlier completed read bound and delayed installation survive. Recover one c/decision and preserve actual serial observations, including an accepted-but-unacknowledged owner record. |
| C2: scheduled checked writer | Two consumers independently fold a checked writer, overlapping reader and unrelated publisher. Private queries retain a cut; metadata advances; later agreed matching/mismatching evidence resumes the transaction without an epoch-wide wait. |
| C3: derived message and route replacement | Two origins independently derive one message. Raw and transformed branches reach recipients; one restarts and the route changes before final ACK. Check canonical content, required recipients, deduplication and remaining retention debt. |
| C4: joining root during transfer | A recipient enrolls at a cut during snapshot/tail transfer. Its root persistence precedes a lost receipt; late submitted cache writes outlive its process. Old source/authority retire only after transferring the obligation. Discover and reconstruct actual exact-cut bytes. |
| C5: checked bytes and physical reuse | An old-cut checker overlaps source overwrite, no-COW selection, delayed backend send, representation movement and reclamation. Context, permitted bytes, effect/publication and physical lifetime refer to the same captured input. |
| C6: effect and lost reply | A committed intent is applied, its reply is lost, then delivery falls back or a lineage is restored. Preserve the stable effect ID; deduplicating and ambiguous sinks produce their distinct correct outcomes. |
| C7: resolution under pressure | Repair and a long extension consume ordinary capacity while fault resolution, outcome/decision and delivery retirement need service. Subscriber cancellation and false alarms cannot erase debt or consume the reserved path. Fair unrelated activity is not target completion. |
| C8: scope migration and old plan | An old-map transaction holds one shard and awaits another while a new plan touches a moved scope. Enrollment fencing prevents a mixed-map cycle; transferred reader floors/retention preserve old observations. |
| C9: recovery cut | Two shard checkpoints straddle partial installation. Normal recovery resolves the decision; PITR selects a transaction-closed cut and fences the old lineage before writes. Readability, write authority and restored redundancy are separate observations. |

For each service substitution, record related state/events, input preconditions,
permitted stuttering, crash/in-flight behavior and preservation of service/fairness
conditions. A mapping of final states does not establish trace compatibility;
safety refinement alone does not transfer liveness. Joined cases must retain the
three-way authority/material/capacity dependencies, not assume one service ready.

## Finite dimensions

These dimensions distinguish the required mechanisms. The catalog and family
reports specify the actual bounds, reductions and authored schedules. Factoring
irrelevant interleavings needs a justification preserving the named interaction;
a stopped graph supplies no completed evidence.

| Family | Distinguishing instance | Larger required instance |
| --- | --- | --- |
| Journal | 3 voters, 2 leaders/ballots, 2 entries | 3 ballots; overlapping and disjoint successor configurations separately; two successive transfers; compacted base plus suffix |
| Transactions | 3 transactions, 2 shards, overlapping effects and actual reads | 4-transaction pending/RMW/replacement/reader case; 3-shard acquisition and late source |
| Epochs/extensions | 2 consumers, 2 epochs, checked writer + independent work, 2 checkers | 3 epochs, dynamic nested call/source; observations distinguishing same-final-delta operations |
| Admission/repair | 2 streams × 2 LSNs, 3 domains, one loss | 3 LSNs; fresh protection generation and second incident; producer loss with unknown tail; two early-dispatch generations |
| Retention/recovery | 2 root owners/holders, 3 versions, two recipes | root joining during transfer, checkpoint plus pending obligation, two restoration lineages |
| Delivery/dataflow | 2 contributions, 2 recipients, duplicate origin, changed route | dynamic child/feedback, third joining recipient, raw/transformed carriers, both sink policies |
| Views/runtime | 2 versions, 2 items/page, reader + writer + backend user | 2 writers + late reader/checker, two incarnations, queued/active/undrained users and two-page materialization |
| Capacity | 1 ordinary worker plus resolver/completion service, finite bytes and metadata | 2 admitted obligations plus repair/shared subscriber; cyclic demand across two resource kinds |

Every family needs safety, applicable conditional-progress checks, intended
defects and successful-path witnesses. C1–C9 map to exact configurations and
properties in the family reports. Enlargements should add a meaningful interaction,
such as another overlapping writer, independent domain or outstanding backend
user; a larger checker count cannot substitute for missing writer overlap.

Joined histories allocate enough distinct records for context, position, outcome,
decision and transfer/cancellation. A compacted representation cannot merge the
persistence and receipt boundaries those histories are meant to exercise. Authored
service schedules remain restrictions on the explored composition even when local
provider families explore broader interleavings.

## Maintaining models and evidence

Review the requirement/property mapping, actor knowledge, fault boundaries,
fairness and reachable successful/adverse paths when a mechanism or abstraction
changes. A deliberate defect should fail its intended property, not an unrelated
error. A witness establishes existence, and a completed safety graph does not by
itself establish conditional progress. Every claimed abort must have independent
evidence; matching a fabricated result against itself proves nothing.

Measure tractability on complete small cases before spending resources on larger
ones. Retain generated/distinct/queued states, depth, memory and temporal-check
cost; a growing queue does not estimate completion time. Stops are incomplete.
Do not prune inconvenient histories with arbitrary queue, retry or clock limits.
Retry and representation reductions need explicit identity/recovery and enabled-step
arguments, including preservation of fairness. Temporal checks do not use symmetry;
terminal stuttering needs independently established discharged obligations.

Use the pinned runner and existing checkpoint tooling described in
[README](README.md). Exact parsed dependencies, configuration, checker and tool
identity determine whether a receipt applies. Changing an imported kernel requires
replacement evidence even if a particular case seems unlikely to visit the changed
branch; an unrelated edit does not. Preserve raw outcomes, sources and useful
counterexamples through the existing evidence collector and artifact tools.
Dated retained selections stay immutable; current status belongs in RESULTS.

A source-matched selection records what was checked, not an implementation proof
or a clean benchmark of full-suite wall time. Physical performance and native
refinement remain separate investigations. The earlier campaign's sizing and
review history can be recovered from its retained evidence and Git history.
