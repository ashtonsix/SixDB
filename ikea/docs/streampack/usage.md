# Using StreamPack

StreamPack stores variable-length values in caller-owned byte storage. A block
assigns up to **256** stable ranks; each reservation contains 0–65535 bytes,
subject to a smaller schema maximum and an independent block byte limit. Live
empty values have ranks. Retired values keep their reservations until packing.

The payload has no headers. Curated sidecars supply geometry, optional used lengths
and optional tombstones. Ikea allocates nothing, acquires no version and does not
publish a transaction. The enclosing collection supplies those services through
[its owner protocol](../integration.md).

The [implementation qualification](../../../workbench/spikes/streampack/IMPLEMENTATION.md)
records native correctness, codegen, performance and real owner/scheduler evidence.
Experimental packed/padded recipes remain in Workbench. The [reference](reference.md) specifies stored bytes, checked
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

`full` derives U from R. `immortal` omits tombstones. `fixed_reservation` derives
all geometry; zero-width schemas need only count and optional tombstones. Mutation
cannot introduce a fact the schema omitted. Native lane sizes are implementation
choices derived from schema bounds, not additional public geometry names.

The [ordinary example](../../examples/streampack/ordinary.cpp) constructs a Width
block, appends a value and an empty value, retires a rank and packs it. It also
shows source-qualified effects. New storage must be `initialize`d. External bytes
must pass `validate`; ordinary rebinding after growth need not rescan previously
admitted contents. Newly appended groups initialize all readable vector padding.

## Read at the useful grain

`value` and `length` check rank and liveness. `reverse` maps an occupied byte offset
to its rank and tombstone state, including reservation slack. No zero-width slot
owns bytes. Offsets beyond the current occupied extent have no owner.

`range` visits live values in original rank order while carrying geometry. `select`
preserves requested order and duplicates, skipping retired ranks. Sparse selection
uses direct point decoding; the scratch overload pays once to decode all starts
for dense reuse. Scratch and encoded inputs must be disjoint. Neither operation
retains callbacks or decoded state beyond its return.

For repeated/fused work, dispatch once around the complete consumer:

```cpp
sp::author::dispatch(block, [&](auto reader) {
    for (unsigned rank = 0; rank < reader.size(); ++rank) {
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
Use a CPS boundary for a useful unresolved dependency; keep ready arithmetic inline.
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
