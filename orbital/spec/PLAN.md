# Orbital verification plan

This is a ground-up restart, dated 2026-09-27. The earlier investigation is now
a [spike](../../workbench/spikes/orbital-formal-first-pass/README.md). Its tools,
counterexamples and sizing observations are useful inputs; its passing cases
do not discharge the obligations below. Independent ground-up audits and review
of the integrated plan preceded replacement model implementation. The review
covered transactions/epochs/dataflow, authority/admission/recovery and physical
views/capacity, with Orbital ASSISTANT separately challenging the whole composition.

## Destination

The deliverable is a maintained, executable account of the correctness-critical
design in [BRIEF](../BRIEF.md) and [PHYSICAL](../PHYSICAL.md): its observable
guarantees, the mechanisms that establish them, the assumptions those mechanisms
need, and the ways the mechanisms compose. It is not a collection of independently
interesting checks or a promise that more cases will eventually cover the design.

For every requirement below, completion means an identified model and property,
a completed distinguishing configuration, an independent review of its abstraction
and assumptions, and a retained result. A relevant deliberate defect must be caught
by the intended property; a reachability case must show the promised path actually
occurs. Mechanisms that are still unspecified must receive an explicit candidate
design before their models can establish anything about them. An unresolved
internal dependency keeps the corresponding requirement incomplete.

TLC establishes properties of the complete reachable graphs of the stated finite
instances. It does not prove arbitrary-size correctness or that future C++ code
implements these specifications. The final report will state that qualification
alongside the checked guarantees. It will not substitute that general caveat for
identifying a missing mechanism or composition check.

The destination has six observable guarantees:

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

These are the acceptance criteria for the whole exercise. No model count, case
count or elapsed-time target substitutes for them.

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

## Design choices the models must make explicit

The briefs intentionally leave some protocol choices open. The following are
candidate bindings for this verification, subject to review and counterexamples;
they do not edit either brief by implication.

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
  only an agreed record does that. The spike's drain candidate is not imported
  as an unimplemented service assumption.
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

The bindings also settle these less visible choices:

**Reservation policy revision.** The post-campaign design comparison selects
original enqueue order with protection after older conflicts release. Waiting or
held requests retain their local age until durable fixation or cancellation.
A waiter excludes younger conflicts only after its older live conflicts are gone;
actual holders always exclude conflicts. Every agreed local record performs the
canonical grant closure before exposing its state and outputs. Replay preserves
that interpretation, live order and full reply evidence. Continuing-arrival
progress, the admitted-younger-WAN boundary and the actual Tx/journal binding are
additional obligations; the previous ordered/eligible projection remains a
counterfactual comparison. No in-place change of old journal interpretation is
assumed.

**Scope changes.** Plans bind a versioned scope/authority map before acquiring
any reservations. Migration closes enrollment of new plans touching the moved
scopes, drains enrolled old plans, transfers versions, reader floors and retention
obligations, then opens the new map. It never retargets a partially acquired plan.
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

Every attempt first journals `Begin(g, holders, recipe-or-coverage)` under the
owner, making its destinations and obligation discoverable before any hold request.
`Register(g)` and `Abort(g)` are mutually exclusive terminal registration choices;
subsequent registered-root release has its own ordered record. Recovery reads Begin
and actual replies, then completes registration or records justified abandonment.
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

Each row names a requirement family, not one assertion that can restate its own
guard. The model descriptions below specify distinguishing interactions. The
final case catalog will map these identifiers to exact configurations/properties.

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
| T2 | Atomic local group grants, common shard order, no overtaking, local release after final durable c | TransactionMachine |
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

## Model structure and integration

Kernels are variable-free modules over explicit state records. A transition is
`[tag, next, emissions]`; wrapper specifications own variables and choose enabled
transitions. Shared commands are `[owner, id, kind, body]`; emissions are ordered
sequences of `[id, src, dst, kind, body]` events. Body records carry the context/evidence needed by their
consumer, not an observer's conclusion. Journal input occurrences are a finite
authored workload including explicit duplicate submissions; retries preserve their
identity and remain deliverable. There is no arbitrary maximum log length that
silently disables an otherwise valid retry.

The shared vocabulary includes lineage, stable transaction/context, source/command
identity, scope-map revision, cut/position, parent/call/emission slot, root and
enrollment generation, recipe/interpretation, physical-copy/storage incarnation,
view/resource slot and submitting incarnation. The cut interface separates
`RetainCut(context,c,scope,map)` → `CutProtected(root,evidence)` from
`ExactRecipeReady`/`ViewGrant`. The first protects fallback and pending/future
qualifying results; the second supplies actual bytes after visibility resolves.
Read-bound and coverage registration share the source owner's agreed order.

The code is divided by these responsibilities, not by unrelated copies of the
protocol. JournalKernel produces the delivered records used by joined models;
the transaction semantic kernel is shared by its own specification and the
independently scheduled epoch executors. ViewsRuntime and CapacityProgress consume
the same root/publication and operation-retirement events used in recovery cases.

The models share identity, durable-record and outcome vocabulary. They must not
copy each other's assumed conclusions into unrelated state flags. Every exported
event records its evidence, and the consumer's next action uses that event.
Observer histories may check truth independently but cannot authorize a protocol
transition. Delayed receipt is distinct from durable completion wherever recovery
or custody depends on the distinction.

**JournalAuthority.** Explicit per-witness durable promises/accepted prefix,
leader recovery replies, proposals, durable accept replies and learned prefix.
Unique ballots belong to one leader/configuration. Promise persistence precedes
the reply. A promise quorum selects the highest accepted ballot's prefix, longest
among equal-ballot reports, before extension. Accept persistence precedes evidence.
Full-prefix messages bind configuration, ballot, absolute length and exact content;
there is no hidden FIFO/session assumption. A follower can learn from its persisted
acceptance plus matching leader evidence. A terminal suffix remains terminal after
recovery; successor voters persist its certified base before voting. An erased
voter cannot reuse its old storage incarnation.
Start with three voters, two competing ballots and two entries; add a terminal
transfer, two candidate successor identities and old/new configurations. Check
prefix agreement and immutable terminal successor independently of leader guards.
Model restart from durable state and messages from earlier incarnations. A
refinement mapping exposes the chosen prefix as the abstract durable log; choosing
and learning are separate. Progress requires an eventual stable eligible leader,
a communicating surviving quorum and available recovery material, not universal
network delivery or a third witness. Controls omit promise durability, highest
accepted-prefix recovery, old closure or successor binding.

**TransactionMachine.** One composed kernel contains actual reservation queues,
generated positions, read bounds, pending outputs, multiple logical versions,
tentative effects, durable owner records, decisions and participant installation.
The minimum acceptance case has two overlapping writers and a reader on two
shards. Programs include reads that affect writes, late source discovery, a
no-change/abort, complete replacement, and a checked updating transaction. Check
actual observations and published effects against a separate serial evaluator;
do not define eligibility by asking the evaluator. Distinguish local release,
all-fix acknowledgement, verification, commit and installation. Include read-only
context selection, pre-c cancellation with crossed messages, post-c owner loss,
late older installation and retained causal/freshness constraints. Its durable
service is the same abstract interface refined by JournalAuthority. Dedicated
seam cases drive an owner change between one participant's fix and the next, and
between durable decision and receipt. Reservation progress and read progress
must be checked in this kernel, not only in separate noninteracting fixtures.
One required four-transaction configuration joins an earlier pending/no-effect
writer, an RMW, a later complete replacement and a bound-registering reader.
The replacement can serve the reader while the RMW still owes its real predecessor;
late installation cannot clobber the replacement.
Independent outcome semantics additionally require valid work with all matching
required checks, retained inputs and no authorized failure/cancellation to commit.
An abort must cite actual modeled program/envelope/check/retention/resource/cancel
evidence. An always-abort mutant must fail even if other successful witnesses exist.

**EpochExecution.** Two replicas consume identical agreed epochs with independent
physical readiness/schedules. The state transition semantics are shared with the
transaction kernel; a second simplified transaction protocol would not establish
composition. A legal dependency relation is derived from declared operations and
reference order before execution, not from comparing the eventual results. Check
the full fold projection, including observations, bounds, resolutions, generated
messages and persisted continuations. Include overlapping writers and conditional
reads, a pending checked transaction across two epochs, speculative preparation,
and independent publication. Controls remove a noncommuting dependency, compare
only final deltas, close an epoch by losing its continuation, or publish speculative
effects. Branch-specific witnesses establish actual alternative schedules and
useful progress; deterministic scheduling by fiat is insufficient.
Dynamic branches derive calls/dependencies from captured input and parent/call
identity, so the complete DAG need not exist at admission. A third epoch exercises
pending → resumed → published work. The reference interpreter skips blocked
continuations while processing other enabled work; it is not a shard-wide barrier.

Closure uses **logical readiness under agreed input**, never local CPU, page or
network readiness. Plans select in-epoch work or deterministic stopping points.
In-epoch work must reach its logical normal form before closure even if one replica
is waiting for disk. Deferred work emits a stable continuation and resumes through
later agreed input. A remote fact absent from that input can cause carryover;
physical slowness alone cannot. The suite must detect a mutant that chooses
carryover from local readiness. Shared semantic operators expose node identity,
dependencies, local evidence and authoritative application; capture/compute can
stutter. The abstract transaction frontier also admits the declared independent
steps, rather than imposing strict physical log order and pretending reordered
execution refines it. A separate serial application oracle still checks meaning.

**AdmissionMaterial.** Explicit producer stream packages, independent holder
incarnations, separate payload/decoder persistence, receipts, journaled contiguous
frontiers, loss and repair. At least two LSNs and three failure domains expose a
hole and correlated material destruction; a repair case adds a replacement holder
and second incident. Actors see evidence messages, not global survivor counts.
Checks distinguish historical eligibility, present reconstructibility, the stated
failure allowance and renewed protection. The historical-noncoexistence trace is
retained as a boundary requirement, not made unreachable by an omniscient guard.
Controls count relay copies twice, admit across a hole, acknowledge before material
durability, omit interpretation dependencies, or renew protection on stale evidence.
At least two producer streams distinguish per-stream contiguity from accidental
global ordering. A discoverable tail and early-dispatch generation cover A4/A5.

**RecoveryMaterial.** Durable logical roots reference actual symbolic base,
suffix, accepted-result, decoder and executable tokens. Local caches, live physical
borrows and recovery discovery are separate. Check reader/replay/head/checker and
delivery obligations together through checkpoint publication, root transfer,
registry restart, late submitted writes and GC. An immutable representation
conversion supplies another complete recipe, not a new logical version. Include
authority/material separation, partial SOS exports, normal restoration and explicit
fenced PITR. A chosen prefix alone does not authorize serving missing material.
Controls drop dependencies, publish before persistence, advance replay/checkpoint
past recoverable effects, collect during transfer, publish a stale root cache, or reopen
an unfenced lineage. Progress conditions must identify eventual access to an
actual surviving recipe, not a magical restore transition.

**DeliveryDataflow.** Logical obligations and complete contribution identities
outlive particular messages/routes/workers. Explicit local/remote receipt,
durable custody, processing/commit, completion and cancellation messages permit
duplicate delivery and rerouting while work is in flight. Application fixtures
cover a two-part aggregate, dynamic discovery with one child, a shared result,
and a checkpoint plus output transaction. A tentative result may travel but cannot
discharge publication. Check exactly the application-declared coverage and effects;
the protocol cannot infer closure from an empty transport queue. Two origins can
produce the same logical contribution without counting twice. Controls release
on transport receipt, identify work by route/worker, count duplicate partitions,
close before child registration, or cancel another subscriber's root.

**ViewsRuntime.** Concrete symbolic bytes distinguish versions and disjoint
logical items sharing a physical page. Track view mappings/rights, tentative
representations, submitted backend users, physical completion, callback delivery,
incarnations and accounting. Race no-COW selection against a late old view and
checker; materialize from retained tokens; install or undo two writers sharing
a page; cancel/restart while hashing/sending/persisting; replace a binding.
Check actual read/observed bytes and permitted extents, not only pin counters.
Model validated async operations separately from CPU read-only mappings. Controls
allow new access after exclusivity was tested, reuse after cancellation, accept an
old callback, restore a whole stale page, or expose an adjacent unauthorized item.

**CapacityProgress.** Bounded workers, ordinary bytes, completion/fault-service
bytes, metadata and backend capacity form an explicit resource graph. Put one
worker at a first-touch wait while admitted work owns ordinary capacity; resolution
must still run. Couple an output/decision obligation to the capacity needed to
resolve it, including cancellation and owner restart. Check accounting and temporal
completion/refusal under concrete fair service. The positive policy reserves an
escape path; defective variants let ordinary admission consume it or hold a latch
needed by the resolver. Independently runnable unrelated work remains present in
temporal controls so excluding a bad behavior through contradictory fairness
cannot look like success. Unbounded application demand remains an admission or
application policy question, not a claimed arbitrary-resource progress theorem.

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

This is not an automatic assume-guarantee proof. The final report must distinguish
machine-checked refinement, executable seam coverage, reviewed abstraction
arguments and remaining implementation obligations. All critical internal arrows
need actual evidence before this plan is complete.

## Tractability and evidence

The following joined histories are mandatory. A cross-reference is not their
implementation: the preceding transition must create the records/bytes consumed
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

### Planned finite families

These dimensions define the intended complete campaign before performance tuning.
Configs may factor irrelevant interleavings only with a stated justification;
each named interaction remains mandatory. A costly case remains unfinished until
a justified representation makes it tractable.

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

Every family has safety, applicable conditional-progress cases, intended defects
and successful-path witnesses. C1–C9 must name exact configs and properties rather
than disappear under a family label.

A5's observer counts applications by stable logical processor and immutable event,
excluding generation/driver/route. A mutant reassigns the same event to a fresh
generation without reusing the retired one; the correct case includes delayed old
dispatch and lost replies. Volatile claim-before-dispatch is atomic within a live
range. SOS transaction progress requires eventual valid reconciliation/unpause;
reserved capacity alone cannot enable an action the pause policy forbids.

Joined configurations allocate enough distinct records for context, c, complete
outcome, decision and transfer/cancellation. The two-entry journal pilot is not a
cap on those histories. A compacted-state refinement may reduce representation,
but cannot merge persistence/receipt boundaries that the seam is meant to test.

Select distinguishing finite families from this coverage map before running them.
Small cases expose each protocol interaction; adjacent larger instances add a
writer, shard, log entry, checker, domain/incarnation, reader or outstanding user
where that dimension tests a different obligation. Do not substitute a large
checker-count graph for overlapping-writer coverage. The final catalog records
exact family bounds and what each enlargement adds.

Run complete local pilots first using the shared pinned TLC/JVM tooling. Capture
generated/distinct/queued states over time, depth, peak RSS, metadata size,
temporal-check cost and total elapsed time. A growing queue is not a finish-time
estimate. Stops are incomplete; no arbitrary queue/clock/retry constraints prune
an inconvenient behavior. Idempotent pending obligations can abstract redundant
retries only with the identity/recovery argument stated. No symmetry for temporal
checks; terminal stuttering requires discharged obligations independently defined.

After each model's distinguishing cases finish, complete adjacent sizes and test
worker scaling on an unchanged graph. Select the full larger suite against the
roughly one-hour aggregate target, including temporal runs and controls. Record
any uncovered combination explicitly rather than cutting it silently to fit.
Bare metal follows measured useful work and a credible full-suite budget; reusable
checkpoint tooling already exists and should not be reimplemented.

Independent review examines the requirement-to-property map, protocol knowledge,
fairness, abstraction/refinement and reachable successful/adverse paths before
reviewing a green result count. Models and their counterexamples receive review
again after implementation. The report accounts for every coverage row, every
internal dependency and every proposed design change. Only then is this stage
complete; architecture sketch and starter implementation are subsequent work.

Primary methodological references: [TLA refinement tutorial](https://lamport.azurewebsites.net/tla/tutorial/session11-1.html),
[model checking and refinement](https://lamport.org/pubs/yuanyu-model-checking.pdf),
and [Paxos safety](https://lamport.azurewebsites.net/pubs/paxos-simple.pdf).
These explain methods and prior algorithms; they do not establish this design.

The consequential review corrections are part of the bindings above: logical epoch
closure; independent outcome justification; immutable early-dispatch ranges;
bootstrap custody before authority; distinct authority/readiness/redundancy;
discoverable terminal hold attempts; pending-cut roots; physical-copy deletion
generations; non-atomic scope enrollment; and sufficient records in composed runs.
Agreement on this plan is not verification of the models that implement it.
