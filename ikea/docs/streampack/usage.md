# Using StreamPack

StreamPack stores variable-length values in caller-owned byte storage. A block
assigns up to **256** stable ranks; each reservation contains 0–65535 bytes,
subject to a smaller schema maximum and an independent block byte limit. Live
empty values have ranks. Retired values keep their reservations until packing.

The payload has no headers. [Boundlist](../boundlist/usage.md) supplies geometry
reads, search and small geometry-write kernels. StreamPack owns value length,
its optional curated U companion, native value packets and the traversal that
packs or transfers values. Schema preparation composes those laws once; hot
operations bind one coherent source and dispatch once. Grouped R/U may still
share a lengths window: module ownership does not require another allocation,
Part or per-value lookup. Ikea allocates nothing, selects no version and does
not publish a transaction; the collection supplies [those services](../integration.md).

The [original qualification](../../../workbench/spikes/streampack/IMPLEMENTATION.md)
and [Boundlist extraction results](../../../workbench/spikes/boundlist/GRADUATION.md)
record native correctness, codegen, performance and real owner/scheduler evidence.
The [ownership assessment](../../../workbench/spikes/boundlist/OWNERSHIP.md)
qualifies the current geometry/value split. Experimental packed/padded recipes
remain in Workbench. The [reference](reference.md) specifies stored bytes, checked
commands and borrowing. Link `ikea::streampack` and include `<ikea/streampack.h>`.

## Prepare a schema, bind storage

Prepare `layout` once for all blocks with that schema. Slot capacity is fixed at
256; the schema supplies the reservation maximum and optional tighter byte limit.
`view::bind` accepts four windows: payload, directory, lengths and control. The layout reports directory,
control and lengths extents plus lengths alignment. The windows may live in one
allocation; the supplied spans must be disjoint. Alignment gaps need not belong
to StreamPack.

- **Direct** stores exclusive reservation ends in a separate directory plane.
  Point lookup needs the previous end; a scan carries it forward.
- **Width** stores one group-start checkpoint per sixteen slots. A point lookup
  adds a native masked width sum; traversal carries a running position. Its default
  length groups contain sixteen R followed by sixteen U fields.

Choose the facts the schema can promise before choosing field placement. `full`
derives U from R and stores no used lengths; it is useful for exactly reserved
dictionary values. `immortal` omits tombstones. `fixed_reservation` derives all
geometry; zero-width schemas need only count and optional tombstones. Mutation
cannot introduce a fact the schema omitted. Native lane sizes are implementation
choices derived from schema bounds, not additional public geometry names.

For variable Width with both R and U, use the default grouped layout for complete
value lookups. Select `placement::separated` for a workload dominated by U-only
scans. With `full`, both placements store only R: there is no U plane or R/U
interleaving. Their alignment requirements still differ. Direct has separate
endpoints and optional U regardless of the placement selector; fixed geometry
also ignores that selector. Do not make a placement decision in those cases.

Keep reservation maximum and block byte limit distinct. For example, an object
with common-short and long-tail dictionaries can use these schema bounds:

```cpp
sp::description common{.maximum = 255, .byte_limit = 16384, .full = true};
sp::description tail{.maximum = 65535, .byte_limit = 65535, .full = true};
```

Both omit U and admit two-byte geometry entries. The long-tail bound permits an
individual 65,535-byte value without requiring four-byte checkpoints for the
theoretical 256 × 65,535 extent. An allocated payload span alone cannot establish
that persistent encoding bound. The owner can grow within the schema bound or
seal and start another block; schema bounds do not change with allocation size.

For a growing block, request only the payload extent needed now and reserve
address space separately. In a shared allocation, putting metadata before the
payload leaves an allocation tail that can later be reclaimed; independent
planes also work. On sealing, `view::extent()` includes all reservations still
owned by the block, including retired values and slack. A sum of used/live
lengths is not a safe replacement for that extent.

An Orbital owner can relinquish unused suffix access with
`allocation::relinquish_tail`, then request backing maintenance with `trim_cold`.
Retained readers protect their captured footprints. Include every sidecar and
readable padding byte in the retained prefix, and rebind views to the contracted
windows. The [allocation contract](../../../orbital/README.md#allocation) owns
settlement, growth conflicts and accounting. Neither exhausting ranks nor
compacting packed bytes automatically returns physical pages.

Width is useful when byte limits leave blocks partly occupied because its length
groups grow with occupancy. Direct pays its endpoint directory upfront and favors
cheap point lookup. For the long-tail schema above, full Direct needs 546 metadata
bytes versus Width's 582 at 256 assigned slots, but Width needs only 102 at
1–16 slots. These totals include directory, lengths and control, omit allocator
padding, and are layout arithmetic, not measured timing. Full reservations and
occupancy can therefore change the geometry decision too.

The [ordinary example](../../examples/streampack/ordinary.cpp) constructs a Width
block, appends a value and an empty value, retires a rank and packs it. It also
shows source-qualified effects. New storage must be `initialize`d. External bytes
must pass `validate`; ordinary rebinding after growth need not rescan previously
admitted contents. Newly appended groups initialize all readable vector padding.

## Read at the useful grain

| Needed result | Starting path |
| --- | --- |
| One rank | Checked `value` or `length`; a retained typed reader for repeated points |
| Consecutive live values | `range`, carrying geometry across ranks |
| A few ranks in caller order | `select` without scratch |
| Many shuffled ranks from one block | Scratch-backed `select`, paying one starts decode for that operation |
| Offsets/lengths feeding a native predicate or reduction | `native::read16` inside the complete typed consumer |
| Rank containing a known byte offset | `reverse`, rather than testing every slot |

The [implementation measurements](../../../workbench/spikes/streampack/IMPLEMENTATION.md#native-primitive-findings)
show why repeated ordinary point calls are a poor substitute for carried ranges
or dense selection. The crossover for paid decoding depends on occupancy and
reuse; there is no universal selection-count threshold.

`value` and `length` check rank and liveness. `reverse` maps an occupied byte offset
to its rank and tombstone state, including reservation slack. No zero-width slot
owns bytes. Offsets beyond the current occupied extent have no owner.

`range` visits live values in original rank order while carrying geometry. `select`
preserves requested order and duplicates, skipping retired ranks. Sparse selection
uses direct point decoding; the scratch overload pays once to decode all starts
for dense reuse. Scratch and encoded inputs must be disjoint. Neither operation
retains callbacks or decoded state beyond its return. Charge scratch setup to
the selection that uses it. If an authored consumer retains decoded starts,
geometry changes invalidate them; storage lifetime alone is not enough.

For repeated point work, dispatch once around the complete consumer. Here
`ranks` is a sparse request list; use `range` for a sequential traversal:

```cpp
sp::author::dispatch(block, [&](auto reader) {
    for (unsigned rank : ranks) {
        const auto value = reader.get(rank); // Checked rank/liveness.
        if (value) consume(*value);
    }
});
```

An admitted caller can use `reader.value(rank)` or `reader.locate(rank)` directly.
For a population with one shared layout, `reader.rebind(other_named_view)` retains
that schema preparation while changing windows. The owner proves exact shared
layout identity and content admission. This keeps schema loads independent of the
lookup's block/rank. Reconstructing preparation at every point loses that benefit.

`author/native.h` exposes `native::read16`: sixteen offsets and lengths in native
registers plus an original-rank live mask. It shares prefix bodies with ordinary
reads and paid decoding. Consumers can fuse filters/reductions without an obligatory
intermediate array. NEON, AVX2 and AVX-512 have explicit implementations.

The [composition example](../../examples/streampack/composition.cpp) exercises the
existing Ikea chain machinery with both scalar location and native group carriers.
Resolve schema dispatch before constructing the typed stage. Keep `read16` and
its consumer together so a native packet need not cross an aggregate-return
boundary through memory. Use a CPS boundary for useful work around an unresolved
dependency; keep ready arithmetic inline. For cold independent requests, the
owner can choose Loom's Ring using complete-consumer evidence, as described in
[Building fast consumers](../consumers.md). Adding stages to one dependent lookup
does not create memory-level parallelism.
`visit_value_metadata` gives exact admitted hint windows to the owner's policy.
It neither issues prefetches nor establishes readiness or authority to read.

## Mutate and publish

Append reserves a new rank and initializes its entire reservation. Replacement
keeps its reservation and writes the new used bytes; it leaves old slack initialized.
A reservation-changing logical edit appends elsewhere and the owner retargets its
holder. Retirement changes only the tombstone. No operation silently renumbers ranks.

`soft_pack` preserves all ranks and tombstones, removes retired reservations and
optionally shrinks live reservations to used length. Fixed nonzero geometry refuses
packing that would change reservation widths. `transfer` constructs a disjoint
replacement from selected live ranks in caller-chosen order, retaining duplicates.
It supplies hard-rewrite mechanics; deduplication, repartitioning, remaps, shared
holder detach/retire rules and actual holder repair remain owner decisions.

Checked refusal preserves bytes and effects. Successful mutation reports actual
issued spans, including preserving metadata stores and count/extent writes. Effects
borrow the stable named destination view. Reserve effect capacity before execution;
hooks cannot fail or suspend. A zero-byte append may publish only count. Direct
needs no duplicate stored extent; Width publishes its separate extent coherently.

Growth through Orbital may preserve or move the allocation. End borrowed access
before relocating it, resolve aliased inputs first, then rebind changed windows.
Old readers must retain the payload and every interpretation sidecar together.
An allocation hold by itself does not grant permission to change their contents.

Build `ikea_streampack_check`, `ikea_streampack_boundaries_check` and the two examples,
or run `ikea_validate`. [Behavior checks](../../test/streampack/README.md) identify
independent oracles, boundary coverage and native execution checks.
