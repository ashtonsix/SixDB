# Exact-origin Allocation owners

This unapplied control removes the repeated body address from exact-origin,
independently allocated Parts: each Part retains only its 8 B `Storage*`, down
from 16 B. That saves 512 B / 2,048 B for 64 Objects with 1 / 4 Parts. Physical
Allocation metadata remains charged separately.

Capture automatically selects the compact layout only when every independent
Part starts at its Allocation origin. Slices, mixed placements, grouped and
shared owners retain explicit addresses. Full-range Holds still precede release
of every original Allocation. The private bridge uses existing
`FiniteGroup::resolve` or `Window::address`; it adds no public Allocation API.
Holds prevent finite relocation/rebinding and keep the owner and geometry alive;
Window growth preserves its reserved origin.

Selection and View remain 8 B; `Result<View>` remains 16 B. View tags the low bit
of an aligned owner-slot pointer, preserving valid null-address empty Parts.
This assumes the pinned native pointer representation. Export still materializes
one direct address and retains its selected owner; its layout stays 80 B.

The saving adds lookup work. Emitted AArch64 code adds a View tag branch and, for
finite owners, dependent route/geometry loads through two direct boundaries:
`allocation_origin` and existing `FiniteGroup::resolve`. It is not a demonstrated
performance win. Direct-domain8/4 controls are not composed or qualified here.
The separate read-fusion patch must use `part_address` when combined with this
control; its older `address_slot` spelling only handles explicit addresses.

## Reproduce

In an isolated source tree containing these three control files, apply
[`origin-owner.patch`](origin-owner.patch). Use its existing native build with
`SIXDB_SPIKES=orbital-mvcc`, or configure one with the pinned Linux toolchain:

```sh
git apply workbench/spikes/orbital-mvcc/metadata/orbital/shape/controls/origin-owner/origin-owner.patch
orb -m ubuntu cmake -S . -B build/origin-owner -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++-21 -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=orbital-mvcc
orb -m ubuntu cmake --build build/origin-owner --target orbital_mvcc_shape_origin_owner_check
orb -m ubuntu ctest --test-dir build/origin-owner \
  -R '^orbital_mvcc_shape_origin_owner_check$' --output-on-failure
```

The focused check uses real Allocation geometry and NDEBUG-active assertions:
all 256 identities, finite/Window growth and relocation refusal, exported
lifetime through allocation churn, slices/grouped fallback, empty Parts and
budget refusal/retry. It passes with Clang 21.1.8, AArch64 Linux, `-O3 -DNDEBUG`.
The normal CMake target was checked while reusing the already-qualified native
dependency objects/libraries. The previously qualified postimages also passed
the existing Shape histories, including late Hold refusal cleanup/retry, and
compiled the actual read bridge and Shape writer TUs. This packaging change
reruns only the focused target; it adds no timing, sanitizer or broader catalog
qualification.
