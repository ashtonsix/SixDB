# Large writes without large retained protection

This is a design probe for Ashton's 2026-09-25 reconsideration, not an amendment
to the [then-current brief](../../../../orbital/stale-drafts/BRIEF-arbitration.md) or a measured implementation. It
challenges the retained-lock premise used by the current scheduler. Its
conclusion is narrower than “add multiversioning”: **many physical writes can
share one logical ordering/publication decision, but an arbitrary broad SQL
mutation cannot always finish independently of concurrent small mutations.**
The useful simplification may be to accept that obstruction explicitly.

## What “protect all the rows” conflates

A million-row mutation has several potentially very different sizes:

| Quantity | Can it be small while a million rows change? |
| --- | --- |
| Private output construction | Yes: no live row needs a lock merely because a worker builds an unpublished replacement page. The bytes and CPU are still real. |
| Logical description | Sometimes: one range deletion or an ordered transformation can describe many effects. An arbitrary per-row result may require a million values. |
| Live dependency on prior state | Sometimes: producer-supplied blind assignments have fewer dependencies than `UPDATE … SET v=f(v) WHERE predicate`, joins, or constraints. |
| Atomic visibility decision | Yes: readers can select a committed generation or transaction outcome. This does not make all its payloads available or make its construction valid. |
| Work required before success is known | Not generally: uniqueness, joins, cascades, errors and requested outputs can require processing the full mutation. |

Retaining a lock on every output row throughout its construction is one
implementation, not an atomicity requirement. Conversely, a small commit record
does not make conflict detection, availability, or the data volume small.

“Blind” must mean independent of database reads under the full transaction's
semantics. `SET balance=100` is blind only if predicates, constraints, triggers,
index maintenance and outputs do not introduce dependencies. Idempotence is
different: applying the same assignment twice is harmless in isolation but can
overwrite a later legitimate assignment. A transaction/event identity is still
needed to distinguish replay from new work. Integer addition may commute but is
not idempotent.

## Workload probes

These are authored workload cases and counterexamples. The alternatives are
not interchangeable contracts. “Snapshot job” below means the user explicitly
accepts a result for a stated input cut; it does not silently weaken an ordinary
read/write SQL transaction.

| Workload and variation | Why the set becomes large | Simpler route and what it gives up or must preserve |
| --- | --- | --- |
| **1. Broad price correction:** `UPDATE products SET price=price*1.1 WHERE region='EU'` while checkout and point edits run | Reads determine membership and every new price; secondary indexes may multiply physical work | One ordered transformation can avoid a million lock entries. A later checkout must see the transformation at its logical position, possibly paying deferred evaluation. Precomputing old prices and overwriting live ones loses point edits. Committing chunks changes atomic visibility and potentially which rows satisfy the predicate. |
| **2. Retention deletion:** delete events older than a cutoff | Millions of existing rows, perhaps no dependence on their payloads | A versioned range tombstone can encode an ordered deletion without eagerly rewriting each row. Later inserts must be distinguished from rows present at the deletion's position. Cascades, triggers, exact deleted-row outputs, or a predicate not represented by the range can remove this shortcut. Chunking means partial deletion is visible. |
| **3. Tenant erasure:** delete a tenant and all its dependent objects | A small initiating delete reaches many tables through foreign keys, blobs and indexes | A tenant generation/liveness record can immediately make the tenant inaccessible, with cleanup later, **if that is the promised contract** and every access path observes it. This is not automatically equivalent to synchronous SQL cascades, deletion callbacks, or physical erasure. Cross-tenant references destroy a tenant-only boundary. |
| **4. Append-only ingestion:** bulk insert immutable events with independent IDs | Huge physical input, little or no shared target state | Build immutable batches and publish their inclusion. Existing rows need not be protected. Duplicate IDs, source transaction atomicity and uniqueness still require a decision. Many independent writers contending on one root/counter is a representation bottleneck, not inherent conflict between event payloads. |
| **5. `INSERT SELECT`:** create a daily analytical partition from changing orders | Broad source reads plus broad destination writes | A new, exclusively owned destination generation can be built from a coherent source cut while the source moves. An “as of watermark W” contract makes this natural. An ordinary transaction that also reads or writes shared live control state can have cycles with source writers; a snapshot alone does not license its publication. |
| **6. `MERGE` / upsert:** synchronize a customer dimension from a staging feed | Match/non-match decisions, target values, uniqueness and possibly deletions for rows absent in the source | Authoritative replacement of a feed-owned table is simpler than reconciling arbitrary live writes. Otherwise execute the merge against its chosen serial state; a prebuilt patch can be stale. Duplicate source matches and concurrent insert decisions are semantic work, not only lock scheduling. PostgreSQL explicitly distinguishes `MERGE` from `ON CONFLICT`; duplicate target modifications can fail. [MERGE](https://www.postgresql.org/docs/18/sql-merge.html) |
| **7. CDC with duplicates and corrections:** replay events and repair old source values | Many target keys; duplicates, out-of-order events, delete/reinsert, changing keys | If a source owns a target key and supplies a usable total event order, per-key sequence-aware replacement plus deduplication can avoid wide retained locks. A stale correction must not beat a later event; a duplicate delta must not apply twice. Multiple sources, local target edits, or preserving multi-row source transaction atomicity need an explicit reconciliation/commit boundary. |
| **8. Snapshot catch-up while CDC continues** | A table scan races streamed updates to the same keys | A deliberate chunk-and-reconcile ingestion protocol can avoid one table-long transaction, but its progress state and collision handling are not free. Debezium uses chunk windows/watermarks and deduplicates snapshot/stream collisions. This is evidence that the workload can be decomposed, not that arbitrary SQL can be split invisibly. [Debezium](https://debezium.io/documentation/reference/connectors/postgresql.html#postgresql-incremental-snapshots) |
| **9. Computed-column backfill or index build** | Every old row contributes output while live writes produce newer output | Build privately and pause writers for the whole operation for a simple exact baseline. Online build instead needs change capture/dual maintenance, catch-up and a publication boundary. PostgreSQL's concurrent index build performs two scans and waits for relevant transactions; it can leave an invalid index on failure. “Online” has not removed coordination. [CREATE INDEX](https://www.postgresql.org/docs/18/sql-createindex.html#SQL-CREATEINDEX-CONCURRENTLY) |
| **10. Partition refresh / table replacement** | A whole partition's physical contents change | For an authoritative replacement, publish a new generation; old readers retain their generation. If concurrent writes must survive, either carry them forward, reject/rebuild, or pause admission. Blindly swapping a stale whole root is not a correct implementation of a partial update. Per-partition publication changes a whole-table atomic refresh into partial visibility unless readers use a shared catalog generation. |
| **11. Materialized-view refresh:** rebuild revenue by customer | Broad reads and output even though base rows need not change | A result explicitly current to an input watermark can have one generation publication and tolerate staleness. “Always current after every source commit” needs synchronous maintenance or an overlay that incorporates pending changes. PostgreSQL offers refresh concurrent with readers, still allowing only one refresh per view; this is a useful, explicit scope limit. [REFRESH](https://www.postgresql.org/docs/18/sql-refreshmaterializedview.html) |
| **12. Incremental aggregates:** millions of facts update shared totals | Physical fan-out and logical hotspots at shared summaries, even from small point writes | Exact count/sum deltas can be folded; correction events need before/after effects and replay deduplication. `MIN`, distinct counts, top-k and deletions are different. Returning each intermediate total, bounds, numeric overflow or floating-point grouping can invalidate a commutativity shortcut. The [aggregate spike](../../aggregate-maintenance/README.md) also warns that fewer logical adjustments need not mean lower construction cost. |
| **13. Unique/FK constraints and cascades:** import many accounts; change a parent key | Disjoint primary rows can still collide on a unique value or reference a parent being deleted; one parent update can fan out | New generation ownership does not authorize broken live constraints. A coarse ordered execution position can evaluate the entire invariant without fine locks, accepting serialization. Deferred constraints move when the decision is made; they do not remove it. PostgreSQL's documented cascade and deferred-check behavior makes this semantic surface concrete. [Constraints](https://www.postgresql.org/docs/18/ddl-constraints.html) |
| **14. Payout or inventory rebalance:** select eligible accounts, reserve funds, write thousands of ledger entries | Aggregate budget, eligibility and all-or-nothing business effects bind otherwise separate keys | A single ordered database transaction can prepare private output and publish it atomically. A job of independent payments is a valid simpler contract only if partial completion, reconciliation and idempotent resumption are accepted. External payments cannot be made rollbackable merely by a database root swap; the database can atomically record instructions whose later dispatch is a separate workflow. |
| **15. Repartition / migration:** move or re-encode a large live table | Huge physical copy, possibly no logical row changes | Preserve logical identities and versions; this should not require a million SQL write conflicts merely because representation changes. Cutover still requires routing/ownership coherence and retention of representations needed by readers. Capture/replay of concurrent changes or a write pause is required if copying mutable data; a lost or twice-applied change breaks the migration. |
| **16. Blind multi-key assignment or delta batch** | Producer already supplies every key/value or contribution | Order one logical batch, retain its output privately, and make its commit visible atomically. Same-key blind assignments need a defined order; they do not inherently require failed retries. Commutative completion-only deltas can permit more schedules. A batch containing “debit only if nonnegative” is not such a blind operation. |
| **17. Mutation with `RETURNING`, triggers, audit or generated IDs** | A superficially simple write exposes intermediate values or performs extra reads/writes | The transaction's output and errors must agree with its chosen order, not only the final table. PostgreSQL returns values for actually updated rows and its count can be affected by triggers. Omitting this surface makes a broad-write probe artificially easy. [UPDATE](https://www.postgresql.org/docs/18/sql-update.html) |

## Can we order a write instead of protecting its materialized rows?

Yes, for some important cases. Suppose a batch carries fixed IDs and integer
deltas, imposes no bound checks, and returns only completion. Its data can be
durably recorded as one logical operation. Applying its contributions to pages
can happen later, and compatible operations can combine. The ordering and
visibility metadata can be much smaller than its physical effects. FoundationDB
provides a concrete precedent for distinguishing atomic mutations from ordinary
read/modify/write: its atomic operations do not add read conflict ranges.
[FoundationDB developer guide](https://apple.github.io/foundationdb/developer-guide.html#conflict-ranges)

For general SQL, an ordered **program** is still useful, but it carries an
evaluation obligation. `DELETE WHERE P` means the rows satisfying P at the
operation's serial position, not every future row that ever satisfies P. A
following point read/write must observe the earlier program's effects. It may
evaluate that program locally if its dependencies are local; a join or aggregate
can require remote work or the completion of the earlier operation. An exact
row count or `RETURNING` can require full logical evaluation before replying.
Secondary indexes must answer the same logical state, whether updated eagerly
or corrected from the pending program.

This does **not** imply that physical compaction and replication of every
rewritten page must finish before later writes begin. It does imply that moving
work to an overlay transfers work to reads, conflict checks and compaction.
Unbounded pending programs create their own latency/space failure mode. A small
program plus a large unknown outcome cannot be acknowledged as a successful
SQL mutation before possible constraint or arithmetic failures are resolved.

The strongest simple candidate here is ordinary ordered execution with delayed
physical materialization where the representation supports it. A new general
language of semantic certificates, predicate rewrites and lazy query fragments
would be a substantial additional mechanism; this probe does not recommend it.

## Root publication: two tiny counterexamples and a scope limit

**Lost independent update.** Initially a table root represents `x=0,y=0`.
U constructs a replacement root to implement `SET x=1`. W commits `SET y=1`.
Publishing U's old-base root produces `x=1,y=0`. Neither serial order of those
two partial updates gives that result: both give `x=1,y=1`. The root swap must
compare its base, preserve W's changed subtree, reconstruct, or execute at an
order where W subsequently builds on U. Atomic pointer replacement alone is
insufficient.

**Stale arithmetic.** Initially `x=0`. U precomputes `x+10` as 10 and W commits
`x+1` as 1. Publishing U's result as 10 loses W in either serial order. Publishing
the operation `+10` against W's state yields 11. This works because these exact
operations compose; it does not license rebasing a conditional, joined or
constrained update in the same way.

**Intentional replacement is different.** “Replace table T with this supplied
authoritative image” may validly overwrite earlier writes when ordered after
them. “Apply these changes to T while preserving other writes” may not. Treating
the former as an optimization of the latter quietly changes the contract.

Iceberg illustrates both sides: immutable snapshots and an atomic metadata
pointer enable table publication, while concurrent writers still check
assumptions and may retry. Its overwrite API requires conflict validation and
warns about resurrecting concurrently deleted rows. This is not a free general
cross-table SQL commit primitive. [Reliability](https://iceberg.apache.org/docs/latest/reliability/),
[OverwriteFiles](https://iceberg.apache.org/javadoc/latest/org/apache/iceberg/OverwriteFiles.html)

A table root also makes disjoint row writers compete at one publication point.
That may be an acceptable short serialization step, but it must be charged and
batched if appropriate. Splitting roots reduces the hotspot while reintroducing
a multi-root atomic visibility question. A shared transaction descriptor or
commit frontier can answer that question, but readers, caches, failover and
garbage collection must all honor it. A pointer is not itself a distributed
publication or recovery protocol.

### Publication and recovery work that cannot be discarded

Before a successful outcome becomes durable, the system must have durably
retained the inputs/results required to reconstruct that outcome under its
failure contract. If a committed generation has not reached a shard yet, a
reader may wait or fetch it; substituting that shard's old generation would
break atomic visibility. A logically complete operation need not already have
its final compacted pages, but it must have a valid recoverable representation.

A crash before the outcome leaves private work uncommitted. A crash after it
must resume installation or replay without applying additive changes, audit
rows or logical trigger effects twice. Replaying a committed blind assignment
out of order must not roll back a later value. The same distinction matters for
repairing a live replica: copy an agreed base and replay changes in the required
logical order, rather than treating a late copy as the newest write. External
effects need their own delivery contract; transaction replay is not permission
to repeat them.

Primary rows, secondary indexes and required derived state must describe the
same visible outcome. A deferred index implementation must account for pending
changes on its read path; otherwise an indexed query and a table scan disagree.
This amplification can come from one small logical mutation and does not by
itself justify a broad *logical* conflict scope. Finally, reclaiming an old root
must account for snapshot readers and unfinished work that still needs it.
These obligations remain in all three alternatives below; they should be
shared publication/recovery machinery, not rediscovered in every job type.

## Three whole alternatives worth comparing

These are deliberately coarse baselines. They are complete at the level of
transaction admission, ordering and failed-attempt disposition; they are not
claims of a finished distributed wire/recovery protocol.

### A. Conservative optimism, then an exclusive turn

Prepare against a coherent committed snapshot without retained read locks.
Build private output. At an agreed commit position, conservatively validate
the versions of every read table, including reads used by constraints and
discovered work. Reject on any change; precise predicate validation is optional
optimization, not required for correctness. Blind writes can be ordered at
commit, subject to any actual constraint reads. Table-level validation deliberately
accepts false conflicts to keep the baseline small.

After the permitted retries, put the transaction into a fair, durable fallback
queue for one fixed coordination domain. Stop admitting normal read/write work
in that domain, finish or discard the remaining speculative attempts at a
defined boundary, then rerun the head transaction from the resulting stable
state and publish its result. Release the turn after its durable outcome.
Existing snapshot readers can continue on retained state; requests requiring
newer results wait. Do not salvage partially locked preparation or select a
winning connected component.

This retains the SQL atomicity/isolation contract but trades availability and
tail latency for progress. The paused interval includes irreducible query and
constraint evaluation, not merely a presumed short “final commit.” Work on
immutable inputs may be reused; state-dependent work is recomputed. If dynamic
discovery can reach anywhere, the simple safe domain is the whole database.
A tenant/table domain works only with an enforced boundary; expanding domains
on demand risks recreating the component mechanism.

Strongest failure: repeated expensive fallbacks dominate the system. Large
transactions get progress by delaying innocuous small ones explicitly. A crash
or unavailable participant must leave an agreed recoverable outcome/turn;
independently timing out and releasing different shards is unsafe. This is a
much simpler policy only if that broad stall is an accepted product tradeoff.

### B. One ordered mutation lane; parallel snapshot readers

Give every read/write transaction one agreed position within a fixed domain.
Execute its logical decisions against the preceding committed state; later
read/write transactions wait until those decisions are complete. Dynamic
discovery needs no protection acquisition because a later writer cannot change
the logical input underneath it. Parallelize work **inside** a transaction and
delay physical application when subsequent operations can interpret the same
logical state. Independent snapshot readers run on committed versions.

No normal conflict retries, arbitration, lock retention, fallback mode or
distributed conflict graph is needed. Failed operations get an agreed failure
outcome, then the next operation proceeds. Admission order and all errors,
outputs and nondeterministic inputs must be agreed so consumers meet Orbital's
fixpoint requirement. The existing per-shard order does not magically supply a
single order for arbitrary cross-shard programs: that stronger ordering scope
is part of this alternative's cost.

Strongest failure: a slow join, unavailable participant or user computation at
the head stops later mutations across the domain. This can be unacceptable for
HTAP even if all cores and storage bandwidth parallelize its execution. Adding
general out-of-order dependency scheduling would compromise the intended
simplicity; any commuting fold is an optional demonstrated optimization.

This baseline tests whether the user prefers predictable queueing and a small
mechanism over speculative concurrency. It also tests whether the expensive
part of broad jobs is logical evaluation or merely physical materialization;
only the latter can move safely behind their logical completion without more
concurrency machinery.

### C. Bound live transactions; make broad ETL generation jobs

Admit bounded short SQL transactions, with a simple serialized turn available
for their failed retries. Reject work exceeding the live-transaction limit.
Broad jobs consume explicit snapshots/watermarks, write private generations,
and publish into outputs they own; per-key ingestion jobs checkpoint bounded
chunks. Operations needing an exact broad change to mutable live tables require
an explicit exclusive maintenance turn, or are unsupported by this contract.

This removes the promise that every long query-plus-update can run as one
concurrent general transaction. It is therefore a product tradeoff, not merely
an implementation technique. FoundationDB and its Record Layer demonstrate a
real system making explicit transaction size/time limits and moving longer work
into surrounding machinery; their exact limits are not proposed for SixDB.
[FoundationDB paper](https://www.foundationdb.org/files/fdb-paper.pdf),
[Record Layer paper](https://www.foundationdb.org/files/record-layer-paper.pdf)

Strongest failure: users bought HTAP+ELT to avoid orchestrating separate systems,
and this boundary can move complexity into job APIs, application recovery and
freshness management. A payout that truly needs whole-batch atomicity cannot be
silently converted into a resumable set of individual payments. Multiple jobs
sharing mutable targets also reintroduce conflict resolution unless ownership
or maintenance admission is explicit.

## Mechanisms removed, obligations retained

| Mechanism / obligation | A: exclusive fallback | B: ordered mutations | C: bounded live work + jobs |
| --- | --- | --- | --- |
| Incremental retained C1 read/predicate locks | Removed | Removed | Removed for jobs; short core still needs its chosen isolation implementation |
| Connected-component collection, merge and authority | Removed | Removed | Removed |
| Provisional union reservations | Replaced by one fixed-domain turn | Removed; order itself excludes later logical mutations | Removed; explicit ownership/maintenance turn |
| Yield requests and transitive partial-preparation invalidation | Removed; reject/recompute whole attempt | Removed | Removed from general jobs; checkpoint/recovery belongs to job contract |
| Retry scheduling | Small bounded optimistic phase plus FIFO turn | Removed for conflicts | Bounded short core only |
| Version/read validation | Coarse and conservative on normal path | No optimistic validation against later writers | Explicit job cut/ownership; bounded core validation |
| Atomic publication, durable outcome, replay deduplication | Retained | Retained | Retained at each declared publication/chunk boundary |
| Old-version retention and reclamation | Retained for snapshots | Retained for snapshots | Retained for job inputs/reader generations |
| Arbitrary concurrent large read/write SQL | Preserved semantically, stalls others after retries | Preserved semantically, always queues other writers | Intentionally restricted |

The discriminating tests should keep semantics fixed before measuring: a broad
read-dependent update with independent point writes; the same update with
commutative deltas and then `RETURNING`; an authoritative refresh versus a merge
that must preserve concurrent writes; and a backfill with writes arriving
throughout construction and at cutover. Charge blocked time, discarded work,
retained bytes and work moved to reads. A root swap or compact program that
omits constraints, outputs, recovery or later-read costs has not simplified the
same operation.
