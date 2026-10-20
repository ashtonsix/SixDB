# Index directory implementation spike plan

Plan and pseudocode only. The rejected implementation remains deleted. No new
directory implementation, benchmark executable or CMake target is present.

2026-10-20: production composition/source review completed with Ikea, Orbital,
Loom and Workbench; Architecture reviewed the paper counterexamples. Local
references, budget arithmetic and occupancy-credit arithmetic were checked.
No new timing is claimed. Ready-write capture/publication and dispersed cold
navigation remain unqualified against the complete Engine envelope; the plan
does not treat either as a passed result or authorize a broad implementation.

The objective is a directory cheap enough for the complete Engine to beat both
Calico and competent native LMDB with substantial headroom. A fast container
kernel alone does not establish that result. The operation budget includes the
composition that makes its result usable.

- [Budget](BUDGET.md): comparator evidence, aggressive operation ceilings,
  composition costs and the conditions under which a candidate is rejected.
- [Pseudocode](PSEUDOCODE.md): reads, cursors, edits, overflow, balancing,
  publication, failure, schema changes and suspension.
- [Layout and design review](DESIGN.md): bytes, ownership, paper revisions,
  prior art and the remaining costs that need direct measurement.

## Supplied model

Segments exist independently at each of five layers, identified by index,
layer and physical prefix. Their overlapping 16-bit windows are 0:16, 8:24,
16:32, 24:40 and 32:48. A segment has at most 65,536 positions and exists only
while populated. The last layer contains records; earlier layers contain the
specified summaries, potentially with other data structures as well.

The physical key is six bytes and unique. Its first J bytes coincide with the
K-byte logical key. Physical placement preserves full logical order, including
slots inside a segment. Equal logical keys need no internal order.

Every segment has one representation. The index schema can retain many
representations, including historic ones, with different allowed sets at each
layer. The directory tracks segment bounds, representation and Part locators.
Its dirent is 64 bytes: six eight-byte locators and sixteen metadata bytes.

Queries begin in the directory at a caller-selected layer. Seek and continuation
stay within that layer. Explicit descent changes the layer. Directory storage
pages and their balancing never split a prefix-defined segment, assign record
keys, or establish an order between layers.

## Composition used by this plan

Read the production interfaces, their real examples and source, rather than
treating the older disposable drivers as Engine:

| Responsibility | Implemented seam |
| --- | --- |
| Ikea edits and searches | Concrete Hashtable owner/native reader; Boundlist source and overlap; StreamPack mutation/transfer; source-qualified issued writes |
| Orbital versioning | `ObjectStore::Scope`, finished `Owner`, `Prepared`, `prepare`/`install`, history and bounded `drain` |
| Orbital physical ownership | Representation capture, retained `source(selection,p)`, fresh Allocations, shared/mixed PartSources and escaping Parts |
| Loom execution | Ready ordinary calls; actual Ring for independent dependencies; stationary Task and Scope for a genuine wait |
| Engine | Segment-change inputs, directory format/navigation, private construction, and coherent inclusion of the required changes in Orbital publication |

Engine introduces no version ledger, reader registry, undo journal or second
commit protocol. A private candidate can be discarded. Captured bodies and
readable padding are sealed even before publication. Ikea effects do not supply
write permission, beforeimages or automatic ObjectStore preservation.

The relevant reading is [Ikea integration](../../../ikea/docs/integration.md),
[Hashtable ownership](../../../ikea/docs/hashtable/usage.md),
[Boundlist](../../../ikea/docs/boundlist/usage.md),
[Orbital material](../../../orbital/docs/material.md),
[ObjectStore](../../../orbital/include/orbital/object.h), its
[publication implementation](../../../orbital/include/orbital/instantiate/object.h),
[Loom usage](../../../loom/docs/usage.md), and the actual
[Object/Task](../orbital-mvcc/callers/object_module.cpp) and
[Object/Ring](../orbital-mvcc/callers/object_ring_check.cpp) callers.

## Proposed storage strategy

Ordinary data writes do not imply directory writes. After the request has
located its segment, unchanged segment membership, routing range,
representation and complete Part-locator association require no directory
mutation, capture or publication. The segment producer owns data changes,
summary deltas, pristine flags and compaction. A replacement/port that moves a
referenced Part is a directory event; five-layer eager replacement is one
possible producer, not the assumed producer.

Keep directory routing bounds separate from summary values. Compare exact
occupied logical bounds with stable enclosing fences supplied by the placement
policy. A stable fence can avoid edits while new keys use existing padding;
its false candidates and exact segment refinement are real read costs. Neither
choice changes prefix-defined segment identity or permits unordered placement.
The directory does not choose a record-key allocator or summary policy.

Keep the first two layers as bounded ordinary tables. For shared-prefix layers
below them, use small actual Ikea Hashtable owners, partitioned by a fixed
physical-prefix range; do not clone a whole layer. For remaining layers, use a
balanced tree whose ordered endpoint searches are actual Boundlist operations.

Compare a 32-entry co-located five-Part leaf with independent dirent chunks.
The former retains/captures more sources per edit but avoids a payload catalog
lookup and many structural publication changes. The latter minimizes scalar
capture fanout. Count both, including real source and catalog costs.
Child references in directory routing nodes are stable Object names, selected through the same
Orbital Scope. A changed child Part address therefore does not rewrite every
ancestor. A locator-only edit changes a bounded payload Part, not the routing
tree. An extrema change repairs only affected bounds along its path.

Extra locators use separately versioned local StreamPack blocks. Their logical
block/rank coordinates do not change merely because the block's physical bytes
change. Reservation growth and packing repair only the affected local holders.

No mutable shadow roster is authoritative. The current Scope supplies the old
directory. A small Ikea Hashtable deduplicates private drafts within an edit;
it is discarded at the end and never serves readers.

## Work sequence

1. Finish the paper design against the complete budgets. Count dependent
   selections, copied bytes, Part-owner work, allocation calls, publication
   changes and worst local repair for every operation. The pseudocode below is
   the candidate after those revisions, with unresolved costs identified.
2. Establish same-host native Calico and LMDB controls before interpreting new
   directory timings. Use ordinary row-key LMDB as the Engine headroom reference
   and a separate directory-record comparison to expose directory overhead.
   Do not use an opaque-blob comparator as a row-index result.
3. Write the smallest assembly-first vertical slice: one ready directory lookup,
   an unchanged-directory data-update handoff, and one single-segment locator
   replacement through the real material, allocator, capture and publication
   paths. Keep setup, ownership and independent
   oracles in C++. Use [Workbench's actual module-call recipe](../assembly-calls/README.md).
   Compare F32 co-location with independent C8 chunks using the same operations.
   Link real Ikea/Loom/Orbital operations; do not replace them with stand-ins.
4. Attribute the complete slice before extending it. Exercise a retained reader,
   refusal and actual cleanup immediately. If it misses the headroom budget,
   revise the pseudocode or raise the specific Orbital/Ikea/Loom seam with its
   owner. Do not grow the prototype around an already losing path.
5. Compare producer event mixes before assuming their publication sizes:
   stable locators, buffered summaries, eager summaries, representation changes,
   and compaction/port bursts. The directory consumes their actual events, not
   a miniature row/summary engine. Add operations in dependency order:
   multi-change batch, seek/continuation,
   bounds repair, insertion/deletion, split/merge, extra Parts and representation
   changes. Keep a direct LMDB control and the earlier successful slice in each
   comparison. A failing operation remains a visible result.
6. Extend pressure once the ordinary paths are competitive: large directories,
   cold dependencies, long equal-key runs, arbitrary key bytes, rich Part sets,
   concentrated structural churn, long readers, resource refusal and waits.
7. Translate successful assembly into maintainable C++ composition. Compare the
   same complete operations, linked module bodies and memory ownership before
   accepting the translation. Assembly is a design/performance probe, not an
   excuse to omit ownership or publication work.

This sequence is a proposed experiment, not authorization to implement now.

## Workloads and checks

Drive directory changes directly with `SegmentDelta` inputs. Record rows,
summary maintenance, compression and physical-key assignment belong to a
separate producer or later Engine driver. They are not added to the directory's
mutation algorithm or hidden inside its performance result.

Vary J=0..6; every layer; K=0/1/6/8/16/32/64/256 and long equal prefixes;
dense, clustered, sparse and scattered physical prefixes; and directory
populations from one segment through memory-sized millions. Include point hits,
exact-prefix misses, logical seeks through gaps, open/closed ranges, equal keys
spanning many segments, first/last prefixes and empty/nonempty transitions.

Mutations cover same-value requests, inline and extra locator changes, extrema
changes, segment birth/death, increasing and decreasing prefixes, insertions
between neighbours, duplicate-run growth, representation/Part-count changes,
and the supplied changes produced by physical relabeling. Concentrate inserts
at one fence, alternate insert/delete near occupancy thresholds, and migrate
the hotspot. No record-placement algorithm is smuggled into this scope.

Measure both one-operation publication and batches of 16/64, with clustered and
dispersed changed segments. Include one change and five-layer change sets;
report the actual changed directory Objects and supplied segment Objects.
Include the important zero-directory-change cell. Five directory plus five
segment replacements is a conditional relocation/producer stress cell only.
Retain fresh, old and deliberately late-discovering readers. Vary fresh,
recycled and aged Pool inventory and bound retained history by actual memory
pressure. Run one measured treatment at a time; alternate comparator order.

Independent checks derive expected directory membership, bounds and locator
identity from the offered deltas, not from the tree's own caches. Old/fresh
Scopes must agree with their respective expected states after every failure,
structural repair, conversion and retry. Include duplicate fences at every
storage-page boundary, all-null summaries supplied by the producer, maximum
physical prefix/position and old schema interpretation. Sanitizer and failure
checks support correctness; they cannot certify performance.

Mapped/remote resources use explicit readiness outcomes and real lifetime rules.
Resident measurements will not be presented as I/O, durability, recovery,
distributed-writer or complete Engine-versus-LMDB results.
