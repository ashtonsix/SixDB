# Orbital's first formal models

2026-09-27. Proposed model boundaries, reviewed before implementation. The
[brief](../../../orbital/BRIEF.md) supplies the intended guarantees; the
[native simulator](../../simulator/README.md) supplies useful executions
and known gaps. Neither is a proof. These models should expose mistakes in the
design, not formalise only the simulator's successful paths.

## Questions and decomposition

Start with bounded, executable application fixtures over opaque logical scopes.
Application meaning belongs to the fixture, not Orbital. Distinguish output
reservation conflicts from read invalidation. Check actual observations against
an independently defined serial history, not just equality between replicas.

| Model | Question and smallest useful case | Important limits |
| --- | --- | --- |
| **Transactions** | Can local release, late source discovery and partial installation preserve one serial position and atomic observations? Two shards, two writers and a reader; a separate three-writer overlapping-envelope case. | Ordered durable shard commands are imported. No consensus or permanent decision-owner replacement is implied. |
| **CheckedReplay** | Can a checked transaction retain its old cut, bind the full interaction and publish only the agreed outcome across restart? Two checkers, two transcript identities with the same final value, a source overwrite, and delayed report/installation. | Exact symbolic transcript equality abstracts hashing. Hardened WASM and approved execution are separate trusted contracts, not unchecked consumers that count as verified. |
| **AdmissionCoverage** | Does a contiguous admitted producer range have the required payload and interpretation evidence, despite holes, duplicate receipts, propagation and loss? Two LSNs, three domains; grow streams separately. | Journal prefix agreement is imported. Historical eligibility and present reconstructibility are distinct. No routing algorithm or election proof. |
| **RetentionClosure** | Can old readers, replay and the current head still reconstruct after checkpoint publication, retirement and restart? Three versions, reader and replay roots, base/tail/decoder dependencies. | A serialized retention authority. Distributed root admission and GC remain undesigned. Explicitly test registration before granting a new view; do not assume an invisible global pin. |
| **BorrowLifetime** | Can cancellation or owner death allow reuse while another physical user still needs the bytes? One reusable slot, two generations, two backend users and two owner incarnations. | Logical reconstruction is a separate obligation. This checks lifetime rules, not UFFD, memory ordering or hardware isolation. |
| **HandoffContract** | Does an authority transition carry its exact suffix, durable closure and named successor, while delayed old evidence remains interpretable? Old/new configurations, two successor names and an outstanding old entry. | Conditional on unique chosen prefixes, recovery of possibly chosen suffixes and terminal no-extension. It cannot establish those assumptions or one-domain failover availability. |

The first five are executable protocol or service models at deliberately narrow
boundaries. Handoff initially checks a contract and its misuse. If an actual
replacement protocol is selected, add a separate whole-prefix consensus model
with durable promises, accepted ballot/sequence and leader recovery. Do not
manufacture a safe-certificate action and call it consensus verification.
Authority activation, readable data and safe reopening are separate: missing
payload, interpretation material or recovered ordering constraints still prevents
service. Dynamic root registration is a proposed serialized protocol beyond the
simulator's fixed-root fixture, not an already implemented guarantee.
Handoff is a later assumption audit: an immutable final prefix alone does not
bind a unique successor. Competing target claims must be possible, and a local
durable binding must exclude accepting both. If the model only repeats an
acceptance predicate as its invariant, defer it rather than call that verification.

Transactions retains acquire, announce, durable choice of `c`, local fix/release,
received acknowledgements, read registration, outcome persistence and participant
installation as distinct actions. A reader cannot inspect a magic global commit
bit. Missing local evidence means it must obtain evidence or wait. Aborts and
no-effect outcomes resolve only their own announcements. A recovery case must
preserve completed-reader bounds as well as object versions. Complete replacement
is initially excluded, not treated as ordinary no-effect resolution.

CheckedReplay is also a composition test: an admitted old read survives a source
overwrite, reclamation attempt and process restart, then supplies evidence for
one durable decision. Grant the read only after its retention root is durably
registered; crash after registration persistence before the grant receipt, then
reconstruct through the retained root and dependencies. Recovering context from
fixture constants or assuming `OldCutAvailable` would evade the question. Start
with one shard, as required for checked native execution; Transactions owns the
two-shard installation case. The read-only variant has an empty effect envelope.
Its context names the position,
sources, executable/profile and required checker set. A report from another
context cannot discharge the obligation. Compare full request/response/effect/
status transcripts even when final result values match. Read-only results obey
the same verification gate. Decision-owner restart with surviving storage is
modelled; permanent destruction leaves an explicit unresolved obligation.

Fixed-history fold checks compare completed consumer states, ordering metadata
and emitted outputs. A continuation may
cross an epoch while independent work completes. They do not compare executions
whose different admission histories legitimately select different outcomes.
This can live beside CheckedReplay if it stays small; otherwise use a separate
fixture sharing the same logical events, not an all-protocol Cartesian product.
Initially use the same explicit reference command order, allowing different
physical completion times. This establishes replay agreement, not general
within-epoch confluence: a concrete rule for additional legal reorderings must
be selected before those can be checked. The desired fixpoint law is a property,
not a guard that may exclude disagreeing schedules.

Admission initially separates storage domains, immutable in-flight packages,
durability receipts and an imported agreed frontier. Producer/witness/consumer
mailboxes and authentic receipt attribution need a later actor refinement.
Body and shared-decoder tokens must actually persist before issuing a receipt;
forwarding the same holder's evidence cannot count twice. Single-domain
destruction removes everything in that domain. Two historical receipts need
not imply two copies ever coexisted: one may be destroyed before another becomes
durable. Preserve that boundary as an executable witness. The initial survival
claim allows one loss over the entire execution, not a fresh one-loss allowance
after each admission. Eligibility, fault-budget renewal and redundancy repair
need explicit interpretation before a stronger claim is justified.

## Evidence boundaries and independent checks

Each module names its imported guarantees and exported obligations. Passing all
modules is not an assume-guarantee or refinement proof: the implementation still
has to realise the interfaces, and the selected seam cases only check their
bounded composition. Keep logical IDs, positions, context IDs, generations,
durable records and messages aligned where models meet. Record how corresponding
simulator observations map to these actions; do not impose a production wire ABI.

Safety configurations allow arbitrary delays and allowed failures. Progress
configurations state eventual service, finite predecessors, terminating programs,
surviving required evidence and recoverable authority explicitly. Fairness belongs
to concrete enabled service/delivery/recovery actions, not a `Commit` oracle.
Use no symmetry for temporal checking. An indefinitely replenished workload
requires its own cyclic fairness model; draining a finite cohort does not prove
starvation freedom. Physical capacity deadlocks remain distinct from reservation
cycles; BorrowLifetime can use one or two capacity tokens without a timing model.
That optional progress case must actually hold a token needed by its resolver
or completion path while ordinary work can proceed. The basic ownership/reuse
case alone cannot establish completion-capacity progress.

Every model needs an independent property and a meaningful bad variant. Initial
controls include execution before all fixes, a skipped pending predecessor,
lost completed-read bounds, missing checker or context/transcript comparison,
frontier advancement across a hole, duplicated same-domain evidence, missing
replay/decoder retention, early checkpoint publication and premature borrower
retirement. At least one omitted retry/service action must produce a liveness
counterexample under the stated fairness. No negative counts as detected merely
because its run exits unsuccessfully: retain the expected property and trace.
An omitted action is also removed from its fairness clause; otherwise an enabled
but unscheduled action can make the specification vacuous. Require a legal
infinite stutter or cycle counterexample under the remaining assumptions.

Also establish reachability of the behavior a positive check is meant to test:
publication, abort, partial installation, restart after persistence before receipt,
late old access, and final retirement as relevant. A passing invariant with an
unreachable publication action is not useful evidence. Use small dedicated
reachability configurations rather than a growing history variable in every run.

Some design questions remain open: durable ownership after permanent coordinator
loss; concrete leader/witness replacement; distributed root registration and
retirement; and the identity/restart boundary of pre-persistence externally
effective processing. Do not add an invented protocol or an exactly-once external
effect claim to close these gaps. In particular, committed intents and replay
alone do not guarantee a remote effect happens exactly once.
Checked read-only execution needs its own agreed context/bounds action: the
native fixture currently requires an output ticket. Pre-position failure also
needs cancellation to fence delayed reservation messages; an unrestricted
`Abort` that magically releases everything would assume away that issue.

## Make exploration affordable before scaling it

Bound the authored workload, versions and fault episodes, rather than silently pruning
states with an arbitrary queue or clock constraint. Keep values symbolic and
small; preserve causally meaningful durability, delivery and incarnation steps.
Use finite idempotent message records where multiplicity has no semantic effect,
with that abstraction stated. A retryable obligation can remain pending or be
delivered; do not cap its retry count and thereby remove its eventual service
path. Bounded fault episodes restrict coverage explicitly. Avoid unbounded histories, packet timing and all
topology permutations. Derived observations need not become mutable state.

The initial local ladder uses one worker and an explicit heap budget. First
complete the smallest distinguishing positive cases and expected counterexamples.
Then increase one meaningful dimension at a time with 30–120 second observation
budgets. Selected interactions and unsymmetrized temporal runs may take a few
minutes. A budget stop is **incomplete**, never a pass. Keep deadlock checking
enabled; model legitimate terminal stuttering explicitly.
Terminal stuttering is guarded by independently defined discharged obligations,
not by the absence of an enabled useful action. Position bounds likewise must
leave room for every authored transaction; test exhaustion separately if it is
an actual policy rather than silently treating it as a protocol deadlock.

Record source/config hashes, exact TLC artifact and JVM identity, command,
generated/distinct states, queue and depth over time, elapsed time, peak RSS and
metadata storage. Separate temporal-check cost where reported. Diagnose expensive
actions on small instances; profiling itself changes speed. A high early state
rate with a growing queue is not a completion estimate. Require completed adjacent
sizes, then measure worker scaling on a fixed case before using bare metal.

The roughly one-hour target is for the entire selected larger suite, not each
module. Choose its dimensions from measured growth and the interactions they
expose. Publish omitted combinations and any bounds reduced to fit; more machines
do not justify erasing a difficult interaction from the coverage claim.

Use the [shared, byte-pinned TLC artifact](../../tools/tlc/source.json)
with the local Java 21 runtime initially. The upstream release URL changes in
place; retain the hash, not just the release name. Preliminary smokes used the
older July artifact from Calico; the captured suite uses Scaffolding's tested
September build. Scaffolding owns reusable worker and
checkpoint support; local model experiments should not duplicate that machinery.
Completed checkpoints and periodic export matter more than an interruption
notice alone. Recover from the same sources, configuration and tool identity.

## Review basis

Independent reviews from Orbital ASSISTANT and three focused audits covered
transaction composition, durability/retention and the xmem modelling history.
Their consequential challenges are incorporated above: independent read semantics,
explicit consensus/recovery gaps, retained old-cut verification, physical borrowers,
and completion-based sizing rather than initial throughput extrapolation. The
synthesized plan received a second review before model code was written.

Second review accepted this decomposition after tightening retry finiteness,
fairness mutations, successor binding, the durable read-grant seam and the
distinction between ownership safety and physical progress. Start implementation
with Transactions and BorrowLifetime and their negative controls. Then add
CheckedReplay, AdmissionCoverage and RetentionClosure at their smallest useful
sizes. This is a coverage menu, not a requirement to build every dimension before
learning from the first complete local graphs. Handoff and general epoch
reordering remain explicitly scoped follow-up questions.

The Calico report at commit `62dfcd6a` records a multi-region × pull run stopped
at 113 million distinct states with a growing queue; reducing `MaxGen` to zero
excluded the pull interaction. Its M1D BFT history records 913 million distinct
states before reducing two generations to one. Conversely, synchronous pinning
completed in 6,489 states and asynchronous registration failed in four steps.
These justify targeted models; none of Calico's mechanisms becomes an Orbital
requirement. See the [Calico source map](../../notebook/calico.md).

TLC's own guidance also distinguishes [constraints and their coverage effects](https://tla.msr-inria.inria.fr/tlatoolbox/doc/model/spec-options-page.html),
[symmetry and temporal checks](https://tla.msr-inria.inria.fr/tlatoolbox/doc/model/model-values.html),
and [execution options](https://github.com/tlaplus/tlaplus/blob/master/general/docs/current-tools.md).
