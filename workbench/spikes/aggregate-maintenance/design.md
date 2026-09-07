# Maintaining descendant aggregates

Research question opened with Ashton on 2026-09-07. The initial spike is
[concluded](CONCLUSIONS.md); this design-space map preserves alternatives and
semantic questions beyond the two count/sum probes. The architecture remains open.

**How can higher strata retain useful per-field `min`, `max`, `count`, and
`sum` summaries without every mutation causing expensive ancestor writes?**
Investigate buffering aggregate changes so queries can use stored summaries
alongside pending corrections. Partitioned runs and a
[shared append buffer with dirty-prefix filtering](shared-dirty-buffer.md)
are candidates. The data structure, geometry, and buffer representation remain
open; the first measurements make foreground construction cost a central concern.

Ashton's Calico experience establishes this essential set. Min/max changes
usually stop propagating fairly soon; monotonically increasing values are an
important exception. Count/sum changes can reach every summarised ancestor.
This is the motivation to investigate, not a distributional assumption for all
experiments. Monotonic keys and monotonic values of a summarised field are
separate workload properties.

See the [literature map](literature.md) and [experiment ideas](experiments.md).
The first [executable probe and findings](FINDINGS.md)
now measure logical count/sum maintenance and resident-memory cycle costs.
[Sketches, filters, and histograms](../../notebook/secondary-summaries.md) are a separate
question; their semantics and maintenance policies are not inherited here.

## What Calico actually supplies

The local checkout has several relevant paths, not one uniform policy:

- [`maint.h`](../../../../calico/engine/include/engine/maint.h), especially
  `maint_write`: batches changes by ancestor position, maintains counts,
  widens bounds, and either updates sums or marks rows dirty. This path
  re-encodes stratum containers; its stated scope is u32 keys and non-null
  columns for this writer.
- [`sumrise_build.h`](../../../../calico/engine/include/engine/sumrise_build.h),
  `prepare_dirty_point`, `prepare_dirty_insert`, and `store_dirty_point`:
  point writers prepare patches to affected ancestor dirty words and publish
  them with the leaf mutation. Missing or already-dirty summaries need no
  patch. This is a smaller physical operation than the batch re-encoding path.
- [`source_farray.h`](../../../../calico/engine/include/engine/source_farray.h):
  read-side summary use checks dirtiness and membership alignment. A dirty
  summary cannot provide an exact aggregate through that path.
- The [summary-grain study](../../../../calico/workbench/science/systems/summary-grain-16v256/FINDINGS.md)
  found workload-dependent benefits from finer bounds. It is evidence about
  the complete pruning/decoding cycle, not a measurement of the proposed
  delta buffer. Preserve its lesson: count bytes and downstream work saved,
  not just the number of extra decisions a summary can make.

The new opportunity is to preserve query usefulness after a mutation instead
of relying primarily on invalidation and descent. No Calico timings were
rerun for this investigation.

## Separate the aggregate semantics

| State | Insert | Delete / replacement | What can be combined cheaply? |
| --- | --- | --- | --- |
| Row count | `+1` | `-1`; replacement normally zero | Signed count deltas |
| Per-field non-null count | `+1` for non-null | Subtract old presence, add new presence | Signed presence deltas |
| Sum | Add value | Subtract old value, add new value | Additive deltas, subject to numeric semantics |
| Exact min/max | Extend extremum if necessary | Removing the last witness can require finding a replacement | Merge over live disjoint sets; no scalar inverse |
| Conservative min/max bounds | Widen to include additions | May retain a removed extremum | Bounds can remain useful for pruning while exact extrema are unknown |

Per-field non-null count matters for SQL: an all-null/empty input does not
have a non-null `SUM`, `MIN`, or `MAX`. A numerical accumulator of zero does
not distinguish it from a non-empty input whose sum is zero. Exact row count
and field presence also distinguish empty subtrees from stale bounds.

A replacement needs its old contribution. For a key move, subtract at the
old location and add at the new one, with one transaction's visibility.
Buffering does not eliminate the cost of obtaining beforeimages for arbitrary
updates and deletes. Explicit increment operations may already supply a delta.
Integral/decimal variants still need defined overflow rules and sufficient
intermediate width, including a signed representation for negative corrections.

Floating-point sum needs its own decision. Ordinary floating-point addition
is not associative, and subtraction does not undo earlier rounding. Therefore
base-plus-delta and arbitrary compaction schedules need not reproduce a fresh
scan, even with fast-math disabled. Candidates include exact/widened integer
or decimal accumulation where applicable, a reproducible accumulator, or a
specified reduction contract. A deterministic schedule alone does not make
different schedules equivalent. [ReproBLAS](https://bebop.cs.berkeley.edu/reproblas/)
is relevant prior art for order-independent reproducible reductions; its state
and cost need measurement, and it does not settle SixDB's SQL numeric contract.

## The main design choice: what is a delta addressed to?

Let `h` be the number of summarised ancestors of one changed location.

**Address every affected summary.** Emit `(summary ID, field, delta)` to each
ancestor. Reads are simple keyed lookups. Coalescing and sequential writes can
reduce physical write amplification, but a mutation still generates up to
`h` logical contributions. Putting these entries into an LSM or a buffered
tree does not itself remove that expansion.

**Address the changed location once per relevant keyspace.** Emit the
contribution at a stable leaf/key range, retaining enough detail for future
queries. An ancestor asks for the aggregate of deltas beneath its coverage.
This removes immediate ancestor expansion but makes the delta buffer a range
aggregate index in its own right. Multiple maintained keyspaces still cost
multiple contributions; that independent fan-out must be charged.

**Address selected intermediate strata.** Coalesce changes within a coarse
partition, materialise some rollups on freeze/compaction, and descend inside
the delta representation for other ranges. This interpolates between the
two extremes without requiring every stratum to share one policy.

The closest early precedent is the [dynamic update cube / Δ-tree](https://www.vldb.org/conf/2001/P521.pdf):
queries combine a stored prefix-sum cube with an index of changes. Crucially,
its update algorithm still adjusts ancestors in the Δ-tree. The hypothesis
here is that batched construction of delta rollups can avoid making that
smaller index the new mutation bottleneck.

## Physical designs to compare

| Design | Potential benefit | Cost or failure mode to expose |
| --- | --- | --- |
| Eager summaries with normal buffering and batch coalescing | Cheap reads; simple reference | Ancestor work, shared cache lines, dirty-page pressure |
| Dirty summaries with rebuild / descent | Very cheap repeated invalidation | Analytical work shifts back to children or rows |
| Flat partition-local append buffers | Minimal foreground indexing | Queries scan pending entries; log growth becomes read growth |
| Mutable aggregate delta tree | Fast range correction over a small changed set | Recreates ancestor updates and contention inside the buffer |
| Frozen sorted delta runs with aggregate indexes | Sequential construction, compression, query skipping | Multiple runs, index bytes, compaction and snapshot history |
| Buffers integrated into tree nodes, in the Bε-tree family | Route and batch changes through useful partitions | Flush scheduling, node contention, and aggregate-aware reads across buffers |
| Selected rollups over frozen runs plus a small mutable tail | Coarse queries can skip detailed deltas | Rollup maintenance, tail scan, and publication protocol must all be counted |

Bε-tree query handling is useful precedent for consulting deferred messages.
Its ordinary range-reporting bounds do not automatically imply cheap range
aggregation over buffered messages; an aggregate-aware design must establish
that separately. [Bender et al., 2015](https://people.csail.mit.edu/bradley/papers/BenderFaJa15.pdf)

An eager baseline should already batch dirty pages and have the same durability
and replication guarantees as the alternatives. Comparing synchronous ancestor
writes against an undurable memory append would answer the wrong question.

## A promising first hypothesis

Explore **partitioned mutable deltas, frozen into sorted runs whose summaries
are constructed in batches**, with independent policies for additive state
and extrema. This is a candidate to challenge, not a selection of an LSM as
SixDB's core structure.

Partitions start from stable logical key ranges within shards. Producer lanes
may help avoid a shared mutable root, but every additional lane increases
query fan-out. Range partitions, producer lanes, time epochs, and field groups
are independent axes. Addressing a delta by a mutable physical row rank would
couple it to splits, compaction, and membership layout changes.

A frozen run could contain sorted changed locations and signed count/sum
vectors, with a sparse directory and block aggregates or prefix sums. A query
can absorb fully covered run blocks and inspect boundaries. Optional coarse
rollups are built while the sorted batch is already being traversed. For the
additive state, prefix subtraction is available only when the chosen numeric
representation supports it correctly; min/max need a different run index.

The mutable tail must remain queryable at the requested snapshot. A bounded
scan is the simplest comparand; a small index or per-lane partition total is
another. A fine partition scheme that requires a whole-shard query to open
thousands of leaf logs is not automatically an improvement. Frozen-run totals
can reduce this fan-out without an eagerly updated global root.

Coalescing has two different opportunities: repeated updates to one location,
and different locations sharing a summarised prefix. Retain location-level
detail where boundary queries require it. A total for an entire partition
cannot answer arbitrary subranges inside that partition.

For `min/max`, initially compare cheap widening plus deferred exact repair
against eager exact maintenance. Deletions can leave an outer bound safely
loose; **unaccounted additions can make it unsafe**. Queries must include the
pending additions' envelope before pruning with the stored bound. An exact
`MIN`/`MAX` answer needs a surviving witness, sufficient candidate state, or
descent/reconstruction. Witness counts and small runner-up sets are candidates;
neither solves arbitrary deletion sequences with constant state.

Do not let an unknown extremum automatically discard a valid count/sum.
Whether separate validity metadata per aggregate/field pays for itself is
another measurable choice.

## Queries, visibility, and compaction

For additive state over covered region `P`, the conceptual rule is:

```text
answer(P, snapshot s) = base(P, cut c)
                      + deltas(P, visible after c through s)
```

This assumes `c <= s`, a complete committed cut, and a compatible base
generation. It is notation, not a requirement for a global scalar timestamp.
Different partitions may have different complete cuts; they must still be
read at one transactionally valid snapshot. A newest observed sequence number
is not proof that all earlier changes have arrived.

The lookup has both a spatial interval and a visibility interval. A run total
is directly usable only when its entire represented contribution belongs in
both. A run that straddles the base cut or the reader's snapshot needs finer
time information, even if its key range is fully covered. Sorted keys alone
do not answer that two-dimensional selection. Time-banded runs, retained
per-key histories, and indexes over time are alternatives to price explicitly.

Publication must prevent gaps and duplication. For example, base sum `1000`
at cut `100`, followed by `+7` at `101` and `-2` at `102`, gives `1005`.
After publishing base `1007` at cut `101`, only `-2` remains to be applied.
Reading the new base with both deltas incorrectly gives `1012`. Readers need
a compatible base/run manifest or an equivalent validated generation view.

Compaction of delta runs and incorporation into base summaries are separate
operations. Combining physical files need not discard version distinctions.
An old reader between two cancelling updates must still see the intermediate
state. Coalescing across that boundary requires retained history or proof that
the boundary is no longer observable. Long readers can therefore set the
memory/storage floor even when current net deltas are tiny. Differential
Dataflow's distinction between [physical and logical compaction](https://timelydataflow.github.io/differential-dataflow/chapter_5/chapter_5_3.html)
is a useful conceptual reference, without adopting its timestamp model.

Mutation and summary correction must become visible together, or queries need
a correct fallback until the correction is complete. Recovery must reconstruct
both from durable information without counting replayed mutations twice. A
separate durable summary log is one option; deriving deltas from the ordinary
mutation log is another. Charge bytes and recovery work for both. Reads of a
transaction's own writes need the same correction before those writes commit.
Across shards, partial visibility of a transaction must not produce a partial
aggregate. This study does not select the transaction protocol.

Queries may absorb a summary only when its entire live coverage is known to
satisfy the query predicate at that snapshot. Partial coverage, filters on
other fields, and joins require finer evidence or row work. The base summary
and its corrections do not turn an arbitrary filtered SQL aggregate into a
single sum. Pending inserts or updates may also change whether the coverage
qualifies at all.

Splits, merges, and shard movement must preserve logical coverage and delta
ownership. Parent and child summaries can coexist, but a query's chosen cover
must be disjoint: never count a region through both levels.

## What determines whether this wins?

- **Write locality and batch size:** repeated keys and shared prefixes permit
  coalescing. Dispersed updates, wide schemas, and many keyspaces weaken it.
- **Query coverage:** broad aggregates reward coarse run totals; tiny or
  fragmented ranges expose directory, boundary, and mutable-tail costs.
- **Memory and residency:** a larger delta index may spare storage writes
  while losing on cache traffic, pointer chasing, or NUMA coherence.
- **Merge policy:** more overlapping runs reduce immediate rewriting but
  increase query probes. More aggressive merging spends bandwidth earlier.
  Folding sparse changes into a large base can rewrite mostly unchanged bytes.
- **Sustained load:** regular compaction needs a service budget, not just a
  timer. Track backlog bytes, age, read fan-out, and foreground interference;
  include recovery after bursts and what happens when service falls behind.
- **History:** snapshot retention limits cancellation and garbage collection.
  Numeric determinism can independently limit legal regrouping.
- **Extrema churn:** monotone growth, distribution shifts, and repeated removal
  of extremal values challenge the expectation of short propagation.

Measure logical contributions, bytes appended, bytes rewritten, background
CPU, query work, and retained history separately. A smaller foreground number
is insufficient if the same work becomes an unbounded compaction backlog.
The [experiments](experiments.md) are intended to find where these costs cross,
without choosing a project milestone before the evidence arrives.
