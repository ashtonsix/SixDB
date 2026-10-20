# Engine versus LMDB: proposed demonstrations and targets

Proposal, 20 October 2026. Both earlier drivers served their purpose. The next
comparison should measure useful logical operations with Engine in the critical
path, after the directory and the necessary segment operations are implemented.
The targets below are engineering ambitions, not measured results or promotion
gates. Directory implementation and the supplied index semantics remain those
in the [spike plan](README.md).

## What the comparison should establish

There are two related questions: can the complete Engine path compete on ordinary
transactional access, and can its summaries, representations and selective Part
access deliver larger analytical gains at an acceptable maintenance cost? Both
matter. A fast aggregate alone does not establish a competitive mutable index;
point parity alone does not demonstrate the value of the richer structure.

The headline comparison should start with logical keys, predicates, projections
and mutations, and end with the requested result or local transaction outcome.
Engine performs its actual plan execution, directory access, tree navigation,
representation/schema binding, field operations and required publication. Supply
supported Engine operations or plans directly; Shore parsing and protocols need
not precede this embedded comparison. Report prepared-plan execution and first
plan construction separately. Physical positions and fixture ObjectIds are not
request inputs that bypass Engine's lookup.

The LMDB application implements the same logical operation using its own suitable
layout. It need not store Engine's segments or run Ikea codecs. Shared encoded
bodies remain an optional attribution control, rather than the main comparison.
Consume the same requested fields and results, including complete requested
streams; avoid forcing either side to construct unused full records.

## End-to-end demonstrations

| Demonstration | Useful requests and histories | What it should reveal |
| --- | --- | --- |
| Ordinary transactional access | Unique hits/misses, duplicate equality, short ranges, projected and full-record reads; value replacement, RMW and small multi-record transactions. | The cost of Engine's real lookup, interpretation, snapshot and publication path. Include both isolated requests and equivalent work under a shared transaction. |
| Sustained mutable ordering | Append, interior insert, duplicate insertion, delete/refill and moving hotspots, with reads throughout. Continue beyond initial growth into repeated rebalance and reuse. | Whether full logical order, padding, directory edits and all required five-layer summary maintenance remain affordable, including expensive-event tails. |
| Projection and filtering | Scan narrow and wide logical ranges, selecting a few fields from wider records; vary predicate selectivity and correlation with segment extrema. | Whether fewer Part reads, compression and pruning save actual time and bytes. Include poor-pruning and incompressible cases. |
| Exact range aggregates | Count, sum, min, max and not-null count over changing ranges and nullable fields. Exercise fully covered summaries and partial boundary descent. | Work avoided by the actual tree. Vary range width, duplicate runs and boundary alignment; count records examined, summaries consumed and Parts read. |
| Mixed transactional and analytical work | Point/RMW/insert traffic alongside range scans and aggregates, with short and retained snapshots. | Useful analytical throughput while preserving transactional latency and correctness, including scheduler service, reader retention and maintenance. |
| Representation diversity and migration | Different representations among segments of the same layer, many historical schema definitions, and migration while old readers remain active. | Binding/dispatch overhead, conversion cost, old/new overlap and the effect on ordinary service. Compare service against LMDB; conversion itself is an additional Engine capability. |

Once Engine's actual cross-index references and maintenance exist, extend this
to secondary lookup plus record fetch and mutations affecting multiple indexes.
Their atomic update and lookup costs belong in the request. A directory alone
does not supply those semantics or justify a synthetic substitute. Joins and
broader relational execution can follow their actual Engine implementations.

Optional filters, sketches, histograms and other data structures can contribute
when a tested representation supplies them. Exact queries still owe exact
answers; workload estimates and approximate answers require their own stated
result contract. The first aggregate fixtures can use bounded integers to avoid
silently choosing floating-point reduction semantics.

## Give LMDB the appropriate comparison

Use the independent [native LMDB provider](../../peers/README.md)
(`workbench/peers/lmdb.cmake`, target `workbench::lmdb`). It supplies the installed
headers and library; the Engine comparison owns its encoding, transactions,
flags and timed interval. Record the actual runtime version and flags.

Use native record lookup, positioned cursor traversal and appropriate duplicate
storage. Preserve record multiplicity without requiring an order among equal
logical keys. A fixture discriminator can distinguish otherwise identical rows;
charge it where stored. LMDB exposes duplicate counts, database entry counts and
append/duplicate access paths: use them when they answer the request. Direct
mapped values can be consumed without an extra adapter copy. Query/key extents
must fit the chosen LMDB layout or include the explicit cost of its adaptation.
These capabilities and size constraints are documented in the
[LMDB API](https://raw.githubusercontent.com/LMDB/lmdb/mdb.master/libraries/liblmdb/lmdb.h).

For analytical work, show both an ordinary LMDB record/index application and,
where useful, an application maintaining suitable aggregates or covering data.
For example, maintained logical-range bucket summaries with record scans at
partial boundaries are a reasonable aggregate control. Charge their construction,
updates, extreme-value deletion repair and storage. This need not reproduce
Engine's five-layer tree. Report the comparisons separately: one shows the
benefit of maintained summaries over records; the other tests the composition
against a stronger application strategy.

Use the same logical transaction boundaries, snapshot lifetime, requested output
and completion guarantee. Batching 64 independent requests and one transaction
containing 64 operations are different workloads. LMDB's transaction and mapped
value lifetimes are described in its
[introductory documentation](https://raw.githubusercontent.com/LMDB/lmdb/mdb.master/libraries/liblmdb/intro.doc).
Keep native LMDB locking and versioning in the comparison. Specify and pin its
version, flags, layout and any application-maintained state.

The initial local-publication comparison can use LMDB with durable flushes
disabled, naming those flags explicitly. This measures a local Engine/MVCC path,
not equivalent durable completion. Default LMDB commits flush buffers; `NOSYNC`
and `NOMETASYNC` change that behavior, as the
[API documents](https://raw.githubusercontent.com/LMDB/lmdb/mdb.master/libraries/liblmdb/lmdb.h).
Durable or replicated comparisons need the corresponding real Engine/Orbital
completion path and matched guarantees. Keep those future results distinct from
resident-memory, local-publication and actual storage-I/O measurements.

## Appropriate initial targets

Ratios below compare the same logical workload and resource budget. They are
proposed ambitions to revisit after the first complete measurements. Do not
collapse them into a single geometric mean or require every representation to
win every workload.

| Area | Proposed ambition | Conditions that make the result meaningful |
| --- | --- | --- |
| Ordinary points, short ranges and small transactions | At least LMDB throughput with no material p50/p99 regression. A repeatable 2x gain would be a substantial result. | Engine costs included, matched snapshots, one-core comparison first, representative hits/misses and payload widths. Retain losses by case. |
| Sustained insertion/update/deletion | Parity or better after paying directory, rebalance, summary and version costs; competitive tails through repeated growth and churn. | Include changes to extrema, duplicate boundaries and reader-held versions. A throughput lead with damaging long pauses is incomplete. |
| Projection-heavy scans | Aim for 2x or more useful throughput, with competitive full-row scans. | Same selected fields/output, matched CPU resources, all metadata/storage counted; include narrow records and poor compression. |
| Broad exact aggregates using summaries | Aim for 10x or more versus an ordinary record-scan application on genuinely coverable ranges. | Exclude requests already answered by LMDB's native metadata fast paths from this claim. Show narrow and poorly aligned ranges too. Against maintained LMDB summaries, target lower total workload cost rather than assume the same speedup. |
| Mixed transactional/analytical service | Aim for at least 2x analytical throughput at the same transactional offered load and p99, or lower transactional p99 at the same analytical rate. | Same total CPU/memory budget and snapshot age; sweep shared absolute offered rates and count all offered, completed and refused work. |
| Space and retention | Demonstrate useful compression after counting directory, schema, summaries and histories; stable space under bounded-reader steady churn and recovery of reclaimable capacity after readers release. | Report the entire retention curve, conversion peaks, reusable pool backing and OS return separately. Compression gains depend on the data, so no universal bytes/record target is selected. |

The aggregate and mixed-work ambitions are where the architecture should earn
its extra machinery. Ordinary-access parity makes those gains useful without
making the transactional side prohibitively expensive. Absolute ns/op or ops/s
targets should come from the first full source cut on the target hardware;
the old drivers' numbers do not establish them.

Also report the measured read/write break-even for a summary-heavy use. If a
read saves `delta_read` CPU time and a write adds `delta_write`, the simple CPU
break-even is `reads/writes > delta_write/delta_read` when both deltas are
positive. Validate that estimate with the actual mixed workload: queueing,
retention and maintenance can change its service behavior.

## Workload shape and measurement

Start with one real index and enough field/representation support to execute the
first useful read and mutation requests. Vary logical key length, shared-prefix
J, duplicates, nulls, field width/entropy, occupancy and update location. Use
short keys as a demanding point baseline as well as long common-prefix keys.
Include populated segments at all five layers and both useful and ineffective
summary pruning. Directory partition boundaries and overflow must occur in real
histories. Correctness compares logical results and visible snapshots, allowing
permutations among equal keys; it does not compare numeric physical positions
between backends.

Use the same logical dataset with enough memory for both backends first, then a
separate fixed-memory-budget comparison. Compression changing whether data fits
is a useful result, but distinguish it from a matched-residency CPU comparison.
Report backend data/metadata, histories, scratch, RSS and page/file accounting;
virtual map reservation is not resident memory. Establish actual working-set
conditions rather than infer storage I/O from an above-cache footprint.

Time complete Engine requests with requested result consumption. Include
allocation, required maintenance, retries and publication where the operation
needs them. Count helper CPU and end-of-trial drain in amortized resource cost;
request latency ends at the promised completion, not Store teardown. Report
loading, index construction and plan setup separately rather than hiding them.
Measure isolated service latency separately from offered-load response latency
including queues. Batch makespans are batch results, not individual request tails.
Sustained throughput needs stable backlog and memory, not merely a short burst
that postpones work.

Reuse the current capture, dataset and reporting tools and the production
consequence checks identified in the
[driver retirement map](../../benchmarks/retired-drivers.md). The
[old allocation findings](../../benchmarks/orbital/README.md) and
[versioned-driver handover](../orbital-mvcc/OUTCOMES.md#the-test-driver-is-disposable)
remain historical references, with source recovery available when needed. Build
the new caller around actual Engine code. Preserve useful failure,
retained-reader and settlement histories without carrying the old fixed records,
fence tree or clone policy into Engine. Component timings and profiles diagnose
the end-to-end result; they do not replace it.

This is a proposed comparison and driver scope. No new executable, measurement
campaign or production interface is selected or implemented by this document.
