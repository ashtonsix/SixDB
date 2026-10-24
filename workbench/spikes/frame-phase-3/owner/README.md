# Frame description, binding and owner composition

This is the phase-3 owner-side experiment for checklist A1/A2/A4/A6/A10,
A7–A9/B8/B9 and the owner account of B12. It uses the **shared**
[stage implementation](../proto/frame.h); there is no second encoding tree.
It is research, not a promoted `ikea::frame` interface.

The earlier [end-to-end prototype](../proto/e2e.cpp) established retained
group Parts and exact old readers, but attached every group for each write
and kept interpretation in the driver. Its 9–17 µs draft/apply/emit cost was
mostly owner work. Phase 1's
[lifecycle account](../../frame-phase-1/lifecycle/PROGRAM-COSTS.md) also showed
that a logical fold grain does not imply independently reclaimable backing.
This experiment retains the existing group images, binds only admitted
groups, and gives their interpretation real lifetime ownership. Historical
timings do not establish this candidate's performance.

## Five different descriptions

| Description | Contents and lifetime |
| --- | --- |
| Logical contract | Ordered non-null UInt64 rows; exact unsigned 128-bit SUM, UInt64 COUNT; empty SUM/COUNT zero. Signed, floating, NULL and alternate orderings are not admitted here. |
| Fitting policy | Exception penalty/cap and reserved slack; supplied when constructing or rebuilding. Changing policy cannot reinterpret an existing image. |
| Actual representation | Immutable canonical pipeline bytes, logical row extent, group extents and logical schema identity. Retained by the material, not a transient builder. Per-block fitted bases/widths and exception membership are data in its group images. |
| Bound execution | Reader/stage objects and borrowed image addresses, availability and any operation plan. Invalidated by mutation or replacement of the interpretation they were built from. |
| Evidence and costs | Measured costs, fitted statistics and their validity/basis. They are neither decoding parameters nor zero when unknown. |

[description.h](description.h) provides a bounded, versioned description in
Ikea's existing five-byte envelope (`Fr`, version 1). Node identities and
ordered edges preserve sharing; child coordinates distinguish identity,
checkpoint, present-rank and record-field relationships. Decode checks exact
length, parameters, topology and reachability before a binding is attempted.
This is an envelope for composition, not a replacement SeriesPack format.

The executable subset is group-local `pack`, optionally under `bound`, with
the three existing SeriesPack presets. Intervals/checkpoints, shared tuple
records, rank maps and split nodes can describe an extension's **shape**;
they round-trip but execution admission refuses them. Shape validation does
not pretend to prove their future byte law or operation semantics. Intervals
attachment and its own metadata remain work with the stage owner; the
prototype's generic child attach is insufficient when child coordinates differ.

## Admission and partial binding

[group_image.cpp](group_image.cpp) validates Frame's parent composition before
the prototype's trusted `attach`. It checks block rows/widths, contiguous
exception ranges, sorted unique positions, array membership/slots, widths,
row counts, exact leaf extents and total input length. It also uses Ikea's
actual compiled reader to prove the zero residual at every exception and
the absence of logical base-plus-field overflow. Leaf validity alone does
not prove either condition. Validation is a cold admission cost, not a
per-query charge.

The existing headless group sections remain little-endian: bound's block
descriptors and reserved exception area, then pack's block descriptors and
width arrays. The immutable description identifies this composition law.
Inactive reserved slots and unused leaf padding do not carry logical values;
this prototype does not require logically equivalent data images to have one
unique byte string. Canonical **description** encoding is exact and independent
of those data bytes. The current trusted prototype operates on little-endian
hosts; a portable description does not grant a big-endian execution backend.

`binding::attach(description, rows, groups)` receives only the groups the
caller has acquired. An absent group has no address and no halo requirement.
Duplicate/out-of-range group identities or inconsistent images refuse.
`available_blocks()` reports what was attached. Every checked operation
rejects an unavailable requested block before entering a stage, rather than
silently reducing the request to available data or interpreting it as zero.
Range reads validate the entire request before writing output. Fine masks
use original-row coordinates and cannot select beyond a final partial block.

Selections express demanded work, not physical read authority. This first
recipe admits the entire attached group window; it does not claim fine-mask
isolation within that group. Future delta/checkpoint binding must admit its
dependency closure before address formation. A point plan borrows the complete
immutable binding and cannot be reused after a private draft changes its
exception arrays, even when the image address happens to stay the same.

## One group, one private draft, ordered commands

`prepare_group` is an **afterimage** route:

1. Admit source readability, row/group coordinates, pipeline and image.
2. Admit source-copy size, command count and reusable journal capacity; allocate
   private preparation resources. Allocation failure can still refuse privately.
3. Copy and attach the touched group once, keeping original part coordinates.
4. Execute commands in order. Classify from the private interpretation, declare
   private apply effects, check journal capacity, then apply; exhaustion can
   rebuild only this group. It never falls back to reading a whole part.
5. Resolve issued private stores and check that declarations cover them.
6. Derive exact logical SUM/MIN/MAX, emit once, validate the final image and
   enforce the output-size budget.
7. The owner captures the fresh group together with certified retained sources
   for untouched groups, and publishes data and matching derived state.

Refusal drops the private candidate. The source image and published state are
unchanged, including when a later ordered command fails after earlier private
commands succeeded. No raw pointer into preparation workspace escapes.

The budget bounds copying, command count, peak reusable **per-command** journal
capacity and output size. It does not yet prove a hard bound on all allocator
overhead. `issued_bytes` and `records` count private apply stores cumulatively;
rebuild and emission construction stores are not in those counters.
`copied_bytes` and `new_bytes` report source copying and the final image. The
history accounts independently retained bytes separately. These counters must
not be labelled physical memory-bus traffic.

This distinction is necessary: current `bound::attach` copies descriptors
and exception vectors into binding workspace, while the leaf borrows draft
bytes. Granting an external image is not granting that private workspace.
The published physical effect here is replacement of the finished group;
the journal describes private computation. This fixture does not establish
an in-place publication contract or a lock manager. In-place binding still
needs explicit descriptor placement/workspace and owner-store admission.

## Interpretation and publication lifetimes

[material.h](material.h) supplies a concrete compile-once public Orbital
material. Immutable interpretation is retained independently of payload;
selected Parts retain what they need to decode and dispose themselves without
keeping sibling payloads. The resource outlives every owner, Scope and escaped
Part. The material's identity/resource checks are necessary admission, not a
proof of logical equivalence.

The original Scope/basis remains alive through Frame preparation and Orbital
packet preparation. An unrelated publication during Frame preparation makes
that basis stale: the owner must reject or explicitly revalidate and retry.
Orbital admits only one accepted packet at once, so there is no invented
competing-publication race between successful `Store::prepare` and `install`.

A SET that exhausts slack and rebuilds is still a logical change: publish the
next logical version and its exact contribution. A pure equivalent conversion
is separately proved from logical values and uses Orbital's same-version
`adopt`, with zero logical contribution. Copying, destination sizing and
policy choice remain owner responsibilities. Old readers retain their own
row extent and pipeline through append and conversion.

## Checks and scorecard

`frame3_owner_description_check` independently checks canonical bytes,
malformed descriptions, shared identity and placeholder refusal.
`frame3_owner_material_check` checks interpretation/payload/resource lifetime,
escaping selected Parts, append, source reuse and adoption compatibility.
Group-image and complete-history checks extend these to real stage images;
only completed checks are evidence, not this description of intended coverage.

Configure the existing spike with `-DSIXDB_SPIKES=frame-phase-3`, then build
the needed `frame3_owner_*_check` targets. Use the pinned Linux toolchain;
prefix Linux commands with `orb -m ubuntu` in the macOS workspace.
The [qualified controls](../controls/README.md) and
[scorecard](../controls/SCORECARD.md) distinguish whole operations, preparation,
residence conditioning, retained bytes and remaining unmeasured cells.
