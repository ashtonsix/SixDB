# Experiments to distinguish aggregate-maintenance designs

Possible experiments for the [design question](design.md). This spike is
[concluded](CONCLUSIONS.md) after the [partitioned-delta probe](FINDINGS.md)
and [shared dirty-buffer probe](dirty-buffer/FINDINGS.md). The broader probes
below are unperformed possibilities to revisit when useful, with no scheduled
continuation or mandated sequence.

Ashton's [shared-buffer candidate](shared-dirty-buffer.md#focused-experiment)
opens a more focused comparison of append/marking cost, shallow contention,
cold summary lines, and dirty-read-driven flushing.
The [first such comparison](dirty-buffer/FINDINGS.md) has run; it includes
shared-atomic and private-shallow eager baselines, filter variations, and
conservative buffers without a filter. Concurrent publication remains open.

## Minimal correctness model

A small deterministic reference model can expose invalid combinations before
benchmark implementation obscures them. Represent logical rows, transactions,
snapshots, base generations, and delta runs independently of the final tree.
Compute expected aggregates from the visible rows themselves.

Exercise insert, update, delete, nullable fields, duplicate extrema, key moves,
empty regions, non-aligned range boundaries, and mixed predicates. Interleave
freezing, run merging, base incorporation, and readers at older snapshots.
Include cancellation on opposite sides of a retained snapshot, replayed
mutations, incomplete delivery, and simulated split/merge publication.

For count and exact numeric sum, require the reference answer. For pruning
bounds, require containment of every visible value; measure looseness
separately. For exact min/max, verify both the result and any claimed witness.
Keep unknown extrema independent of valid additive state. A later concurrent
or recovery implementation needs its own validation; this model cannot prove
its memory ordering or persistence protocol.

## Where does the ancestor cost go?

Compare eager updates with realistic batch coalescing, dirty-and-descend,
ancestor-addressed delta entries, and location-addressed deltas. For the last,
compare a flat tail, a mutable aggregate index, and frozen sorted runs with
block aggregates. An integrated buffered tree is a useful additional
comparand if the simpler representations leave a routing question unresolved.

Start with identical logical operations and aggregate semantics. Count:

- Entries emitted per mutation and fields encoded per entry.
- Distinct summaries/prefixes touched per batch, including delta-index nodes.
- Mutable writes and cache lines shared between writers.
- Run/index bytes constructed and bytes rewritten per merge or base fold.
- Entries, blocks, runs, and partitions consulted per query.

This first accounting can distinguish reduced work from deferred work. It
cannot establish wall-clock performance. Include construction of frozen
aggregate indexes; it is not free preprocessing.

## Locality and query geometry

| Axis | Cases that should pull designs in different directions |
| --- | --- |
| Mutation location | Uniform, repeated key, hot range, moving hot range, monotonic append |
| Field values | Independent of keys, clustered, monotonic, shifting distribution |
| Mutation type | Insert-heavy, replacement-heavy, deletion-heavy, repeated extremum removal |
| Schema | Few vs many summarised fields; sparse field updates; null transitions |
| Tree geometry | Different depth/fanout and summarised strata; multiple maintained keyspaces |
| Query shape | Root total, prefix-aligned range, narrow boundary range, fragmented cover, selective predicate |
| Mixture | Writes only, reads only, mixed rates, bursts and catch-up |

Vary these deliberately rather than building a large blind Cartesian sweep.
For example, pair broad queries with fine delta partitions to expose fan-out,
and broad dispersed writes with a tiny buffer to challenge batching. Include
an eager shared-state baseline on a working set that fits in cache: a
write-optimised representation must earn its overhead there too.

## Partition and compaction choices

Separate spatial partition size, producer-lane count, mutable-tail capacity,
frozen-run size, run overlap, and summary grain. Compare row-oriented sparse
delta vectors with field-oriented layouts for narrow analytical reads.

Hold the update stream and durability policy fixed while varying merge
policy and compaction service budget. Run long enough to observe repeated
merges and base incorporation, including after input stops. Report throughput
and query/commit latency distributions during merges, backlog size/age,
background CPU, peak memory, and time/work to drain. A finite run ending with
unpaid maintenance must show that debt explicitly.

Report physical write amplification with an explicit denominator, alongside
logical contributions per mutation. Break out base data, base summaries,
delta runs/indexes, WAL, replication, and allocator/block overhead when those
layers exist. Keep estimates from a model separate from measured device bytes.
Equalise retained snapshots, durability, memory budgets, and final maintenance
state when comparing designs.

## Snapshots, contention, and extrema

**History:** hold readers at different ages while updates to the same keys
cancel. Measure how much time detail must remain, how many runs a historical
query needs, and how reclamation recovers when readers finish. Include commit
visibility spanning partitions/shards; a single-thread model is only the start.

**Contention:** compare partition ownership with multiple writers/lane counts.
Measure broad-query fan-in alongside write scalability. On multiple sockets,
separate NUMA placement and coherence traffic from algorithmic work. Do not
quietly give one design more background cores.

**Extrema:** compare exact eager repair, conservative bounds with deferred
repair, witness multiplicities, and small candidate sets. Measure propagation
distance, repair scans, bound looseness, actual payload work caused by loose
bounds, and exact min/max latency. Deleting the last extreme, deleting many
equal extremes, and a moving/monotone distribution should be explicit cases.

**Numbers:** use exact integral arithmetic to isolate structure costs, then
separately price the chosen floating/decimal alternatives. Re-run one logical
history with different batch boundaries and compaction orders to expose
numerical dependence; do not let a structural benchmark silently choose SQL
sum semantics.

## Measurement shape

Use independently buildable targets within this spike
when a probe is selected. Pin CPUs and execute cases/repetitions sequentially,
consistent with SixDB's stated benchmark conventions. State resident set,
access pattern, and actual hardware; cache-tier labels need evidence. Measure
end-to-end query work as well as the update kernel. Zen5 is a primary target;
local ARM measurements would be exploratory evidence for that machine only.

The useful output is a map of where coalescing pays, where query correction
dominates, and what snapshot/compaction budgets make each design sustainable.
No acceptance threshold or product milestone is selected here.
