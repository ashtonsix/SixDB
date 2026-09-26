# Substantial processing applications through extensions

**Yes: a Spark-like application surface is a worthwhile direction for SixDB.**
Extensions could define substantial distributed data-preparation, analytical,
graph and feature-processing jobs, with reusable partitioned intermediates and
explicit publication. A scalar function called by SQL is only one use. This
is an architectural opportunity to investigate, not a claim that SixDB already
implements it or a commitment to Spark API compatibility.

The comparison draws on Spark's lazy transformations/actions, partitioned
functions, cache reuse and shuffle behavior. Its structured interfaces expose
more optimization information than opaque functions. These ideas motivate the
application surface; they do not supply SixDB's transaction or verification
rules. See the official [RDD guide](https://spark.apache.org/docs/4.0.2/rdd-programming-guide.html)
and [SQL/DataFrame guide](https://spark.apache.org/docs/4.0.2/sql-programming-guide.html),
with the inspected scope recorded in [sources](SOURCES.md#spark-like-application-angle).

## The useful composition

A job could describe a deferred computation over snapshot-qualified partitioned
data and immutable artifacts. An action requests a finite result, a reusable
dataset, a continuing maintained result, or an explicitly defined publication.
Engine can inspect relational stages and compose them with extension regions;
an application adapter can supply other operators and meanings. Loom chooses
physical work and residency within that meaning. Orbital carries and retains
the bytes and exposes its service facts.

For example, an application could capture a source version, parse documents in
partition-sized extension calls, join a versioned dictionary, build features,
reuse those features for several model evaluations, and publish a selected
generation. Source-local parsing, a broadcast dictionary, feature materialization
and model-local inference are alternative placements. The correct choice depends
on expansion, reuse, model size, input residency and metered network edges.
Keeping the program near existing database versions might avoid exports and
duplicate infrastructure; it could also consume the CPU, memory and history
needed by short transactions. Both effects must be priced.

| Capability worth supporting | Proposed fit and meaningful limit |
| --- | --- |
| Structured transformations plus partition functions | Keep filters, projection, joins and known algebra inspectable. A batch extension region supplies a versioned computation with declared access/effect rules; opaque code does not automatically expose a legal predicate pushdown or incremental derivative. |
| Deferred execution and reuse | Bind an action to explicit inputs and result meaning. Laziness can avoid unused work and enable fusion; a direct small call should not need a global graph or durable task per function. |
| Shipping code and dependencies | Send pinned artifact references and captured inputs toward eligible data, or move data toward a warm large model/runtime. A process pointer, mutable captured object or locally resolved code alias is not a replayable binding. Account for artifact distribution and startup. |
| Reusable distributed datasets | Separate cached residency, a reconstruction recipe, selected durable checkpoints and a published data product. Key reuse by input/cut, code/runtime/configuration and applicable representation/permission context. Reuse cannot stand in for a required independent execution. |
| Stage/exchange boundaries | Fuse useful local regions, repartition only where required, and select bounded channels or materialization at boundaries. Operator, task, network packet, checkpoint and transaction grains remain independent. |
| Long jobs and iterative work | Keep a job identity and progress across many physical tasks and epochs. Reuse arrangements/features and choose checkpoints to bound reconstruction. Logical iteration and source version remain distinct. |
| Streaming applications | Expose finite state/offset/publication steps and an explicit lateness/correction policy. General unbounded processing is not one indefinitely open atomic transaction. |
| Results and externally visible actions | Return bounded/paged results or publish prepared objects under a defined contract. Large `collect`-style gathers need a finite destination budget. External actions follow committed intents and their sink's outcome/deduplication contract. |

The first useful application surface could therefore combine structured plans,
batch extension calls, explicit reuse and asynchronous job control. An arbitrary
serialized closure language, a JVM/Python executor fleet, accelerator collectives
or Spark-compatible scheduler are separate possible investments. Their value
should follow actual application requirements. GPU or tightly synchronized
training work may need coordinated resource admission and different numeric
assurances; the current evidence does not select that path.

## Job lifetime is not transaction lifetime

Three different promises can support a long job:

- **One coherent historical result.** Read at a captured cut, retain needed
  versions and code, and serve the completed result under its verification rules.
  Private parallel work may span many rounds and epochs at that same cut.
- **A prepared generation with bounded activation.** Construct private data,
  establish its availability and publish a reference or finite set of references.
  Define whether this is an authoritative replacement or must preserve live edits.
  A small activation record does not remove the data's retention obligations.
- **A continuing sequence of finite updates.** Couple each step's source offsets,
  state and effects to its explicit publication unit. Later corrections and sink
  actions follow that contract; checkpoint completion alone is not publication.

These promises cannot be chosen merely by the scheduler. A job that reads an
old ranking and later mutates today's winner needs the application's declared
semantics or validation, not an accidental split into convenient transactions.
Dynamic sources can be discovered at the fixed transaction position; newly
discovered published effect authorities must still fit the complete envelope.
Private shuffle partitions are not themselves new application authorities.

The current extension lanes also matter. Hardened WASM and approved logically
deterministic native execution can compose through the distributed application's
rules. The selected checked-native lane confines source and effect authorities
to one shard. A Spark-like job interface does not silently broaden that lane.
Any fresh read-only result still obeys the applicable publication/checking gate.

## Focused reuse and reconstruction probe

[lineage_probe.py](lineage_probe.py) adds a small executable comparison for this
angle. It is a **fixed partition workload**, not a graph runtime or Spark model.
Four partitions each contain 32 integers. A versioned pure transformation is
evaluated when an action asks for count, sum and top-three; another action can
reuse it. A bounded LRU cache stores computed partitions. A separate finite
checkpoint store can retain a selected iteration. One partition can lose its
cache, and captured source or executable editions can become unavailable.

The operations are exact integer calculations checked against an independent
whole-input oracle. Eight bytes per input/result value are an illustrative
encoding. Counters record function calls and source/checkpoint bytes; there is
no wall-clock claim, network, VM, distributed driver recovery or simulated
storage durability. Per-partition working space and final gather are outside
the cache budget; [the resource probe](RESOURCES.md) studies that missing coupling.
Source pins, artifact bytes, security context and replication are not sized here.

| Authored comparison | Result |
| --- | --- |
| Define a job, request no action | Zero source reads and extension record calls. |
| Two actions, no cache | 256 extension record calls. |
| Two actions, fitting 1,024-byte cache | 128 calls. |
| Two actions, sequential scan through 512-byte cache | 256 calls: repeated eviction eliminates the hoped-for reuse. |
| Lose one of four cached partitions after one transform | Lineage recomputes 32 records; a complete checkpoint requires zero transform calls, reading 256 checkpoint bytes. |
| Lose one partition after 20 iterations | Source lineage requires 640 record-step calls; a checkpoint at iteration 10 cuts this to 320 while initially writing 1,024 bytes. |
| Only 512 checkpoint bytes, lost partition was not checkpointed | Recovery still requires 640 calls; a partial checkpoint must not be presented as full coverage. |
| Captured source or executable missing | Reconstruction reports unavailable instead of resolving a newer edition. |
| Deliberately reconstruct one missing partition from live source/code aliases | The whole result changes: mixing three old partitions with one current partition is wrong. |
| New source/code binding with version-qualified cache identity | The action computes the new answer and does not reuse old partitions. |

A complete checkpoint can supply this pure workload's result representation
without executing its upstream code again. It does **not** establish a required
independent execution/check, permit removal of audit artifacts, or replace the
publication record. A more general cache key must include every input that
changes semantics or access, not just the simplified source/code fields used
in this probe.

All 12 cases, including the deliberately invalid control and unavailable cases,
are retained in [lineage.json](evidence/lineage.json). Nine focused checks cover
the model, including 60 source/code/iteration/capacity combinations. The
[exchange probe](EXCHANGES.md) supplies actual join alternatives and skew
counterexamples, while [progress](PROGRESS.md) covers incremental and recursive
state. No one probe establishes their complete production composition.

```sh
orb -m ubuntu python3 workbench/spikes/orbital-dataflow/check_lineage.py
orb -m ubuntu python3 workbench/spikes/orbital-dataflow/lineage_probe.py --output build/orbital-dataflow/lineage.json
```

## Recommended emphasis

Include extension-hosted distributed processing in the scope. Start with the
composition that directly serves SixDB: versioned partitioned inputs, inspectable
relational plans with batch extension regions, selected exchanges, reusable
artifacts and explicit job/result/publication lifetimes. Retain alternatives
for custom application flows above the same physical services. Cache everything,
checkpoint everything and retry every task are each wrong defaults in ordinary
counterexamples. The opportunity is substantial, but deserves an application
model and finite resource accounting rather than being inferred from the mere
presence of an extension VM.
