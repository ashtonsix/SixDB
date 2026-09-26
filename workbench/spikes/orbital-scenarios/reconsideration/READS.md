# Large reads: which promises actually need contention handling?

2026-09-25. Read-side investigation for Ashton's request to reconsider preparation
and contention from the ground up. These are authored workload cases and design
alternatives, not measured results or selected architecture. The
[then-current brief](../../../../orbital/stale-drafts/BRIEF-arbitration.md) and [protection question](../PROTECTION.md)
are the candidate being challenged.

The broad-reader problem is not principally how to represent a large read set.
It is whether a long computation must keep participating in the changing live
database. A mechanism that supports every combination of arbitrary discovery,
fresh decisions, unbounded duration and guaranteed progress has taken on a much
harder job than a snapshot query engine. Narrower read certificates can help
particular queries, but making every query prove its smallest dependency set
would be another major mechanism, not a simplification.

## Keep the contract fixed before comparing mechanisms

These distinctions describe the workload; they are not proposed new protocol
states or a mandatory decomposition of C1.

- A historical report needs a coherent view and access to its versions. It does
  not need its source to remain current throughout the report.
- A general serializable updating transaction needs an admissible order for
  its observations, writes **and outputs** relative to other transactions.
  Overlapping writers may sometimes be placed after it without waiting for its
  computation. A snapshot by itself does not establish that order.
- A promise about the live state at publication or throughout an interval is
  stronger. Exact current selection and continuous stability are different
  promises; neither follows from an old snapshot.
- A snapshot query followed by a new command is two transactions. It is useful,
  but it does not preserve the semantics of an arbitrary original transaction.

General SQL and multishard transactions remain in scope. The alternatives below
must either implement them or name the lost progress/latency guarantee. They
must not silently turn general SQL into snapshot-then-command.

## Concrete HTAP and SQL/ELT cases

The examples intentionally include small outputs with huge reads, huge outputs
with simple reads, and dependencies on rows that do not exist. SQL is illustrative;
no particular dialect or current SixDB implementation is assumed.

| Case and precise operation | What must stay consistent; why the set grows | What can be removed, and what would change the contract |
| --- | --- | --- |
| **1. Operational dashboard.** Join orders, payments and fulfilments; group by region; display totals while OLTP continues. | All query workers must use one coherent cut. Reading only local shard epochs can double count or miss a cross-shard transfer. Long duration grows version-retention and resource costs, not an intrinsic need to exclude writers. | Retained source read locks can go if a suitable snapshot is provided. Switching to independent per-partition cuts changes the answer's meaning. |
| **2. Multi-query finance report.** Compute header totals, then stream detail rows over minutes. | The two statements and the pagination must share a cut; otherwise the displayed total and detail can disagree even if every statement was individually coherent. | No need to freeze live accounts. Reopening a fresh snapshot per page is a semantic tradeoff, not merely streaming implementation. |
| **3. Exact maximum, historical report.** `SELECT id, value FROM items ORDER BY value DESC, id LIMIT 1`. | The selected row, its eligibility and tie order at the chosen cut. Access may scan millions of rows; the result is one row. | Snapshot semantics remove current read exclusion. A deterministic tie rule matters if the identity is returned or used later. No promise that the item stays largest. |
| **4. Exact maximum with a database action.** Select the largest delinquent account, run expensive scoring, insert an action referring to it. | If this is one serializable transaction, its selection and action need one serial witness. Some concurrent changes can be placed after it. Changes to tables the scoring reads create additional dependencies. | An answer-preserving change to a losing row need not logically invalidate the result, but proving that for arbitrary SQL may cost more than a conservative check. Splitting selection from the action changes the contract. |
| **5. Exact priority queue versus eligible work queue.** Select highest-priority ready job and claim it. | Exact priority depends on the absence of a higher-priority eligible job. Claiming any eligible job only depends on the chosen job still being claimable. | Dropping exact priority can collapse a global/range dependency to one claim, but allows priority inversion and potentially starvation. `SKIP LOCKED`-like behavior must not be presented as exact priority. |
| **6. Top-k, percentile or rank-dependent awards.** Award credits to the top 1% of customers and return all ranks. | Boundary membership, ties and often population size matter. A new customer can change the percentile cutoff; deleting a winner promotes another. Exact ranks can change while the winning set stays unchanged. | Historical awards can name a fixed campaign cutoff. “Top 1% at publication” cannot be substituted silently. Tracking only chosen rows misses new qualifiers. |
| **7. Join-based invoice generation.** `INSERT INTO invoices SELECT ... FROM orders JOIN price_rules ... JOIN tax_rates ...`. | Hundreds of thousands of source rows may depend on a small shared rule table. Preserve a coherent source cut and define whether prices are historical or current. Returning invoice IDs and totals are observable effects. | A frozen billing period/ruleset removes ongoing source contention if that is the business contract. Current-price invoicing is not equivalent. Chunking may expose a partially billed period unless publication is separately atomic. |
| **8. Anti-join cleanup.** Delete customers for whom `NOT EXISTS (SELECT 1 FROM orders WHERE orders.customer_id = customers.id)`. | A new matching order matters even when the original read found nothing. Referential integrity may independently forbid deleting an attached customer. | Locking/checking only existing order rows is insufficient. A parent-row convention could concentrate the constraint but requires every writer to participate; it is a schema/protocol choice. Snapshot export of orphan candidates plus later checked deletes changes atomic whole-job semantics. |
| **9. Deduplication and “not yet processed.”** Insert an event only if no event with this deduplication key exists; or choose a canonical customer by fuzzy match. | Exact-key uniqueness can have one narrow logical conflict even if a bad plan scans a table. Fuzzy or configurable matching can have a genuinely broad absence predicate. | A real unique constraint can eliminate application scan/then-insert for exact equality. Treating fuzzy matches as exact keys changes semantics; building a summary index adds maintenance obligations. |
| **10. Capacity or risk threshold.** Read `SUM(exposure)` over a portfolio and accept a new trade if the limit is respected. | All increases that can consume the same remaining capacity interact even when they write distinct trades. Narrow row writes do not make the decisions independent. | A maintained counter changes representation, not the shared dependency. Preallocated per-partition allowances restrict flexibility and may reject a globally feasible trade; that explicit tradeoff can simplify admission. |
| **11. Cross-row safety rule.** At least one clinician must remain on call; each transaction reads the roster and removes itself if someone else remains. | Two small writes based on the same coherent old roster can jointly violate the invariant. This is a read/write dependency problem with no write/write overlap. | Snapshot-only updating is insufficient. Serial execution preserves the rule; an aggregate guard can also serialize the decision but does not remove its contention. |
| **12. Dynamic graph traversal.** Follow account links, ownership edges or a bill-of-materials graph; mark all reachable nodes, move a subtree, or approve based on reachability. | The discovered shard and row set depends on earlier values. Edge deletion can invalidate a path; insertion can introduce previously unseen reachable nodes. A negative reachability answer may inspect a huge region. | Read-only traversal can use a cut without retaining locks along the path. A transaction that changes the live graph based on that traversal still needs ordering. Declaring the full component in advance is often tantamount to doing the computation. |
| **13. Authorization joins and revocation.** Read memberships, role inheritance and row filters before exporting data or changing many rows. | Define whether authorization is assessed at query start, at commit, or continuously. A role revocation during a long export makes these contracts observably different. | A start-time authorization cut can avoid continuous protection if accepted. A database commit cannot retract bytes already delivered externally; live revocation requirements are not solved by transaction rollback. |
| **14. Feature extraction and model training.** Join an evolving event history with labels and dimensions, train for hours, then publish a model ID. | Reproducible training needs named data/rules versions. The model may be valid precisely because it describes an older cut. Publication can require only its own catalog rules. | Source protection through training is unnecessary under that contract. Requiring the model to reflect all data current at publication turns an offline job into a perpetually moving target. A fraud decision made from the model has separate freshness requirements. |
| **15. CDC reconciliation.** Compare source and target, then insert missing rows or delete target rows absent from source. | “Absent” depends on a source cutoff, late arrivals, deletion semantics and source/target identities. A coherent database snapshot does not establish completeness of an external feed. | A named closed input batch makes the intended population explicit. Advancing a watermark alone does not prove late data impossible. Per-key reconciliation can give eventual convergence but loses all-at-once replacement semantics. |
| **16. Warehouse rebuild or materialized projection.** Read a large source snapshot into a new relation, then make the relation visible. | Atomic visibility of the **derived relation** need not require freezing sources. Readers must see a complete chosen generation, including its source-cut identity. | Building an immutable generation and publishing a reference removes source locks under historical-projection semantics. It does not preserve “projection exactly equals current source now,” nor automatically merge concurrent edits made directly to the old projection. |
| **17. Selection drives a mass update.** Raise salaries below the departmental average, or discount the cheapest 10% of inventory. | The source predicate, aggregate and writes belong to one operation if exact SQL transaction semantics are required. Returned affected-row counts or `RETURNING` values distinguish outcomes even when eventual totals match. | A historical list plus conditional row updates is a different operation: rows can be skipped or changed between selection and action. Chunking changes atomicity. Snapshot reading alone cannot remove write/publication coordination. |
| **18. Cross-shard pointer chasing.** Read `A.i`, use it to select shard B, read `B.j`, then fetch `A[k]` discovered from B. | Every read must belong to the chosen history, including a shard first discovered late. Independently choosing the freshest local version does not form a coherent cut. | Retained locks on every traversed object can be replaced for a snapshot computation, provided all newly discovered shards can serve that same cut. This shifts the hard part to frontier construction and retention; it does not make it disappear. |
| **19. Physical granularity or escalation.** A query needs ten rows scattered across ten large blocks; protection is recorded per block, partition or table. | The logical dependencies can be tiny while the exclusion or validation scopes include thousands of unrelated rows. Escalation bounds metadata by widening the represented set. | Finer representation can remove these false conflicts without changing SQL semantics, but costs memory/work. A compact exact range is different: a million inserts into that range may really affect the query. Do not confuse fewer records describing protection with less protected data. |
| **20. Long session or slow UDF.** A client reads a broad set, waits for user input or an external service, then updates one row. A worker may disappear entirely. | The lifetime, not only cardinality, determines how much intervening work intersects the set. Several individually modest sessions can retain overlapping dependencies indefinitely. External responses used in an agreed result also require agreed inputs. | Whole-attempt timeout/failure removes an unbounded progress promise. Keeping the session on a historical cut removes read exclusion but still needs an explicit retention limit or durable materialization policy. It cannot quietly become a fresh current update when the client returns. |
| **21. Plan and runtime variations.** The same parameterized query alternates between a selective index seek, a sequential scan and a spill-heavy join as data changes. | A plan-driven lock footprint can widen despite an unchanged logical query. A slow spill or skewed join increases lifetime; data-dependent successors increase its discovered extent. | Semantically equivalent plans should not redefine isolation. Conservative plan-based protection can be accepted as a cost, but tuning one fast plan is not evidence that the mechanism handles the workload generally. |

These cases have further adversarial variations: shared tenants versus isolated
tenants; indexed versus scan plans; one huge reader versus many overlapping
readers; read-only versus one final marker write; bounded versus recursive
discovery; externally supplied UDFs; inserts into an empty predicate; and
high-rate changes that leave an aggregate unchanged. Comparing plans must not
mistake their different access footprints for different SQL promises.

Two dimensions therefore matter: **how broad** the dependency or exclusion is,
and **how long** it lives. At high arrival rates, even a small retained set can
meet many transactions over time. A bounded arbitration snapshot cannot describe
the eventual set of transactions a long interval would encounter. Conversely,
a huge read of a closed historical generation can have no live exclusion at all.

## Small histories that separate the promises

**A broad read can create a star, not a serialization cycle.** R reads all N
keys at a coherent pre-update cut and performs only a report; N independent
writers each replace one different key while R is open. R before all writers
is a serial witness. In an updating variant R additionally writes a fresh
report record that none of the writers reads. The same witness exists. The
undirected read/write component has N+1 members, but the N writers are mutually
compatible. Component size, retained read exclusion and an oldest-winner policy
are separate costs.

**One back edge defeats that argument.** R chooses `a = 100` as maximum and
writes `chosen = a`. W reads `chosen` as empty, inserts `b = 101`, and both
commit with those observations. R before W contradicts W's read; W before R
contradicts R's maximum. The problem is not the number of rows. The two
observations and two effects cannot all be preserved.

**Absence can carry the entire dependency.** Initially there is no active
campaign. T and U each observe that predicate empty, then insert a different
active campaign. If the rule is at most one active campaign, neither serial
order can produce both successful observations and inserts. A checker that
tracks only initially present keys sees no read set and misses the conflict.

**A fixed aggregate is not fixed inputs, and fixed final state is not fixed
outputs.** If an exact-integer `SUM` is the only observation, an atomic transfer
of +5 and -5 within its group leaves that observation unchanged. A conservative
row-version check can nevertheless reject it. Conversely, transactions that
increment a counter and return its previous value can leave the same final
counter under different orders while returning different values. Optimizing
either case requires retaining the actual declared semantics, including outputs;
it is not an argument that all sums, SQL types or UDFs commute.

**A distributed cut is part of the experiment, not free input.** With a transfer
from A to B, a read of A after the debit and B before the credit observes a
state that never existed atomically. A vector of locally completed epochs is
not automatically a valid cross-shard transaction cut. An initial comparison
may author a valid cut, but must label that an assumption and count retention
for shards discovered later. One snapshot needs the historical values it reads,
not necessarily every intermediate version produced while it remains open.

## Three simpler whole approaches worth confronting

The attraction of each candidate is something it deletes. None removes atomic
publication, agreed outcomes or failure recovery. These are alternatives to
evaluate, not features to combine into one mechanism.

### 1. A coarse serial fallback, with the convoy accepted

After ordinary attempts fail, restart the **whole** transaction in an agreed
exclusive execution interval over a conservative domain. Run its discovery and
execution with that domain stable, publish atomically, then let subsequent work
run. Use a FIFO or otherwise agreed order, not an optimization over a conflict
component. If domain membership cannot be established safely before execution,
the genuinely simple version takes the whole database.

This could remove component construction, distributed arbitration, provisional
component reservations, partial-yield negotiation and preservation of partial
preparation. Dynamic discovery is ordinary execution inside the interval. The
accepted cost is a convoy: a long transaction can stall unrelated writers in
the domain. Existing snapshot readers can continue only if version storage and
publication permit it. A runtime bound either limits the pause or denies
completion to sufficiently long transactions; it cannot promise both.

The gate is not a magic drain primitive. Current C1 transactions can hold
incompatible partial locks, so merely stopping admissions and waiting may never
drain. The candidate needs a simple boundary rule such as canceling uncommitted
attempts and finishing already irreversible execution. Canceling is a candidate
rule, not an implemented safe transition. If discovering who to cancel needs
the old graph or negotiated yield, little has been removed.

The hardest workload is a global graph traversal or join plus writes lasting
minutes while a small independent key serves latency-sensitive OLTP. Per-tenant
gates help only if the transaction cannot cross tenants. Acquiring more gates
as discovery expands reconstructs lock ordering/deadlock problems. A process
failure holding the gate also needs agreed recovery; the gate reduces decisions,
not the durability obligations of those decisions.

### 2. One optimistic scheme; no escalation and no guaranteed retry success

Use a coherent version cut for the whole attempt, validate conservatively at
atomic admission/publication, and discard the **whole** attempt on conflict.
After a finite retry budget, return a definite contention failure. Do not add
reservations, distributed victim selection or partial-preparation salvage to
rescue it. Read-only snapshots can run separately from updating validation.

This alternative avoids a long global pause and preserves the chosen isolation
contract of successful general SQL transactions. It explicitly gives up
guaranteed progress for broad updating transactions on a moving dataset. That
can be an unacceptable HTAP product limitation, but is an honest simpler
baseline. Calling the transaction supported while it practically never commits
would conceal the tradeoff.

Its strongest counterexample is a full-table aggregate followed by one marker
write while an unrelated-looking row changes continuously. Conservative
validation can reject every expensive attempt despite histories that a more
powerful dependency scheme could accept. Increasing backoff cannot fix an
almost-always-stale read set. Making the marker a separate transaction would
change the original contract, not solve the validation case.

There remains one substantial shared mechanism: coherent versions plus atomic
distributed validation/publication with an agreed result. This is not just
"add MVCC" to the current protocol. A useful comparison must actually remove
retained read locks, C1 part commitments and contention escalation, rather than
retain both systems. Read/write dependencies, phantoms and declared outputs
still determine what the validator must reject.

### 3. Make long work a versioned data product, and keep live actions bounded

For workloads whose owners accept the contract, compute from a named snapshot
into immutable output, then submit a bounded catalog publication or a fresh
checked command. Reports, training, closed-period billing and warehouse rebuilds
often have a natural source version. A publication may switch one reference
without retaining source protection through the whole computation.

This deletes long distributed preparation from those operations and makes
failures restartable at the job/output level. It is not a generic implementation
of `BEGIN; arbitrary reads; arbitrary writes; COMMIT`. The strongest counterexamples
are an exact current risk check, absent-row constraint, salary update using the
live departmental average, and continuous authorization revocation. They must
use a general transaction mechanism or accept a changed promise. Merely placing
them in a workflow does not remove the invariant.

This boundary can coexist with either general-transaction alternative above,
but should not be used to claim that the general mechanism handles long exact
updates efficiently. Where atomic replacement targets mutable live data,
concurrent writes to the old generation must be prevented, included or explicitly
discarded; a reference swap alone does not preserve them.

## Preparation is open to deletion, not just refactoring

The brief currently makes intermediate C1 results durable commitments supported
by retained locks, then repairs them transitively when yielded. The three
candidates instead put long work in one of three places: ordinary computation
inside an exclusive transaction; speculative computation that is discarded as
a whole; or a named historical job with an explicit separate publication.
None needs each discovered part to become a protected partial promise.

For Orbital, every candidate still has to select agreed outcomes independently
of consumer-local timing. A valid serial history is insufficient if two allowed
schedules choose different committed transactions, returned ranks or metadata.
A coarse agreed transaction order is a possible simplification, but can reduce
concurrency and delay epoch publication. The current epoch contract does not
itself specify a global snapshot frontier or a global fallback interval.

## Prior-art anchors and limits

These sources establish specific precedent; they do not prove that the proposed
SixDB compositions work.

- PostgreSQL distinguishes statement snapshots, transaction snapshots and
  serializable admission. Its serializable predicate records can detect
  dependencies without blocking writers; using snapshots alone still admits
  read/write anomalies. This is evidence that exclusion is not inherent, not a
  recommendation to import its entire machinery. [Transaction isolation](https://www.postgresql.org/docs/18/transaction-iso.html).
- PostgreSQL also exposes `SERIALIZABLE READ ONLY DEFERRABLE`: snapshot
  acquisition may wait, after which a report avoids ordinary serializable
  tracking and serialization failure. This is a concrete example of paying an
  up-front delay for simpler long-read execution. It is not a general updating
  transaction. [SET TRANSACTION](https://www.postgresql.org/docs/18/sql-set-transaction.html).
- FoundationDB explicitly describes snapshot reads as weakened isolation and
  illustrates an **arbitrary** item removal narrowed to a selected-key check.
  That example does not justify exact maximum selection. Its documented
  long-transaction limit and version-reference publication pattern show
  explicit capability boundaries rather than universal support for long live
  transactions. [Developer guide: snapshot reads](https://apple.github.io/foundationdb/developer-guide.html#snapshot-reads),
  [long-running transactions](https://apple.github.io/foundationdb/developer-guide.html#long-running-transactions).
- Spanner's consistent multiversion reads rely on transaction timestamps whose
  ordering reflects the promised transaction history. This demonstrates that
  nonblocking multishard reads are feasible, while making timestamp/frontier
  construction part of the mechanism. It does not follow from choosing local
  epoch numbers independently. [TrueTime and external consistency](https://docs.cloud.google.com/spanner/docs/true-time-external-consistency).

The [small history probe](histories.py) explores serial witnesses separately
from candidate scheduling mechanisms. It is not an implementation of coherent
snapshot acquisition or distributed admission.

The next useful read-side comparison is small: the N-writer star, the maximum
with a back edge, empty-predicate inserts, the global aggregate on a moving
table, and a long graph traversal. Hold observed results and isolation promises
fixed. Count blocked independent work, whole attempts discarded, long-transaction
completion or explicit failure, historical versions retained, and the agreed
state needed by each candidate. A semantic rewrite deserves its own row, not
an apparent performance win over the original transaction.
