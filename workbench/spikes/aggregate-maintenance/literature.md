# Prior art for descendant aggregate maintenance

Initial targeted reading, 2026-09-07. This is a mechanism map, not an exhaustive
survey or a novelty claim. Statements about a source are separated from their
possible application to SixDB. No published speedup is treated as a prediction
for modern SixDB hardware or workloads.

## Closest to the question

**Chun, Chung, Lee, and Lee — “Dynamic Update Cube for Range-Sum Queries”
(VLDB 2001).** Sections 3.2–3.5 combine a stored prefix-sum cube with a sparse
Δ-tree of changed cells, periodically incorporating updates into the cube.
Fully covered delta-tree regions provide aggregate answers. Section 3.5
explicitly propagates an update through the delta tree's ancestors.
[Paper](https://www.vldb.org/conf/2001/P521.pdf)

Relevance: unusually close to stored summary plus query-time correction.
Limitation for this question: it relocates propagation into a smaller mutable
index; it does not eliminate it. Its multidimensional cube setting and optional
approximate answers are separate from SixDB's essential exact aggregates.

**McCarthy and He — “Efficient Updates for OLAP Range Queries on Flash Memory”
(The Computer Journal 54(11), 2011).** The publisher's abstract describes RAM
caching, writing whole trees instead of incremental disk-tree updates, a
quadtree representation, and optional lossy compression. It explicitly starts
from the Δ-tree's update cost on flash.
[Publisher abstract and DOI](https://academic.oup.com/comjnl/article/54/11/1773/351417)

Relevance: a direct follow-on suggesting batched construction of delta indexes.
Reading depth: abstract only in this pass; the full algorithms and experiments
still need inspection. No recommendation of its lossy path or transfer of its
reported performance factors.

**Bender et al. — “An Introduction to Bε-trees and Write-Optimization” (2015).**
Buffers batch messages on their path toward leaves. Queries account for
pending messages; upserts encode deferred changes without requiring the caller
to read the current value first. The article explains the update/query
trade-off and why read-before-write can erase an upsert's advantage.
[Author-hosted paper](https://people.csail.mit.edu/bradley/papers/BenderFaJa15.pdf)

Relevance: buffer placement, routing, batching, and deferred operation semantics.
Adaptation needed: a query that absorbs an internal aggregate must account for
all relevant pending messages, including messages above or beneath that region.
Ordinary dictionary/range-reporting guarantees do not establish the cost of
that aggregate operation. One message per ancestor would still expand writes.

## Implemented mechanisms worth inspecting

**RocksDB Merge Operator.** Deferred operands can be evaluated on a read or
during compaction. Partial merge combines operands when their semantics permit
it; full merge applies operands to a base. Associativity is an explicit concern.
[Project documentation](https://github.com/facebook/rocksdb/wiki/Merge-Operator)

Relevance: distinguish merging corrections with each other from applying them
to a base. A keyed merge operator alone supplies neither a hierarchical range
aggregate index nor the right snapshot contract for SixDB.

**ClickHouse SummingMergeTree.** Background part merges combine numeric values
for matching keys, but summation can remain incomplete across parts. The
documented query pattern still aggregates those parts at read time.
[Project documentation](https://clickhouse.com/docs/reference/engines/table-engines/mergetree-family/summingmergetree)

Relevance: a practical example of query-visible partial aggregates surviving
between merges. It does not establish the transactional update/delete,
hierarchical coverage, or snapshot semantics sought here.

**Differential Dataflow — indexed arrangements and compaction.** Arrangements
index batches of `(data, time, diff)` updates and merge their representations.
Logical compaction gives permission to coalesce time distinctions readers no
longer require; physical compaction controls merging batches.
[Arrangements](https://timelydataflow.github.io/differential-dataflow/chapter_5/chapter_5.html),
[compaction](https://timelydataflow.github.io/differential-dataflow/chapter_5/chapter_5_3.html),
[original CIDR 2013 paper record](https://www.microsoft.com/en-us/research/publication/differential-dataflow/)

Relevance: indexed deltas and the distinction between memory layout and retained
logical history. This is a conceptual input; adopting a dataflow engine or its
progress model is not implied.

## Aggregate and concurrency comparands

**Pennino, Pizzonia, and Papi — “Overlay Indexes: Efficiently Supporting
Aggregate Range Queries and Authenticated Data Structures in Off-the-Shelf
Databases” (IEEE Access, 2019).** DB-trees store aggregate
structure above an ordinary DBMS. The paper covers aggregate range queries,
updates, and batching; its evaluation explicitly does not use a multi-client
cache protocol.
[Paper](https://arxiv.org/html/1910.11754)

Relevance: a general aggregate-tree comparand and batched update construction.
It is not a solution to concurrent durable delta publication; database round
trips and the overlay setting distinguish its cost model from SixDB's.

**Kokorin, Yudov, Aksenov, and Alistarh — “Wait-free Trees with
Asymptotically-Efficient Range Queries” (IPDPS 2024).** The paper maintains
concurrent metadata through ordered operation processing and hand-over-hand
helping, including range counts without enumerating all keys.
[Accepted manuscript](https://openaccess.city.ac.uk/id/eprint/34318/1/Concurrent_Count_Queries.pdf)

Relevance: exact aggregate visibility is a concurrency problem as well as an
arithmetic one. This is a comparand for shared mutable aggregate state, not
evidence about durable storage write amplification or a reason to select
wait-free machinery for SixDB.

**ReproBLAS.** Reproducible accumulation addresses dependence on summation
order and execution arrangement.
[Project and publications](https://bebop.cs.berkeley.edu/reproblas/)

Relevance: regrouping sums during compaction must respect a chosen numeric
contract. Benchmark accumulator size and arithmetic cost alongside the buffer
design; compiler flags alone cannot choose that contract.

## Further reading, if the experiments call for it

- [DBToaster: Higher-order Delta Processing for Dynamic, Frequently Fresh Views](https://arxiv.org/abs/1207.0137)
  (Ahmad et al., 2012): auxiliary views maintain other views through higher-order
  deltas. Abstract inspected. Useful if the question broadens to maintaining
  query-specific expressions and joins; simple descendant sums do not require
  the full machinery.
- [Root-to-Leaf Scheduling in Write-Optimized Trees](https://arxiv.org/abs/2404.17544)
  (2024): flushing work that must actually reach leaves. Abstract inspected.
  Follow up if buffered-tree scheduling becomes a candidate, especially for
  bounded completion latency under load.

The open contribution is the combination of placement, range correction,
coalescing, history, and physical costs in SixDB's setting. This reading does
not establish that any such combination is novel.
