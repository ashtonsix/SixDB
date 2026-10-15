# Reflection and decisions

Return to the bucket hierarchy. The separated-control experiment did not earn a
replacement architecture. The useful lesson is to measure complete lookup,
allocation, growth and lifetime together—not to transplant one promising layout
feature or optimize only the current worst multimap loop.

## What the quick experiment taught us

Separating selectors can make misses cheap, but a successful lookup then depends
on the selector result to locate a transposed payload. Group pointers, metadata
residence, slot occupancy and payload stride all matter. Abseil also separates
controls, so separation alone does not explain the loss. The
[weakness analysis](SEPARATED-EXPERIMENT.md#recovery) keeps those mechanisms
and counterexamples distinct.

M=4 reduced payload slack and stride but multiplied group ownership and allocation
frequency. Removing packed tails simplified addressing without removing the
control-to-payload dependency. Base reservation and geometric growth changed the
allocation history enough to overturn conclusions drawn from address arithmetic
alone. Keep the hierarchy's co-located common hit; do not carry M, rectangles or
packed tails forward as public options.

The mutation experiment also exposed implementation costs independent of layout:
a large insertion-capable body on existing-key hits and unnecessarily outlined
caller boundaries. Preserve fused ready operations and specialize admitted stages
where complete measurements justify it. A smaller frame or fewer calls is evidence
about a mechanism, not a performance result.

## What the resumed work changed

| Area | Decision supported by evidence | Limit that remains |
| --- | --- | --- |
| Lookup/storage | Keep the hierarchy, compact list and reversible key representation. | Common warm hits and large compact-list lookup still lose in important cases. |
| Cold execution | Retain Loom and select physical read windows over the same format. Wide maps earn a located-value stage. | Serial latency and wide-multimap memory can lose substantially despite throughput wins. |
| Growth | Prefer doubling and completed occupancy deltas; do not count every repeated update as a new key. | Threshold, primary/secondary capacities and slot geometry must be selected together. |
| Geometry | Use the solver at each growth level. The latest 14/28 families cut wide-map active bytes with similar update costs. | Narrow owners gain little; no universal family is selected. |
| Batches | Keep eight requests, with explicit SIMD and separate read/write lowering. | SIMD hash preparation and the rejected shared compact selector do not solve the whole Worker cost. |
| Small owners | Keep compact persistent state and direct private Orbital growth. | Singleton/context cost and lookup remain; premature underfilled hierarchy promotion is not the answer. |
| Multimaps | Keep width-dependent inline budgets; compare links, pooled vectors and direct buffers as complete owners. | Vectors have not won universally. Wide bags and long merge histories still need a better choice. |
| Worker composition | Bound intake and retries, preserve source interpretation, fold into an existing destination on its own Pool. | Current serialized chunk transfer is sound but not broadly ahead; retained publication and bucket-wise union remain open. |
| Mutation effects | Separate logical deltas, issued damage and pre-retirement preservation. | Whole-material publication tests do not establish byte-COW or a general preservation hook. |

The complete native Worker format panel narrows an architectural problem: larger
buckets recover wide-map storage but barely change the wide-multimap append loss.
That loss cannot be solved simply by raising slots per bucket. The actual Worker
fold then charges the ownership/copy boundary for all three kinds. It finds useful
narrow-map storage and near-parity transfer, alongside substantial set, wide-map
and multimap losses. [Current results](RESULTS.md) keep these separate from cold
read advantages.

## Measurement lessons that affect decisions

Allocating an untimed validator between measured phases changed subsequent malloc
costs and produced false lifecycle advantages. Corrected full-byte oracles now
admit their storage before owner construction and compare after retirement.
[The audit](BENCHMARK-VALIDATION.md) identifies affected cohorts. Keep their raw
observations, but do not select policies from superseded lifetime ratios.

The format/growth/inline/storage arms must describe one coherent configuration
before their best numbers can be combined. Memory means more than packed payload:
charge owners, directories, inactive fixture state, Pool metadata and retained
coexistence separately. Process RSS is not per-container RSS. Static disassembly
and llvm-mca are diagnostic tools, not replacements for complete caller timings.

Orbital allocation friction belongs with Orbital. Its repairs improved real
container paths; a local allocator wrapper would have hidden the problem. The
same rule applies to Loom: the existing final-release-credit Message pattern is
sufficient for the current serialized fold. No extra abstraction is warranted
merely to wrap it.

## Where to stop this round

Finish the running evidence and consolidate it before further tuning. The useful
output is a comprehensible implementation direction and explicit remaining losses,
not another growing list of flags or a claim of broad parity. Future work should
start from the finite [graduation gaps](GRADUATION.md), particularly integrated
multimap ownership, compact lookup, and complete mutation/retained-reader lifetime.
This does not authorize another experiment campaign during the consolidation pass.
