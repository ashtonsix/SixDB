# What the verification says about the architecture

The test is whether a small set of strong contracts composes, not whether enough
small models can be made green. This assessment accompanies the exact finite
coverage and limitations in [PLAN](PLAN.md), the family reports and the evidence
index. It is not an assertion about arbitrary implementations or deployments.

## The foundation worth sharing

The model boundaries support four common mechanisms:

1. **A durable logical owner.** Chosen-prefix agreement, stable operation identity,
   idempotent ordered folding and barrier-correlated recovery have the same meaning
   for transaction, root, delivery and effect records. An implementation can share
   this consumer plumbing. Each owner still defines its own commands and outcomes.
2. **Explicit outstanding obligations.** Acquisition precedes use; completion names
   exactly what it discharges; terminal state fences crossed late requests. Recovery
   reconstructs outstanding work from owned records and actual evidence. A received
   reply or a cached absence assertion is not the durable owner of that obligation.
3. **Immutable material with accountable custody.** A cut/context identifies meaning;
   a recipe identifies a reconstruction path; a physical copy identifies storage.
   Transfer uses actual bytes and successor acceptance. Discovery, rescan and copy
   resumption are services of the relevant owner/holder, not omniscient inventory
   supplied by a coordinator or test harness.
4. **Physical lifetime independent of callers.** A shared per-host registry primitive accounts for
   queued, active, completed and retired work. Named capacity leases remain charged
   until actual last use. Root release, subscriber cancellation and process death
   cannot retire a still-running operation. The same registry and lease mechanisms
   now serve holder metadata, views, transaction resolution and emergency services.

These are common invariants and ownership boundaries, not an invitation to create
a universal acquisition framework. Several evidence types have deliberately
different meanings. Durably fixing a transaction's position at a shard releases that shard's reservation;
a decision resolves its semantic outcome. A delivery custody receipt is weaker
than application completion. Root-transfer custody preserves a reconstruction
obligation. An external sink's lost reply may leave an Unknown outcome even after
Orbital's own intent is durable. Data-protection receipts quantify destruction
since a generation began; they do not prove all historical holders are still alive.
Combining these meanings behind one undifferentiated `ack` would simplify names
while weakening the contracts.

There is a concrete implementation simplification opportunity: use one ordered
journal-consumer adapter, one incarnation/correlation vocabulary, the shared
[backend registry](ViewsKernel.tla) and [lease-accounting](CapacityKernel.tla) primitives. Keep transaction ordering, recipe
retention, recipient coverage and sink ambiguity as separate semantic folds. The
models should continue to reuse the actual kernels across compositions; independent
near-copies of a protocol would undermine the reference value of this work.

## Classifying what changed

| Finding | Architectural significance |
| --- | --- |
| A replaying consumer could reread its transaction's already-installed output as the input to that transaction. | The abstract source view must exclude the transaction's accepted output, as the concrete retained-material source already does. Own tentative effects remain a separate overlay. |
| Capturing an entire local map made replica output depend on unrelated physical installation. | Precision of deterministic execution: capture the declared logical inputs. The counterexample was in the modeled capture implementation. |
| Two checkers could agree while disagreeing with the selected main output. | Precision of the existing full-output verification obligation, not a new epoch-wide verification barrier. |
| Source/profile context was present in the log but execution reread a fixture constant. | Missing modeled data flow; actual journaled versions and verification policy must supply execution context. |
| Replay of a previously enqueued reservation returned an obsolete empty reply. | Recovery must reconstruct grant evidence from the recovered state. This strengthens the implementation of disposable drivers. |
| Migration carried a payload that its receiver did not consume; its initial floor was zero. | A hollow composition check. The revised case preserves a positive source-owned bound. Same-owner authority movement can recover it from the certified prefix; this does not require a second production metadata-transfer mechanism. |
| A one-accept scenario actually delayed failure until all voters accepted. | Authored-history coverage defect. The cut must independently demonstrate the acceptance count it claims. |
| Directory discovery could miss a later completed package; one-shot forwarding could strand a lost destination inbox. | Necessary recovery services: persistent discovery/rescan and message-driven copy resumption. These make eventual-service contracts concrete; they do not alter contention order. |
| A root's terminal owner record preceded holder tombstone persistence and physical retirement. | Logical and physical completion are distinct existing obligations; sharing the registry/lease substrate makes this distinction enforceable. |
| New subscribers could evade completion checks or inherit an already-retired source obligation. | Separate captured-cut debt and real retention acquisition are required. Membership changes cannot rewrite historical completion. |
| Transferring a complete recipe did not exercise transfer of a read still awaiting a source result. | Missing composition evidence for the existing coverage-root contract. The joined case now transfers actual fallback custody and chosen pending-source responsibility, then recovers and finishes the same cut after old material is deleted. |
| Unordered tail replay could turn a resolved source back into a pending source. | An intake-adapter defect. A complete authoritative resolution supersedes earlier notices for the same immutable source; it must not introduce an unrelated-source barrier. Mutable identities or partial resolutions would require another rule. |
| Private intents could be redispatched before restored abandonment context arrived. | Recovery must install the actual restored context before external dispatch. Normal deduplication and Unknown semantics remain distinct. |
| Failure checks accepted a status label, or compared a fabricated failure against itself. | Observer defects. Independent retained execution/failure evidence is required before a claimed abort counts as justified. |
| An early-dispatch model retained volatile claims and completed before a fresh grant arrived. | Model/observer defects. Actual incarnation-local loss, recovered grant ownership and authoritative outstanding allocations are now represented. |

The new recovery details are consequential even when they refine an existing rule:
they are implementation obligations that must not be lost in translation. Conversely,
an observer fix, a better failure cut or a larger graph is not itself an increase
in production architecture complexity. The family reports distinguish these from
candidate protocol choices where the briefs intentionally left mechanics open.

## The early path must earn its separate machinery

Early pre-persistence processing is suitably isolated only if it remains optional
and explicitly lossy. Its promise is at most once per original event/processor,
not guaranteed delivery. Durable range allocation and named incarnations avoid a
synchronous per-event persistence hop, at the cost of retiring ambiguous work after
failure. A replacement cannot reinterpret the old driver's range as a new event.
A delayed already-issued attempt can still arrive.

That is a plausible operational benefit for latency-sensitive processing willing
to miss an event, and a poor default for ordinary transactional effects. The normal
outbox uses committed intents and, where available, sink deduplication; otherwise
it preserves uncertainty. The two paths should share identity and durable-owner
infrastructure while keeping their promises visibly different. This verification
neither removes the requested early capability nor measures its value. Range sizes,
refill overhead and acceptable loss on replacement need simulator/implementation
measurements before promoting it as a performance default.

## What remains simple, and what remains costly

The contention centre remains fixed position followed by local reservation release.
Reading a source creates an exact-cut retention obligation rather than enrolling
its entire read set into arbitration. Logical identity, authority and version remain
separate from physical location and physical lifetime. None of the findings above
requires replacing that centre with another contention mechanism.

The accepted costs remain visible. Broad output envelopes cause broad interference.
A transaction that really depends on a WAN fact still waits for it; unrelated work
must have an independent completion path. A late-discovered source can fail if its
old cut is no longer reconstructible. Retaining more history improves that success
promise at a storage/capacity cost. A surviving witness quorum does not conjure
missing material, and an external system without deduplication cannot be given an
exactly-once promise by Orbital's local log alone.

Local release also does not remove reservation convoys. The simulator's broad
waiter can link a small writer to a WAN holder it does not directly conflict with.
`ReservationPolicies` uses the actual grant predicate and the simulator's ordered
scan to isolate the policy tradeoff. Under continuing younger narrow arrivals,
no overtaking lets the broad request progress after its older holders release.
Eligible-first removes the waiter-only bridge, but admits an infinite execution
in which both narrow streams keep completing and the broad request never grants.
Weak fairness applies to actual arrivals, local releases and the grant scan; it
does not assume the broad request eventually acquires a reservation. This small
queue projection does not recycle full transaction identities or establish a
full-protocol infinite-workload theorem. The actual finite transaction families
and the simulator comparison have their separate roles. No new queue policy is
promoted from these results.

The strongest remaining caution is about composition evidence. Local providers
have unrestricted finite-state families; several large joined failures use authored
normal-service schedules with explicit crossing alternatives. Checked provider
correspondence and actual shared records make these valuable, but they are not an
unrestricted product theorem. Their restrictions, atomic durable-record boundaries,
fault assumptions and finite dimensions stay part of the maintained reference.
Formal checks, simulator experiments and eventual implementation refinement have
different jobs. A completed bounded campaign is meaningful evidence for this
architecture; it does not erase those boundaries.

The determinism premise also matters operationally. Approved native execution
trusts the approved program and runtime. An asynchronous audit can expose a breach
and preserve evidence, but it cannot make already published divergent results
consistent again. Synchronous verification and trusted approved execution therefore
support different claims when that trust fails.
