# Index directory implementation spike plan

Assembly-first implementation is in progress. The rejected implementation
remains deleted. Current sources exercise actual module composition; the
[measured checkpoint](FINDINGS.md) records throughput, excessive footprint,
producer trade-offs and the remaining scope. Memory, complete writes and actual
range scans remain open; the latest point-read result does not close them.

2026-10-20: fresh Ikea, Orbital, Loom, Workbench and Architecture review of
`15426260` led to this planning revision. It is an experiment plan, not a closed
implementation recipe or demonstrated budget fit. Current supported controls,
module-owned proposals and still-unqualified algorithms/costs remain distinct.
Ashton subsequently authorized assembly, modeling, live experiments and C++
translation as the evidence supports them. Complete budget fit remains open.

The objective is a directory cheap enough for the complete Engine to beat both
Calico and competent native LMDB with substantial headroom. A fast container
kernel alone does not establish that result. The operation budget includes the
composition that makes its result usable.

The later throughput steer takes precedence over scalar-latency selection:
compare useful completions and total CPU at matched ready work and visibility,
retaining latency and memory as explicit trade-offs. Strong read-only root-graph
signal is a reason to continue, not evidence for write/compaction or complete
Engine headroom. Isolated footprint must include catalog/ownership and retained
capacity; Pool mapped bytes alone are not resident memory.

- [Budget](BUDGET.md): comparator evidence, aggressive operation ceilings,
  composition costs and the conditions under which a candidate is rejected.
- [Pseudocode](PSEUDOCODE.md): reads, cursors, edits, overflow, balancing,
  publication, failure, schema changes and suspension.
- [Layout and design review](DESIGN.md): bytes, ownership, paper revisions,
  prior art and the remaining costs that need direct measurement.

The first native prototype has selected-read assembly, a direct suffix-mask
control and a real64-byte moving-Part producer with coupled Orbital publication.
`index_directory_check` and `index_directory_material_check` cover exact bytes,
original-rank/slot distinction, gaps/max prefixes, guarded private cloning,
late old/fresh readers under reclamation and partial-candidate refusal.
`index_directory_driver` compares useful projected/material outputs and that
actual producer against direct LMDB, with fresh and64-operation cohorts.
`index_directory_composed_check` adds a two-record, five-layer P/Q/P producer:
zero-directory ordinary updates, fixed-pointer authoritative corrections,
pristine bypass/dirty-extrema descent, and P-only compaction with late snapshots.
It is a bounded composition check, not the finished Engine driver.
`index_directory_buffered_check` and `index_directory_buffered_driver` add actual
plain24-byte append, indexed-footer append and a packed eager producer,
eight-field rows, all five layers, partial/full compaction and refusal recovery.
The driver matches native64-byte LMDB get/put and cursor-CURRENT atomicB1/B64
policies, repeated-row coalescing and useful full-field/aggregate output. Its
whole-history clock includes setup, fixture destruction and actual shutdown;
the foreground clock is labeled separately. Functional checks passed; performance
fit remains open. Ordered mutation, balancing, extras/schema, broader producer
workloads and complete Engine/Calico comparison remain implementation and
measurement work.

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
| Orbital versioning | Uninterrupted current-cut `Borrowed`, retained `ObjectStore::Scope`, finished `Owner`, `Prepared`, `prepare`/`install`, history and bounded `drain` |
| Orbital physical ownership | Representation capture, retained `source(selection,p)`, fresh Allocations, shared/mixed PartSources and escaping Parts |
| Loom execution | Ready ordinary calls; actual Ring for independent dependencies; stationary Task and Scope for a genuine wait |
| Engine | Segment-change inputs, directory format/navigation, private construction, and coherent inclusion of the required changes in Orbital publication |

Engine introduces no version ledger, reader registry, undo journal or second
commit protocol. A private candidate can be discarded. Captured bodies and
readable padding are sealed even before publication. Ikea effects do not supply
write permission, beforeimages or automatic ObjectStore preservation.

The relevant reading is [Ikea integration](../../../ikea/docs/integration.md),
[Hashtable ownership](../../../ikea/docs/hashtable/usage.md),
[Boundlist](../../../ikea/include/ikea/boundlist/search.h),
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
policy. A stable fence can avoid edits while new keys use existing keyspace padding;
its false candidates and exact segment refinement are real read costs. Neither
choice changes prefix-defined segment identity or permits unordered placement.
The directory does not choose a record-key allocator or summary policy.

Keep the first two layers as ordinary tables: one L0 slot and 256 L1 slots,
in one or two Allocations. For shared-prefix layers below them, use actual Ikea
Hashtable owners with 64-byte dirents AS VALUES. The table manages their
allocation; there is no second payload map or allocation per dirent. Bounded
hash regions and a sorted suffix-mask/array control are implementation
comparisons, not new segment identities.
For remaining layers, compare balanced trees using actual Boundlist searches.

Cross F32/F128 with co-located/independent payload ownership at C8, then C16.
A 32-entry C8 co-located leaf has five Parts; a C16 leaf has three.
Co-location retains/captures more sources per edit but avoids a payload catalog
lookup and many structural publication changes. Independent chunks minimize
scalar capture fanout. New compound leaves include current grouped native capture.
Count both, including real source and catalog costs. Stable Object-name children
avoid ancestor repair on a payload move but add selections. Immutable raw-child
graphs remove those read selections but copy changed paths and retain children
on writes. Keep their costs separate; the current graph is a successful read
control, not a demonstrated cheap-update design. An extrema change repairs only
affected bounds along its path.

Segments keep independent Allocations. Directory array allocation grain,
logical leaf capacity, Part/publication grain and read-cohort width are separate
choices. The measured 16/64/256 coallocation comparison holds logical16/F32 and
Part grain fixed. It does not select16 as an optimal logical leaf capacity.

Extra locators use separately versioned local StreamPack blocks with a priced
16-prefix holder cursor, complete sidecar writes and one final packing decision.
Schema has a concrete direct-definition-name control that avoids a large roster.
These are experiment controls, not selected permanent formats. Extra blocks
retain their actual namespace/rank association. Their coordinates do not change
merely because the block's physical bytes
change. Reservation growth and packing repair only the affected local holders.

No mutable shadow roster is authoritative. The current Scope supplies the old
directory. A small Ikea Hashtable deduplicates private drafts within an edit;
it is discarded at the end and never serves readers.

## Next experiments and completion evidence

The [budget](BUDGET.md#current-matched-anchors-and-headroom) uses the faster
competent matched peer, targets complete Engine at <=0.50P and directory plus
binding at <=0.25P, and lists every operation's engineering ceiling. The
[pseudocode](PSEUDOCODE.md) covers all paths before implementation; unsupported
module seams and unmeasured costs remain explicit. The next bounded work is:

1. Qualify the existing memory/sanity edits as one identified source capture.
   Rebuild every Control-dependent caller; check actual old/fresh bytes, locator
   association, refusal and shutdown. Report live requested metadata, pool
   backing, native mapped bytes, RSS and aged retained capacity separately.
2. Complete scalar and atomicB64 locator replacement. Coalesce repeated commands,
   copy each affected directory Part once and each changed graph ancestor once,
   publish all actual Segment and directory changes in one packet. Compare
   stable-name and graph custody with the same material/inputs. Count captures,
   child retentions and disposal, not just memcpy. The 250ns scalar and350ns
   five-replacement goals are unresolved; preserve misses.
3. Exercise the actual L0/L1 tables and Ikea V64 hash owner, including its real
   clone/growth buffers, native binding and selected-owner lifetime. Then add
   logical ordered mutation, bound repair and balanced final-batch construction.
   Compare logical16/64/256 array/Part alternatives separately from physical
   coallocation, and F32/F128 routing only where the operation costs warrant it.
4. The actual saved-frame range iterator, quarter/full ranges and requested mask
   screen are complete as bounded controls. The dense-rank refinement gains
   modestly; the one pointer-bank trial loses and stops. Complete the inside-clock
   zero guard and actual native mask-run peer, then qualify the stronger grouped
   native policy. Predicate generation stays outside this control; mask access,
   selected full64B consumption and whole-scan Scope/transaction lifetime stay
   inside. Keep actual visits, offered candidates and selected bytes distinct.
5. Diagnose the point-read grain effect with identical instructions and frozen
   inputs: first examine Segment placement and allocation order, directory
   addresses, page/TLB/cache-line geometry and available counters. Hold Segment
   order fixed in a bounded control. The completed fixed-directory/graph control
   demonstrates material address-spacing sensitivity; cache/TLB/bank attribution
   and portable allocator policy remain open. Do not infer them from VmPTE or
   call counts, or select G16 as intrinsically fastest.
6. Complete extras/schema, structural and retained-reader pressure, then the
   broader Engine driver: independently vary segment/record populations,
   duplicates, insertion/relabel events, scan/selectivity and producer policies.
   Price setup, maintenance, failed work and final cleanup against complete
   competent peers. Stable-fact data edits still emit zero directory mutations.

These are the remaining experiments, not permission to grow a failed path.
At each step, update the operation's pseudocode and full cost ledger before
expanding it. Complete fit is not established by a component cycle estimate or
by adding timings from different trials.

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
Generate valid combinations: J<=min(6,K), shared bytes agree and physical keys
remain unique and fully ordered. J6 consequently cannot have equal logical keys
on distinct records; duplicate-run cases use admissible smaller J.

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
identity from offered deltas, not tree caches. This cannot prove a producer
correctly omitted an event or chose a representative event mix. Producer no-event,
base/delta/validity and whole-request claims require an independent semantic
producer oracle in their owning material/Engine experiment. Old/fresh
Scopes must agree with their respective expected states after every failure,
structural repair, conversion and retry. Include duplicate fences at every
storage-page boundary, all-null summaries supplied by the producer, maximum
physical prefix/position and old schema interpretation. Sanitizer and failure
checks support correctness; they cannot certify performance.

Mapped/remote resources use explicit readiness outcomes and real lifetime rules.
Resident measurements will not be presented as I/O, durability, recovery,
distributed-writer or complete Engine-versus-LMDB results.
