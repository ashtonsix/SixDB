# First experiments for trie remapping

Research scope, opened 2026-09-08. A first subset has now been implemented and
measured: fixed-width unique keys, natural terminal controls, ordered/remapped
placement, column rank, block-local disorder, identity references, container
count/sum totals, conversion, and uniform/hotspot/append histories. Its exact
[probe contract](probe.md) and [findings](FINDINGS.md) distinguish those results
from the broader proposals below.

Mixed-region discovery, full 16-bit containers, variable-length keys,
cache-residency controls, retained locator editions, pending summary deltas,
and durable/concurrent publication remain unperformed. Select the next probe
from the signal; this is not a required sequence or milestone. The first spike
is [concluded](README.md), with no continuation selected.

## First falsifiable question

Does the saved natural-key structure work repay exact suffix lookup, mapping
metadata, and insertion maintenance, while leaving the dense-key path cheap?
Initially isolate a remapped terminal region; separately charge the path used
to discover it. Supplying its locator for free answers only the within-region
question and must be labelled that way.

Begin with a small deterministic model over unique ordered integer keys and
row identities. Encode some fixtures as long byte strings with common prefixes
to keep key-comparison cost distinct from numeric spacing. Natural-key and
remapped methods consume identical histories and answer the same queries.

| Arm | Purpose |
| --- | --- |
| Natural-key radix structure with sparse membership and path/terminal compression | A credible direct baseline; avoid making remapping win against deliberately full empty nodes |
| Direct addressing on genuinely dense keys | The fast path we want to preserve, valid only in this regime |
| Packed ordered leaf with spare capacity at the end | A simple B+-tree-style within-leaf baseline for search and shifting |
| Ordered gapped physical slots, local shifting and redistribution | The proposed mapping; retain exact logical suffixes |
| Ordered suffix/slot directory with independently placed payload | Isolate the benefit and price of moving references instead of records |

The packed-leaf arm is not a complete B+-tree benchmark. A toy natural trie
does not reproduce Calico. Before making an end-to-end index claim, use a
credible complete ordered-index comparand and include routing and splits.

For the ordered-slot arm compare payload gaps with packed rank-aligned columns.
At first use narrow values and one wider multi-column fixture. Count movement
per column; an indirection arm must pay for mapping bytes and scan gathers.
Keep 16-bit position geometry fixed while changing live population and active
allocation. Then a small versus 16-bit universe comparison can identify a
metadata floor, instead of attributing all costs to nominal capacity.

## A small set of discriminating histories

Include transition points explicitly. Sweep natural-key occupancy around 5%
and 20%, extending toward sparse and dense extremes, then refine near observed
crossovers. State the denominator and depth: occupied positions per segment,
occupied cells, and records per occupied truncated position describe different
shapes. Hold population constant in one comparison and footprint constant in
another; identical density can hide very different collision/skew patterns.
Separate the steady-state crossover from the amount of subsequent work needed
to repay conversion. Sweep both directions to expose reversibility and churn.

- Dense consecutive keys, then modest random deletions and refills.
- Sparse keys across a wide universe; separately, long shared prefixes with
  distinct tails. Report collisions at the chosen truncation depth, not full
  PK duplicates.
- Appends, uniform insertion ranks, and repeated inserts between neighbours.
  Global free capacity must coexist with local gap exhaustion in the last case.
- A moving insertion hotspot and delete/reinsert churn; run through repeated
  redistribution events rather than stopping before the first expensive one.
- A mixed collection with direct and remapped regions, so discovery and mode
  checks are charged on the direct path too.

Use point hits and misses, lower bounds with absent endpoints, short and long
ordered scans, and simple filtered column reads. Separate numeric key distance
from insertion-rank distribution. Do not infer a workload from the word sparse.

## Correctness before timings

Use a logical ordered-map oracle independent of slot assignment. Check exact
lookup/inverse lookup, uniqueness, occupied rank, and every range in a small
universe after updates. Verify that logical order agrees with the physical
ordering claimed by each arm; an unordered-payload arm must use its directory.
Check empty/full regions, boundary keys, deletion, slot reuse, relabelling and
splitting with arbitrary logical separators, not just radix-aligned cuts.

A small edition model should retain a reader across a remap and split. A stale
physical locator must either resolve the intended retained edition or fail
validation and recover through its identity; it must never name the new slot
occupant silently. Check a secondary reference to a moved row. This exercises
semantics only, not a concurrent C++ memory-ordering or recovery proof.

For the connection to [aggregate maintenance](../aggregate-maintenance/CONCLUSIONS.md),
verify that a pure remap changes no logical range total. Exercise pending
count/sum deltas across an edition change with an explicit drain or translation
rule. Physical summaries may move; do not count a remap as an insert/delete of
logical rows merely to make the oracle agree.

## Account for where the work goes

Add dependencies progressively: keys alone; narrow and wide columns; multiple
secondary indexes; summaries and pending deltas; then retained snapshots and
durability/replication when those mechanisms exist. Compare a full conversion,
local redistribution, and a split. Account for all affected structures, rather
than multiplying key-only timings by row width. Test full order, order between
blocks only, bounded disorder, and unrestricted placement against the same
point/range/column workloads, including the cost of maintaining each constraint.

Report stored suffix/directory/identity bytes, membership metadata, allocated
slack, payload bytes, temporary peak bytes, and old editions separately.
Count key bytes compared, dependent container resolutions, records relabelled,
payload bytes moved, index references repaired, summary changes, split events,
and largest redistribution. A 16-bit position is not a claim of two bytes of
total metadata per record.

Time search, insertion, scans, and complete mixed histories including required
maintenance. Measure redistribution events separately so rare large pauses
remain visible. Google Benchmark repetition percentiles are not operation
latency percentiles; collecting the latter needs a separate sampling method.
Bound or report maintenance debt at the end. Logical counters and modelled
cache-line touches are not measured coherence traffic or durable write bytes.

Keep the default selection small, TUs independently buildable, CPU affinity
explicit, and repetitions sequential. Use the existing runner/artifact helpers;
retain selected compact evidence and verified S3 references with the
[established workflow](../../tools/artifacts.md). Name actual hardware and
working sets; local ARM findings would not establish Zen5 performance.

The useful result is a map of when remapping pays and which extra coordinate
costs dominate. It could support local remapping, favour a simpler adaptive
natural trie, or show that payload placement is the more consequential question.
