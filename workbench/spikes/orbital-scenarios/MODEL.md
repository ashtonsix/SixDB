# What the models mean

The original comparisons below use “BRIEF2” for the former
compute/promote/renew proposal. The [current recommendation](CONVERGENCE.md)
selects complete mutation envelopes followed by fixed-position execution.
Its new application, authority and composition probes state their limits in
[ELT-WORKED.md](ELT-WORKED.md), [ENVELOPE-AUDIT.md](ENVELOPE-AUDIT.md)
and [COMPOSITION.md](reconsideration/COMPOSITION.md). The timing/core sources
behind the earlier retained evidence remain unchanged.

The [archived arbitration brief](../../../orbital/stale-drafts/BRIEF-arbitration.md)
motivates the earlier protection and fold experiments below. The shared
payload/queue experiment compares a retained-protection policy with the
[former compute/promote/renew proposal](COMPARISON.md)'s snapshot and output
certification. These are design instruments, not Orbital implementations
or complete protocols. [Findings](FINDINGS.md) and [history](HISTORY.md) retain the
earlier experiments and evidence.

## Shared payload and queue experiment

`comparison.py` runs the same offered transactions, values, queues and links
through the policies below. Fixtures in
`comparison_inputs.py` include reports, increments, blind replacement and
aggregate-derived writes. Read stages form a linear dependency chain across
shards. Footprints are authored, not discovered by executing SQL. Collections
have a fixed declared key universe; `None` represents an absent row whose later
insertion is covered by the same observation.

The harness computes actual effects from captured values. It replays committed
transactions in their claimed serial order, checking every observed scope and
resulting write against that history. Arbitration uses decision order;
certification uses the chosen logical commit position. Installation can reach
participants separately; the final-state check excludes keys still protected by
an uninstalled committed output. This checks the executed histories, not every
possible schedule or real-time ordering.

The old policy is a common-harness variant. It reuses `batch.independent_set`'s
oldest anchor and optional age/work/bounded selection, not the earlier scheduler's
epoch timings. It retains compatible read/write grants, collects independent
components, reserves their combined scopes, defers bridges, and invalidates only
conflicting grants and dependent chain suffixes. An output-only yield can retain
computed inputs and effects. Known outputs are reserved during input capture by
default; `early_writes=False` defers their reservation until computation ends.
At collection, a decision with local claims and coordinator authority has no
additional arbitration delay. Collection still sees a complete multishard
snapshot and applies verdicts atomically. Graph processing consumes no service
capacity by default; an optional per-edge delay is a sensitivity, not measured
solver cost.

`priority_locks.py` instead uses wound-wait or wait-die with stable original
arrival/identity priority, retained read/write grants and whole-attempt restarts.
Younger incompatible requests cannot pass older registered waiters. Decisions
are delayed through the same abstract control path; a sufficient older holder
or waiter can witness a wait-die rejection. These policies have no component
fences, but a broad queued request can still exclude younger requests to its
keys. This is a particular queue discipline, not every implementation of these
algorithms.

Certification adds delivered participant-bound probes before remote capture,
actual-output promises after computation, and source renewal when the commit
position is promoted. Output acquisition is all or none within each participant.
Promises survive a coordinator's rejection until that participant receives the
abort; already-issued requests can still execute before participant cancellation.
Abort propagation, acknowledgements and subsequent retries use the queues and
links. Bounded retries can end in an explicit failure. The fixed-position control
uses the same machinery but rejects a required promotion. Historical reports are
an explicit fixture option, not the same freshness service as current reports.

The `occ` comparator uses this same engine but forces every updating transaction
to promote and validate. `snapshot-wait` changes initial capture to wait at the
same position, retaining earlier captures; output and renewal conflicts still
reject. Its `read_wait_ticks` limit is an attempt-age deadline checked on a
blocked capture, including earlier work, not a timer started at first blockage.
Neither waiting nor explicitly older snapshots establish external consistency.

`ordered-writes` adds `write_admission.py` before choosing a snapshot. Every
writer acquires its complete, predeclared output groups in canonical shard order.
Readers ignore this admission, but can meet later certification promises. Output
admission lasts through computation and is released per participant with install
or abort. This layer has no admission deadline; pending-at-horizon results expose
that difference from bounded optimistic retries. An older queued broad group
can delay writes to its currently free keys. It is not discovery of SQL outputs.

All policies execute eligible short local work atomically within one service
tick. A denied local probe costs one unit when no computation ran. Larger work
shares a shard queue through bounded quanta. Row work is charged at the
coordinator in synthetic service units; this is neither measured CPU time nor a
distributed scan or data-transfer model. Authored private delay is separate from
message delay. Links distinguish source and destination: local delivery adds no
network delay, while remote delivery uses the configured delay; both can queue
for service.

Outputs distinguish completion, failure and pending work, successful latency,
discarded computation, component sizes and protected-key time. Transaction-control
message counts include modeled requests and replies but omit arbitration and
wound/die decision traffic and payload bytes. Held-key time excludes waiter
priority, read certificates and history retention; zero retained read protection
does not mean zero read metadata. Source-only late-message cancellation at an
attempt boundary is simplified. These counters do not establish throughput or memory cost.
Transport is failure-free, and the experiment supplies operation order rather
than proving epoch confluence. It models no recovery, full SQL engine, version
garbage collection or native-fold performance. The fold probe below remains a
separate algebra experiment.

`discovery_probe.py` separately evaluates an actual data-dependent maximum-row
update, including its returned key/value. It compares optimistic certification,
narrow discovery followed by admission and fresh evaluation, and conservative
whole-collection admission. Independent program replay checks every observation,
effect and returned value; authored moving-winner/backedge histories expose
invalid stale computation and repeated output-manifest changes. It has no timing
model and does not supply dynamic footprints to the queue simulator.

### Admission after discovery

`output_policies.py` adds two policies without changing the shared scheduler.
`output-wait` computes first, takes actual output groups in canonical participant
order, promises each granted group, and certifies the original computation.
`output-refresh` first takes all write-only gates without promises, discards the
discovery pass, and executes again at a fresh snapshot before certification.
That extra execution and its private delay are charged. Sources outside the
admitted outputs can still invalidate the refreshed computation. A changed
output set restarts; gates do not silently expand in place.

Admission grants are all or none for each participant's group. An older waiting
group can exclude a younger writer on a currently free key. Canonical participant
order prevents gate-acquisition cycles, provided each participant group is known
and acquired once. These gates have no admission deadline. A source-blocked
local shortcut releases its preliminary gate and enters the discovery path;
it cannot sleep holding a gate on which its source producer depends.

### Fixed-position execution

`fixed_execution.py` and `fixed_simulation.py` test a distinct protocol; the
shared engine's `fixed-position-control` ablation is not this design.
`fixed-known` starts with declared complete output coverage. `fixed-discovered`
obtains that coverage from a private preliminary execution. Both then:

1. Acquire all write-only gates in canonical participant order. Partial gate
   ownership creates no read-blocking output promise.
2. Reserve local lower bounds after output heads and relevant read stamps.
   These temporary promises may block reads even though values do not exist yet.
3. Choose one unique position, publish it at every output, and await every reply.
   Raising a temporary lower bound only shrinks its blocked interval. Duplicate
   reservation replies retain the *original* bound, not the subsequently published
   position.
4. Capture and compute at that exact position. Each read registers its position
   before waiting, preventing newly admitted writers from continually entering
   its predecessor set. Already reserved earlier writers can finish.
5. Seal actual values, deliver them to a separate participant staging ledger,
   and await staging acknowledgements before committing. Earlier position
   acknowledgements are not evidence of value delivery or durability.
6. Install actual writes and resolve unused promised coverage per participant.
   A conditional no-write result must not install fake values or leave promises.
   Abort resolves full coverage even if computation never produced any values.

A discovery execution that produces no outputs can itself commit as a read-only
transaction at its captured snapshot. It needs no output admission. Final fixed
execution may use a subset of coverage; a newly discovered output outside it
aborts. The short local path can combine all necessary operations in one logical
step; a blocked shortcut is demoted. Whole-attempt retries, discovery passes and
fixed executions have distinct identities and counters.

There is no source renewal after final execution. Once positions are exact,
data-read waits descend through unique serial positions. This is a conditional
progress argument: position allocation/publication and abort resolution must
finish independently of blocked data execution; gate acquisition must retain
its order; workloads must terminate; coverage must be complete; scheduling and
message delivery must make progress. The model's finite blocked-capture deadline
can still abort work. Temporary bounds are not covered by the descending-position
argument until their owners publish exact positions or abort.

All timed protocols use failure-free transport. Caller-side participant abort
fences reject delayed position and payload operations after local abort delivery.
The core alone does not implement those transport fences or durable recovery.
Final installation can be partial physically, but a reader spanning unresolved
outputs waits rather than returning a fractured result. The tests include
conditional write-skew/quota histories, no-write outcomes, partial installation,
duplicate metadata, and independent serial replay; they are not exhaustive
distributed schedule exploration.

This candidate must fit Orbital's deterministic epochs, not replace them with
a shard-wide transaction-completion barrier. Agreed pending continuations must
allow unrelated enabled work to finish. The metadata needed to unblock execution
cannot itself wait for that transaction's publication. Extension verification
gates the transaction's complete extension use under its applicable requirements,
not unrelated transactions in the same epoch. Transaction-wide atomic visibility,
true dependencies and dissemination/witness frontiers can still delay results;
these costs are absent from the queue simulator. No claim of publication locality
follows from its installation latencies. These are integration constraints, not
a dissemination or verification protocol.

### Dynamic programs and folds

`invariant_probe.py` checks on-call write skew, shared quota and inventory
decisions, including returned acceptance and business rejection. Disjoint output
gates do not make stale source conditions safe; correct validation can abort a
transaction that subsequently succeeds as a business rejection. These authored
histories exercise the certification core independently of the queue simulator.

`index_discovery_probe.py` checks an increment of one stable primary row with a
maintained secondary index. The old and new index entries change with the value;
complete output coverage is therefore not established by the primary row's
identity. Its separate repair counter-probes definitively resolve old promises,
retain transaction-level write-only gates, and try additional gates without
waiting. Denial cancels the waiter and releases all gates. Successful expansion
starts a new fixed attempt and retains excess coverage until resolution. This
can stabilize the simple indexed increment, but is additional cross-attempt
ownership, not part of the timed fixed candidate or general dynamic-SQL progress.

`fold_comparison.py` groups completion-only additive increments with identical
complete read/write footprints, coordinator and no private delay. Deterministic
arrival windows and caps seal groups before execution. Each group owns one
public serial position; the oracle independently replays its original members
as a serial block. Individual intermediate versions and value-returning or
conditional programs are excluded. Work costs one shared execution plus one
synthetic unit per additional contribution. Every original request retains its
arrival, window wait, outcome and latency; a failed/pending group does not count
as successful members. Grouping can change eligibility for the short local path.

`fixed_fold_comparison.py` repeats that experiment with fixed-known execution.
It shares the same grouping contract and original-member oracle. This separates
compatible computation from the retry policy governing its combined transaction;
it is not a native-fold implementation or an adaptive batching policy. Its
cross-shard-only sensitivity leaves local programs' arrivals and execution
unchanged. `arbitration_fold_comparison.py` applies the identical grouping to
arbitration and wait-die, checking observations and original members in decision
order, including partly installed commits. Ungrouped adapters must reproduce
their backend's trace, costs and outcomes exactly. No fold model prices message
bytes, extension verification, history reclamation or the overhead of actually
constructing a vectorized plan.

## Earlier key-protection scheduler

Inputs are persisted and admitted. One logical consumer represents each shard.
Shard epochs alternate among nonempty L, C1 and C2 queues. Capacity counts
attempted parts for L/C1 and whole transactions on that shard for C2. These are
service assumptions; logical work weights consume no modeled time.

Transactions have stable identities and finite authored DAGs. The model knows
the whole DAG, but the coordinator discovers successors through current part
reports. Values do not change the authored placement, lock sets or discovery.
C1 protects toy read/write keys, all or none per part. Read/read and protection
within one transaction are compatible. Prepared parts retain grants until C2 or
invalidation. Invalidation discards transitive dependents, increments generations
and rejects delayed reports. C2 authorization excludes contention invalidation.

The L realization conservatively selects compatible read/write requests within
an epoch; it can defer conflicting writers even when another object algebra
could combine them. Its individual attempt transitions and epoch-local conflict
accounting are **model choices**, not a required acquire/execute/release lifecycle.
There is no payload computation in this scheduler. `folds.py` studies a concrete
alternative without deriving it from C1's retained grants.

### Components and reservations

Every `batch_us`, an abstract coordinated retry input considers previously
attempted ready work. It is modeled between ordinary shard epochs without a CPU
charge or their capacity limit. Compatible isolated requests can proceed;
unresolved requests trigger component collection.

Collection sees all currently ready/prepared claims, including holders and
untried ready parts, and joins them by transaction identity across shards. The
default `reservations: compatible` uses read/write incompatibility for both
component membership and provisional exclusion. Components can share read
scopes. `reservations: scope` deliberately reproduces the conservative alternative
that merges any scope overlap and excludes even compatible readers.

Each component reserves its members' combined known lock scopes. Reservations
grant no access; the granted-lock wait graph and the trace's reservation blockers
are separate. A selected transaction may pass its own component's reservation,
but still needs compatible granted protection. Already-authorized C2 can drain.

The three lifetime policies share collection, selection and verdict logic:

| Policy | What a subsequent retry input does |
| --- | --- |
| `batch-independent` (default) | Retains active components; starts decisions for unrelated components. |
| `batch-hold` | Defers all new collection while any verdict is pending. |
| `batch-reset` | Replaces reservations, even if their verdicts are still pending. A negative control for useful lifetime. |

Under independent collection, a newly arriving or discovered transaction that
connects active components waits behind their decisions. It does not restart
them or create a competing reservation. A verdict protects its selected ready
claims until they advance or are invalidated; it does not reserve every future
part until the entire transaction completes. This release rule is shared by all
three policies, so the current comparison isolates their collection lifetimes.
It differs from the historical model's completion-based release.

Selection keeps irrevocable C2 participants, then an oldest compatible anchor.
`age` greedily adds by age; `work` adds by retained work after the same anchor;
`bounded` maximizes cardinality subject to the anchor on at most 18 optional
vertices, with a 20,000-node search budget. These remain experimental policies.
They neither minimize discarded dependent preparation nor establish fairness.
The earlier star example shows the anchor can exclude substantial concurrency.

Verdicts invalidate only granted parts conflicting with selected claims and
their dependents. They defer losing queued work. Changed generations, discovery
or conflicting C2 authority invalidate a verdict. `arbitration_us` supplies the
aggregate collection/solve/return delay; optional local resolution removes it
only if claims and coordinator authority are local. Optional early retries are
another independent policy, not a claim that eager progress is always beneficial.

### Deliberate limits

Collection is a **complete multishard snapshot**. Verdict application and
dependent invalidation are **atomic**. There is no distributed collection closure,
concurrent merge protocol, partial delivery, loss, failure, election or recovery.
Pending components are protected independently inside this idealization; it does
not establish how shards agree on them. New bridges waiting for old decisions
may cause convoys, and finite runs establish no starvation bound.

Control messages have one authored delay, including coordinator-local messages.
Root admission and successor activation are immediate. Equal-time shard order is
an input, not thread scheduling. Assertions check this state machine after every
event. Repeatability and sampled schedules do not prove confluence or distributed
safety. Times are synthetic; counts and work weights are logical, and no throughput
prediction follows from retry processing that consumes no modeled CPU time.

## Earlier integer-delta fold

`folds.py` gives an object two integer counters and transactions that contribute
one or more deltas, returning only completion status. Boundary protection defers
a whole transaction if any touched key is protected. The admitted contributions
then combine in arbitrary orders and binary groupings; no transaction acquires
and releases an intra-epoch lock.

The probe enumerates all 1,680 orders/groupings of five contributions and compares
the entire declared result: object state, completed IDs and deferred IDs. It
separately checks a protected key, and a counterexample whose transactions return
intermediate counter values. Equal final object state does not suffice when those
observable results differ.

Integers here are unbounded; the probe does not claim that floating-point updates,
arbitrary SQL transactions or undeclared protocol outputs commute. It models no
physical concurrent executor, retries across epochs, or distributed visibility.
Choosing an object's algebra changes which requests are incompatible; combining
this with dynamic preparation remains an open experiment.
