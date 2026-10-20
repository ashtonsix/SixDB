# Supplying material

Orbital publishes caller-supplied material. The caller defines its format,
geometry, validation, cloning and conversion; Orbital owns catalog and physical
lifetimes. A storage adapter and its concrete instantiation TU stay with that
caller. The [caller](../test/extension.cpp) and its
[material TU](../test/extension_material.cpp) use plain integer data.

## Compile once

Ordinary caller headers include `orbital/object.h` and, for native physical
storage, `orbital/representation.h`. One implementation TU includes the needed
opt-ins:

```cpp
#include <orbital/instantiate/object.h>
#include <orbital/instantiate/representation.h>
```

It supplies `Physical` / `Control` and explicitly instantiates their cold
operations. Link `orbital::orbital`; no source-tree include path is needed.
The opt-ins compile against the same Orbital headers/library as the caller.
Ordinal `selection.part(index)` is inline through the ordinary header; read TUs
do not include the cold definitions. Its `Control::part_count()` is visible for
inlining or has a compiled definition when Control geometry is opaque.
The exact Control operations
and selected-proof obligations are beside the
[Representation definitions](../include/orbital/instantiate/representation.h).

## Compose retained and new bytes

`source(part)` borrows a retained Part's physical backing and certified footprint.
`source(selection, index)` borrows directly from a protected Representation,
avoiding an intermediate escaping Part. Both return `Result<PartSource>`;
neither allocates nor retains. Keep that Part or Representation, its proof and
resource alive and fixed through capture, including allocation callbacks.

For a native-backed old Selection and a fresh Allocation, a new two-Part Control
can reuse the old first plane and supply a fresh second plane:

```cpp
auto kept = source(old_selection, 0);
if (!kept) return std::unexpected(kept.error());
kept->id = next_control.id(0);
const std::array sources{
    *kept,
    PartSource{next_control.id(1), allocation::bytes(fresh), {}, &fresh}
};
return capture_representation(memory, next_control,
    PartOwners::independent_allocation, sources);
```

The new Control admits each destination Part's identity, extent, alignment and
padding. A retained source's body can select a slice, but the complete new body
plus readable padding must fit its old certified body plus padding. This supplies
physical bytes, not the source's Object/version label or a logical-equivalence
proof. Publication and conversion keep their existing catalog rules.

Capture acquires another use of the source's physical backing. It retains no
ancestor Part, Representation or source Control. The new Control must independently preserve
all interpretation and physical-owner disposal dependencies it needs. Each
resource must outlive its derived uses. A physical owner can cover several
planes, so selecting one plane does not imply reclaiming its siblings' pages.

A shared owner alone does not prevent a growable Allocation from relocating.
Keep the captured address and full readable footprint stable. Capture seals the
body and readable padding; physical retention grants no permission to write them.
The caller may authorize writes to physically disjoint storage. Unpublished
captured owners and prepared packets also protect their footprints.

An owner can contain a real `allocation::Hold` for a slice instead of the whole
Allocation. Acquire all body-plus-padding Holds before calling
`relinquish_tail(allocation, 0)` and releasing the writer; release alone does not
enable live-tail trimming. Captures and escaping Parts keep that counted owner.
Current Window trimming can reclaim an eligible suffix, not interior holes;
finite Holds still retain their bound Home. This control adds Hold and
shared-owner storage per slice, so compare the complete cost with native capture.

## Ownership and costs

`independent_allocation` accepts fresh Allocations and retained native backing.
`independent_shared` accepts shared backing and retained shared Parts.
`independent_mixed` accepts both kinds. The grouped modes require one identical
physical owner across all Parts and retain or transfer it once. Independent
entries each own a use, even when several retained sources share backing;
duplicate fresh Allocation sources remain invalid there.

Capture reserves metadata, validates sources and acquires all retained uses
before consuming a fresh Allocation. A refusal unwinds acquired uses and leaves
fresh Allocations intact. Native pins require the backing's original adopted
Lane and can refuse on saturation; composition does not move storage to the
destination Pool. Existing return and shutdown lifetimes still apply.

Native and shared independent entries keep their 16- and 24-byte layouts. Mixed
entries use 32 bytes. Borrowed source certificates exist only in the capture
request. Representation, Part and View sizes stay unchanged; capture caches
address-slot spacing so binding does not decode the owner kind. Reads still use
ordinary byte pointers, with no per-point ownership operation. The owning
consumer decides which planes to copy and when to retain or publish them.
