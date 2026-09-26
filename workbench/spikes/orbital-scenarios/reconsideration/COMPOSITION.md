# Extension execution and contention composition

2026-09-26. Ashton clarified that extension verification gates **transactions**,
not epochs, and that specially approved extensions are exempt from BLAKE3
verification. This supersedes the epoch-wide gate assumed by the retained
[composition_probe.py](../composition_probe.py) and its recorded results.
That historical probe remains unchanged for reproduction; its publication
assertions do not validate the corrected contract below.

## Transaction publication and replay identity

A transaction's results become visible only after all its extension use satisfies
the applicable verification requirements. Hardened WASM execution supplies its
determinism assurance; Firecracker outputs require BLAKE3 verification unless the
extension is specially approved. This covers intermediate extension use as well
as the final output.

The epoch may retain the transaction as pending while unrelated transactions
complete and become visible. Work depending on the pending transaction's effects
still waits. Internal extension/query calls may produce tentative results;
they must not require the calling transaction's publication to finish its own
execution or verification. Agreed pending state and protocol outputs must remain
deterministic; removing the epoch-wide gate does not make unverified VM output
authoritative. The complete verification/admission protocol remains open.

The journal identifies the exact extension source versions and executable
artifacts used by each transaction, with applicable verification requirements or
approved exemptions. Retain referenced source and executable artifacts for replay
and audit. A later extension update does not change the identity of prior work.
Bindings can use immutable references in the admitted plan; this does not require
copying source text or journaling every deterministically derived internal call.

## Critical review: comparing consumers' hashes

Ashton's proposed starting rule is to take an arbitrary consumer's hash as a
reference, require the other required executions to match it, and fail the
transaction on mismatch. There is no majority-selected output. Once all required
checks are complete, the all-match predicate is symmetric; reference selection
need not introduce a leader or an additional agreement protocol.

The remaining design choice is which executions are required. Define that finite
set through the agreed plan/policy and use existing transaction participation and
decision state to account for completion. A matching subset is insufficient if a
required result is outstanding. A crashed checker is missing evidence, not a
match or mismatch; its role must recover or the transaction must fail through an
agreed outcome. Copying another consumer's result is not another execution check.
Later replay discrepancies cannot retroactively abort a committed transaction.

BLAKE3 comparison establishes agreement on the checked bytes, not that the
program implements its intended business rules. The checked record must bind
the invocation, exact input and execution identity, issued requests and responses,
effects, result and completion/error status. Framing and logical call identities
matter: two executions returning `OK` can have performed different work.

### Recommended initial shape: prefer WASM, keep checked native work local

Ashton's author guidance is to prefer WASM, reserve unapproved execution mainly
or only for shard-local transactions, and allow deployment owners to approve
unchecked native execution after assessing the risk. Here **checked native**
means Firecracker execution requiring comparison; hardened WASM does not acquire
a native-result checking requirement merely because its author is unprivileged.
Permission to access data or I/O remains distinct from approval to omit checking.
A strict local-only support boundary is a proposal, not yet a user decision.

This supports a simpler initial checked-native path than the general interactive
verification scheme below:

1. Establish T's fixed position c and complete local output envelope through the
   existing protocol. Before execution, register an observation floor covering
   every permitted local observation, independently of the program's outputs.
2. Run the required native executions against that versioned context and their
   private own effects. Queries need no new shared read registration because the
   context already covers them. Keep results, chosen callees, errors and effects
   private; code bindings and permissions come from the pinned context.
3. Compare complete interaction/outcome records under the all-match rule. Admit
   completion or failure evidence through later agreed input and resolve T using
   its existing decision/install protocol.

The floor is a nonexclusive ordering bound, not a lock or a materialized read of
the entire shard. Registering it must not await all earlier pending effects.
Each actual query waits only for its relevant predecessors; future writers move
above c without waiting for T to finish. A floor alone does not retain versions:
captured bytes or the needed history must remain available. No-effect output
coverage is resolved only with the accepted outcome, not from an unchecked
execution's early claim that it will not use a destination.

All logical source/effect authorities must be local, not just the final writes.
Fetching immutable bytes from remote storage is compatible. Discovering a live
remote transactional source, launching a child transaction or performing a new
shared mutation outside the envelope is not. Previously captured external facts
can be explicit inputs, with their stated historical meaning.

This path accepts deferred publication: a fresh checked-native transaction is
not promised to complete in its initiating epoch. It avoids per-query check
rounds and avoids selecting epoch state by local verification timing. Trusted
WASM and approved deterministic native execution can still compose directly
within an epoch. The local restriction does not narrow a broad output envelope;
a slow `MAX` that may update any winner can still delay many observations.

Costs include replicated execution, retained history/output space, pending effects
and one later admission step. The coarse floor also orders unrelated future
allocations above c; compared with fine per-domain floors, that can move their
cross-shard snapshots past additional pending predecessors. It introduces no
source-read exclusion and no requirement to wait for the shard to drain.

The [local-context probe](../extension_context_probe.py) supplies four bounded
histories using the existing fixed-position core unchanged. Omitting the floor
allows a newly admitted earlier-position write to invalidate T's private read,
which the checker rejects against the assigned position order. With the floor,
the writer finishes while T remains pending and the assigned order is valid.
Another case keeps an unrelated earlier write unresolved: context registration
and a disjoint read proceed, while an actual read of that pending output waits.
Different private queries leave identical shared bounds even if both return
`OK`; their complete interaction records differ and an assumed agreed abort
retains the floor. The negative control is not a claim that every serial order
is impossible; it specifically violates T's fixed-position snapshot.

```sh
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/extension_context_probe.py --output build/orbital-extensions/local-context.json
```

These are four authored semantic examples, including the expected invalid
control. The probe supplies checking/decision facts and has a fixed key universe;
it implements no VM, cryptographic verifier, consumer-set protocol, epoch
admission, retention budget or timings. It does not prove the proposed path.

### The first shared effect can precede the final result

Suppose T at position 20 emits a query for x on one consumer and z on another.
Both queries return 0 and both executions eventually return `OK`. Registering
the observations before comparing requests raises x's observation bound on one
consumer and z's on the other. A later writer of x can consequently receive
different positions. Comparing only `OK` misses the divergence; comparing the
whole transcript at the end detects it after shared ordering has changed.
Aborting T cannot retract the effects on those later writers.

For unrestricted dynamic source discovery, the smallest proposed remedy is to
check extension-produced request bytes before they change shared ordering or
other authoritative state. Consumers can compare
the emitted request and its causal prefix at that boundary, then execute its
handler within T's position and tentative state. Pure private computation can
remain tentative and be checked together at the end. A verified read bound can
remain after T later aborts, as an ordinary conservative observation bound.

This does not inherently require a witness event per internal call, but its epoch
boundary is unresolved. Checks already represented in agreed inputs can support
an eligible same-epoch chain. Fresh checks can instead enter later agreed input,
or a finite intraepoch exchange can delay that epoch's completion. Choosing
whichever reports happened to arrive locally before closure is invalid: replicas
could register different bounds before later position allocations. We have not
established arbitrary fresh checked-native same-epoch chains without that wait
or further agreed input. The request-prefix scheme also needs incremental
checking or a completed query-emitting invocation boundary. A verifier that only reports after the whole
transaction finishes cannot unlock the first request on which that finish
depends. Sequential dynamic requests may therefore introduce sequential
verification waits. The number/placement of checking consumers and this latency
remain unmeasured design choices.

| Alternative | Consequence |
| --- | --- |
| Check request prefixes before authoritative host actions | Preserves late source discovery and existing contention rules; adds check dependencies and needs a precise epoch/admission boundary. A general extension to the restricted path, not required initially. |
| Register all possible observation domains before execution | Nonexclusive bounds can cover speculative reads without source locks. Credible for the local context above; arbitrary multishard coverage can create broad metadata/fanout costs. |
| Journal only one final transcript | Does not repair missing read bounds at the time inputs were captured. It needs earlier request authority, full advance coverage, or a new validation/re-execution mechanism. |

The final publication gate covers all dynamically invoked extensions and all
their required checks. With mismatch defined as transaction failure, an
irrevocable commit must also await this evidence. Physical completion order must
not choose an epoch's pending/ready state: that follows agreed input or a
deterministic transition. The verification message/admission construction is
still open; the current probe does not establish it.

### A small transactional interface

The recommended starting contract is a resumable computation over explicit byte
inputs, mediated application requests and tentative effects. Internal handlers
join the parent transaction; creating a child transaction that waits for its
parent's pending result can create a cycle. Effects stay inside the admitted
envelope. Engine or another application binding owns request meaning and scope;
Orbital need not interpret queries or application data.

Private process memory is disposable execution state. Persistent dictionaries,
model adaptation and connector offsets must be explicit versioned inputs/state
effects if they influence future results. Warm processes can cache derivable
state. A durable opaque VM snapshot is a larger possible contract, not required
for this initial one. Recovery can reconstruct execution from retained inputs
and checked interactions; it must not fetch fresh external facts.

| Use | Composition and accepted limit |
| --- | --- |
| Parser or format conversion | Pin code, options and dictionaries; stage finite output. A pure pipeline can compare its complete output without a check between every private function. |
| Extension → query → extension → write | The declared local context permits private callbacks. Beyond it, requests need checking before shared ordering effects. Handlers use the parent's position and tentative state; final commit waits for the whole chain. |
| Enrichment or model inference | Pin model/runtime inputs. A live service response is a captured external fact, not a function every replay calls again. Exact-byte output restricts numerical implementations unless an approved contract supplies another assurance. |
| CDC and stateful streaming | Finite transactions advance explicit state and offsets. An indefinitely running atomic process cannot publish verified prefixes while future extension use is unknown. |
| Read-only or streamed query results | Results are covered by the transaction gate too. Internal chunks may flow tentatively; externally visible chunks need a verified finite transaction or an explicitly different streaming contract. |
| External writes | Stage durable intents and dispatch after commit. A remote sink's deduplication/transaction support determines delivery guarantees; verification cannot undo an earlier payment or message. Direct precommit effects need a separate contract, such as the existing data-loss-tolerant processor path. |

An approved exemption removes a required BLAKE3 comparison, not the obligation
for consumers to reach the same logical state. It may permit a broader physical
representation contract. Private clocks, randomness or live I/O still cannot
silently become differing agreed facts. Resolve extension names from agreed
version bindings, not each host's current installation. Pin source/executable
identity, a versioned runtime/ABI/configuration profile and the applicable
exemption; source text alone does not identify executed behavior.

WASM hardening must cover deterministic imports, floating-point NaNs, relaxed
SIMD and memory/table growth outcomes. Wasmtime documents these boundaries and
their costs in its [determinism guidance](https://docs.wasmtime.dev/examples-deterministic-wasm-execution.html).
Scheduling may pause work without changing its meaning; a local timer must not
independently select a semantic failure. Deterministic fuel is one possible
execution-limit policy, distinct from wall-time interruption
([Wasmtime interruption](https://docs.wasmtime.dev/examples-interrupting-wasm.html)).
Firecracker supplies isolation, not deterministic guest execution; identical
images do not by themselves capture all inputs
([design](https://github.com/firecracker-microvm/firecracker/blob/main/docs/design.md),
[snapshot behavior](https://github.com/firecracker-microvm/firecracker/blob/main/docs/snapshotting/snapshot-support.md)).

Slow execution and missing verification still retain T's pending effects and can
delay actual dependents. Bounded output/retention budgets and an independently
runnable metadata/resolution path remain necessary. Hash mismatch, a reproducible
program error and an unavailable executor are different evidence; none licenses
publishing unchecked output or clearing unresolved effects on a local timeout.

### Approved execution with asynchronous checking

Approval can remove synchronous comparison while retaining background checks.
Re-execute against the recorded inputs or their exact reconstruction recipe,
source/executable versions and runtime profile, then compare with the accepted
interaction/outcome record. The baseline must survive; comparing against whatever
data or code happens to be current would not reproduce the transaction. Use
exact-byte comparison for that contract, or the approved application's logical
comparison where different physical representations are permitted.

A postpublication mismatch is an integrity incident, not permission to abort a
committed transaction. Preserve both results and their provenance, identify
affected transactions and dependents, and support scoped containment and recovery.
An execution mismatch proves disagreement, not which execution was right or that
bytes were necessarily lost. The existing forensic/SOS path can preserve evidence
when the incident or infrastructure warrants stopping admission. Do not repair by
silently choosing the newer result or replaying external side effects.

Audit coverage, delay and retention determine what can be discovered. Sampling
offers a different assurance from checking every execution; a backlog increases
both exposure and the history that must remain reproducible. Missing audit inputs
are missing evidence, not a successful check. Reproducing the same deterministic
program bug can still match, so this checks execution agreement rather than
business correctness. These limits do not negate the value of detecting an
otherwise hidden inconsistency.

## What can finish in one epoch

The program reads a collection, chooses an account and its shard, emits an exact
query, consumes its result, and emits a conditional decision and write. When its
certified remote input is available, the entire chain reaches one epoch's
fixpoint with no witness event per internal call. Enumerating permitted local
action orders preserves all bytes, regional changes and protocol messages.
Independent origins derive identical message identities and payloads; duplicate
delivery order changes neither. This checks an adapter contract, not arbitrary
network schedules or agreement on the remote read itself.

When the remote input is unavailable, the probe's completed and verified
query-emitting invocation leaves a pending transaction continuation. A later agreed input
resumes it without changing its captured bytes or pretending that its first
invocation ran in the later epoch. A business rejection produces an explicit
negative result and no writes. Candidate writes still need transaction admission
and atomic publication; producing the bytes is not commitment.

## The superseded epoch-gate experiment

The historical dependency graph imposed an epoch-wide verification gate. Its
200 illustrative wait units therefore delayed three disjoint responses by
200/199/198 units; moving execution to another shard removed those authored
edges. Those results describe the assumed graph, not a requirement of the
corrected architecture. Separate execution shards are not required to avoid
that verification barrier. Loom can still isolate resource-heavy execution
where scheduling and capacity justify it.

The historical synchronous-callback cycle also assumed that a query needed its
enclosing epoch published. That condition is not imposed by transaction-scoped
verification. A real dependency on the calling transaction's own publication
would still be a cycle and must be avoided.

Remote read results and their common cut are explicit agreed inputs in the
probe. Accepted-byte facts stand for completed BLAKE3 checks; pure functions
stand for the runtimes. The probe implements neither verification, approved
exemptions, source-version journaling nor the revised publication behavior.

## Contention remains a separate choice

Exact extension bytes do not imply known output identities. The dynamic-output
history safely rejects fixed execution whose predicted output is wrong. An
optimistic actual-output attempt commits the quiet case, but retains the known
moving-source/retry limitation. A conservative output domain covers both possible
targets and blocks a writer to a target the final program does not use. An
unrelated key still progresses.

Transaction-scoped verification removes the assumed epoch-wide dependency;
it does not remove genuine effect dependencies or resource contention. Likewise,
the local action-order checks do not prove Orbital confluence. Admission order,
remote facts, read predicates, position metadata, abort resolution, verification
authority and retained history must compose under agreed state. In particular,
metadata needed by a blocked program cannot wait for that program's completion
or verification before becoming authoritative.
