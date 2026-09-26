# Reconsidering preparation and failed-retry contention

2026-09-25. Ashton asked for a complete reimagining, preferring fewer mechanisms
with an accepted tradeoff over increasingly precise contention machinery. This
investigation challenges retained C1 protection as well as component arbitration.
This investigation began against the
[archived arbitration brief](../../../../orbital/stale-drafts/BRIEF-arbitration.md).
The alternatives below are historical proposals; the later
[convergence report](../CONVERGENCE.md) explains the recommendation adopted in the
[current brief](../../../../orbital/BRIEF.md).

The research question is now the composed architecture: epoch fixpoints,
same-epoch extension/query/write chains, extension output verification,
dissemination, preparation, contention and publication. The brief's
[extension](../../../../orbital/BRIEF.md#extensions-and-publication) and
[dissemination](../../../../orbital/BRIEF.md#dissemination-and-adaptation) constraints belong in
that composition; their detailed protocols are not being designed in this detour.
MVTO remains a candidate to revise or discard. The next proposal for Ashton to
review should explain the composition, rather than present another isolated
contention mechanism.

Exact bytes at ordinary extension interfaces require deterministic query-result
encodings even where SixDB permits several internal physical representations.
Ashton has clarified that verification gates each transaction's complete extension
use, not its epoch; specially approved extensions are exempt from BLAKE3.
Unrelated transactions can publish while it remains pending. The
[composition note](COMPOSITION.md) corrects the earlier epoch-wide assumption.
Early PLP-based propagation and independent origins deriving a message must fit
the same account of logical outputs and publication. These are obligations,
not evidence that the current probes already compose. In particular, an
extension/query chain must be able to progress inside one epoch; admitting each
internal step in a separate epoch would fail that requirement.

**Current constraint:** [localize WAN interference](LOCALITY.md). Ashton's regional
latency example rules out a shard/database-wide mutation lane and a common batch
barrier as production recommendations. Their small policy is not useful simplicity
when a rare remote transaction pauses millions of unrelated local writes. The
next comparison must preserve unrelated progress within the same shard, including
publication, while reducing contention machinery. The broad-barrier alternatives
below are retained as negative controls.

The [concrete MVTO candidate and executable probe](MVTO.md) now tests one actual
set of read/write/certification rules. It permits broad reads to coexist with
newer source writes, scopes prepared promises to outputs, and proposes
terminal serialization failure instead of arbitration. Its predicate, distributed
publication and deterministic-fold obligations remain open; it is not a selected
replacement merely because the point-key checks pass.

The useful conclusion is that **protection cardinality, computation size and
exclusion duration need not move together**. A million-row historical read may
exclude no current writers. A million-row mutation may be one ordered program
with private output. Conversely, an empty-predicate check or one shared balance
can impose a real global dependency. Making the metadata smaller does not by
itself reduce that dependency.

The three independent investigations retain the workload detail and sources:

- [Reads and discovery](READS.md): 21 scenarios, including joins, maxima, absence,
  risk limits, graph traversal, authorization, training, long sessions and plans.
- [Writes and ETL](WRITES.md): 17 scenarios, including bulk SQL, ingestion, CDC,
  backfill, refresh, cascades, payouts, migration and observable mutation results.
- [Prior art](PRIOR-ART.md): substantially different mechanisms, what they remove,
  and the costs they retain. Neither single-node locking nor a table metadata
  swap is treated as a finished multishard protocol.

## What generates a large protection set?

The workload catalog distinguishes several causes rather than attributing them
all to transaction size:

| Cause | Example | Consequence for simplification |
| --- | --- | --- |
| Large historical input | Dashboard, training, reproducible export | Keep an appropriate version cut; source exclusion may disappear entirely. |
| Small answer dependent on a broad predicate | Top-k, absence, portfolio limit | The dependency may remain broad even with a compact representation. |
| Large actual mutation | Repricing, deletion, payout batch | Atomic visibility and ordered evaluation matter; individual retained row locks are one implementation. |
| Discovery through changing values | Graph traversal, joins, cross-shard pointer chasing | Declaring the footprint in advance may already require doing the transaction. |
| Indirect effects | Unique/FK checks, cascades, indexes, triggers | A small explicit write set can conceal large reads/writes and shared invariants. |
| Conservative physical scope | Page/table locks, scan-based validation | False conflicts may be accepted for simplicity; they must be counted. |
| Long lifetime or stalled execution | Interactive session, spill, slow participant | Even a small set can intersect an unbounded stream of new work. |

One broad transaction can connect many mutually compatible writers. That is
not evidence they must run one at a time. The current component union reservation
and oldest-anchor selector can introduce additional exclusion beyond the graph.

## Real simplifications, and the price of each

These are alternatives, not a menu of features to combine.

| Candidate | What it deletes | Accepted cost / strongest counterexample |
| --- | --- | --- |
| **Whole-transaction priority locking** | Component discovery, shared arbitration, union reservations, partial yield salvage. A stable age directs waits or whole-transaction aborts. | Keeps retained read/predicate locks and distributed lock/abort coordination. A wide old transaction still blocks or aborts many small writers. Simpler contention policy; limited reimagining of preparation. |
| **One optimistic scheme with a terminal contention outcome** | Retained preparation locks, escalation, component machinery and partial invalidation. Recompute a whole attempt; stop retrying after a stated budget. | Broad current read/write transactions can repeatedly fail while small writes succeed. This preserves successful SQL semantics but gives up guaranteed completion under contention. |
| **Deterministic finite batches** | Retained C1 locks and component arbitration; use a stable batch snapshot, deterministic certification and whole-attempt retry. | A shared completion barrier imports WAN delay into local latency. Unsuitable at shard/database scope under the localization constraint. Its progress argument does not rescue that coupling. |
| **One ordered mutation lane** | Conflict retries, optimistic validation, read protection, component arbitration and a separate fallback mode. Execute the next complete updating program against the preceding state. | A narrow WAN transaction stalls all writers in the lane, followed by backlog recovery. Rejected at shard/database scope; retained as a negative control. |
| **Optimistic execution with one exclusive fallback turn** | Retained C1 read protection, components, union reservations and partial salvage. A whole failed attempt eventually executes with writers paused in a fixed domain. | Two execution modes remain. The pause includes state-dependent computation, not merely a short commit. Broad transactions gain progress by delaying unrelated writers. |
| **Bounded live transactions plus explicit snapshot jobs** | The promise to run arbitrary long analytical read/write work concurrently as one transaction. Jobs publish owned generations or commit explicit chunks. | Atomicity, freshness or workload support changes for some jobs; application/job recovery can become more complicated. General wide SQL needs a maintenance turn or an explicit rejection. |

The prior-art investigation supplies useful counterweights. Aria shows why
physical single-writer execution is not necessary for progress in a deterministic
finite batch, but its long-transaction batch stall is a substantial tradeoff.
HyPer's tentative long-transaction execution supplies a warehouse aggregate
whose changing inputs make serializable validation abort repeatedly. Moving
computation out of locks does not establish that it will remain publishable.
[Aria](https://pages.cs.wisc.edu/~yxy/pubs/aria.pdf),
[long transactions in HyPer](https://www.cidrdb.org/cidr2013/Papers/CIDR13_Paper49.pdf).

## Two broad-barrier alternatives and why their scope fails

These single-mode designs reconsider preparation, but fail the subsequently
explicit localization requirement when they share a shard-wide completion
barrier. They show what could be deleted and why that deletion alone is not a
viable recommendation. Neither is a distributed implementation.

### A. One ordered mutation lane

1. Admit whole updating programs in an agreed order within a fixed domain.
   Every local or cross-shard mutation in that domain participates in this order.
2. Execute the next program against the preceding committed state and its own
   private effects. Discover further work normally; later writers cannot change
   the state underneath it. Parallel work inside the transaction is possible
   without introducing concurrent transactions.
3. Resolve its SQL constraints, errors and returned results. Persist enough
   information to recover the outcome, then publish its effects atomically.
   A genuine application failure records a failure outcome and advances the lane.
4. Execute the next program. Historical readers may use a coherent completed
   prefix while newer work runs, if its versions remain available.

There are no conflict retries to rescue, no read-set validator, no live read
locks, no component collector, and no switch into a fallback mode. Preparation
as a distributed lock-acquisition lifecycle disappears. Pure prefetching can
be speculative; it conveys no authority and must not determine agreed results.
Data dependencies inside a program remain ordinary execution dependencies.

For unrestricted discovery, the simplest domain is the database. A tenant or
partition domain is smaller only if its boundary is enforced; dynamically
acquiring more domains would rebuild a locking problem. Independent shard
epochs do not already supply the required common order. This is an added
ordering obligation and a possible admission bottleneck, even if events are
batched efficiently.

The cost is direct: a minute-long updating graph traversal can put unrelated
writes a minute behind it. A failed participant can hold up subsequent mutations
until recovery resolves the head transaction. Parallel query execution does
not remove that head-of-line delay. Historical readers also need a version/cut
service that the brief at the time had not established.

Logical completion need not wait for all physical compaction or page rewriting.
It must leave a recoverable representation that every subsequent observer can
interpret consistently, including indexes. A generic language of deferred SQL
programs is not assumed; it would be additional machinery to justify separately.

### B. Finite deterministic batches, with whole-attempt retries

1. Choose a finite, agreed batch order. Previously failed attempts retain their
   relative order ahead of new arrivals. Begin from a coherent committed state.
2. Compute the batch's transactions against that unchanged state, with private
   effects and each transaction's own writes. Dynamic discovery is allowed;
   no transaction retains locks against another transaction's preparation.
3. Certify attempts in agreed order against the logical state produced by earlier
   accepted attempts. Accept a compatible result or discard the whole attempt.
   Reads of absent rows, constraints, catalogs, outputs and discovered work count.
4. Publish the agreed accepted effects and retry outcomes as one logical batch
   result. Recompute failed attempts in the next batch ahead of newcomers.

For a finite batch of terminating, otherwise valid transactions, the first
attempt faces no earlier accepted change to invalidate it. At least that attempt
can finish; keeping failures ahead of newcomers prevents perpetual overtaking.
Changing footprints on a later attempt do not invalidate this argument because
the new attempt uses the next batch's stable state. This is a logical progress
argument under those assumptions, not a bound on latency or recovery time.

There is no separate contention-resolution protocol when repeated attempts fail.
They advance through the same rule. The cost is a barrier: completing the batch
requires its slow work to finish or receive an agreed failure outcome. An
interactive transaction, slow join or unreachable participant can stall unrelated
work. Buffered results and repeatedly discarded large computations also cost
space and CPU. Aria provides relevant prior art, not an already suitable SQL
implementation: its prototype lacks range queries and targets short one-shot
transactions. Its optional lock-based fallback is deliberately outside this
single-mode candidate.

Certification can be conservative, but coarse scope is not harmless. If N point
transactions read and increment **different rows of one table** from the same
cut, validating a single table generation accepts the first and rejects N−1.
Every serial order of those operations is valid. That false-conflict cost arises
even without broad transactions; a table validator followed by a database-wide
fallback can manufacture a global stall problem.

Any coarse validation baseline must cover all semantic dependencies, bump
generations for every relevant logical change without ABA, compare against all
earlier accepted writes, and apply exact mutations rather than unchecked stale
roots. More precise validation may earn its cost, but it belongs in this
candidate's mechanism budget rather than being presumed a small detail.

Batch membership, certification and outcomes must agree across consumers.
Independent shard epochs do not supply a shared cut or coordinated batch
publication automatically. A batch can carry many events; this proposal does
not require an individual witness entry for every speculative retry.

### Why the hybrid is a comparison candidate, not the starting recommendation

Optimistic execution followed by an exclusive turn retains both certification
and exclusive ownership. It also introduces a transition: close admissions,
finish irreversible outcomes, reject remaining attempts, and fence their delayed
messages/results before later admission. Merely stopping admission cannot drain
the current partially locked C1 population if it is already deadlocked. Fencing
the fallback owner alone does not fence obsolete optimistic attempts.

The pause includes discovery and state-dependent computation, not just commit.
Under continuing writes, a broad transaction may pay for a doomed optimistic
attempt before almost every pause. A direct ordered lane avoids that wasted
attempt; the hybrid must demonstrate enough ordinary-work benefit to repay its
additional machinery. If pauses are unacceptable, finite terminal contention
failure is a simpler honest alternative, with completion under contention no
longer promised.

### What none of these deletes

All candidates still need durable outcomes, multishard atomic visibility,
recovery, old-owner fencing and sufficient retained state for the reads they
promise. Coherent historical cuts and common certification/order are newly
required mechanisms where the current design has not supplied them. C1/C2 as
distinct *lock-based epoch classes* need not survive; durable staging and an
irrevocable outcome still have jobs to do, whatever the names.

A timeout can bound work by failing a transaction; it cannot promise both
completion of arbitrary slow work and a short pause. Fairness does not create
capacity under overload. These are costs to accept or grounds to reject a
candidate, not invitations to restore components, fine-grained gates and partial
salvaging one exception at a time.

## Many writes without many retained locks

Consider `UPDATE accounts SET balance = balance * 2`. One ordered program can
represent the logical mutation. A following increment must apply to that result,
or the multiplication must incorporate an earlier increment. A private result
computed before the increment cannot simply overwrite it. If `RETURNING`, a
check constraint or a trigger depends on intermediate values, those observations
belong to the program's ordered evaluation as well.

For producer-supplied IDs and values, a blind multirow assignment can be ordered
without read protection. Exact deltas can sometimes combine across orders. An
explicit snapshot job can build and publish its owned output generation without
freezing the historical source. Output ownership alone does not make arbitrary
source-read-plus-publication SQL serializable: a source writer reading the old
publication marker can close a dependency cycle. None of these imply that an arbitrary
old table image can replace a live table without preserving intervening writes.
An intentional authoritative replacement is a different operation from updating
some rows while preserving the rest.

The unavoidable questions are which logical state the program uses, when all of
its effects become visible, and what subsequent operations must observe. Row
locks, root swaps and operation records are different representations of work
around those questions; a compact representation is not itself a solution to
all three.

## What has actually been checked

[histories.py](histories.py) enumerates serial executions for 17 authored histories,
checking the declared observations and final state, with one explicit real-time
precedence case. [Retained output](evidence/histories.json) records the source hash,
177 enumerated orders, eight admissible histories and nine inadmissible ones.
Positive and negative histories are intentional controls, not pass/fail scores
for a candidate implementation.

| Comparison | What the finite check establishes |
| --- | --- |
| Select maximum plus independent insert / insert also reads action marker | Broad overlap can have a serial witness; one added dependency removes it. |
| Blind bulk assignment / assignment returning an earlier value | Equal final state is not enough to preserve observable behavior. |
| Stale root replacement / ordered bulk program | Atomic replacement can lose independent updates; executing the program at its serial position can preserve them. |
| Whole bulk transaction / reader between published chunks | Chunked visibility can violate the original transaction contract. |
| Exact deltas / deltas returning intermediate values | Algebraic freedom depends on outputs, not just final counters. |
| Coherent old report / torn cross-shard report | A broad historical read can overlap writers; arbitrary local snapshots need not form a coherent read. |
| Four disjoint point increments | All 24 serial orders work; table-level validation would introduce false conflicts. |

Run from the repository root:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/reconsideration/histories.py
```

This is an operation-level oracle, not a simulator of validation, admission,
failure or costs. It does not establish Orbital's stronger fixpoint requirement:
consumers must agree on the selected outcomes and protocol metadata, not merely
each obtain some serializable history. The existing key-protection scheduler
cannot measure these alternatives by switching off read edges.

The [localization comparison](LOCALITY.md) now takes precedence: unrelated local
writes in the same shard must progress while a cross-region transaction waits.
Compare scoped ordering/protection and optimistic whole-attempt policies; retain
shared lanes/batch barriers only as negative controls. Include queued x/y bridges,
disjoint rows sharing metadata, broad reads/writes and expanding discovery. Keep
execution and publication delay separate, and measure backlog recovery alongside
discarded work and broad-transaction completion. No synthetic throughput result
is offered before these obligations are modeled.
