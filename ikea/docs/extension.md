# Extending Ikea

Begin a module by defining its value domain, physical layout and operations.
Specify coordinates, access bounds, borrowing and failure guarantees so callers
can substitute representations under the same logical contract. SeriesPack's
bit-window joins and TuplePack's ordered code maps illustrate different
composition structures.

Use three distinguishable surfaces: ordinary caller headers, supported authoring
headers under `author/`, and implementation under `detail/`. Compile cold
description validation, control preparation, diagnostics and alias analysis in
independent TUs. Keep performance-sensitive bodies visible to authors and place
explicit ISA implementations where they can be found. Keep value types, error
vocabulary and traversal specific to the module's operations.

The shared facilities are:

| Facility | Why shared | What stays local |
| --- | --- | --- |
| `ikea/effects.h` | Both modules emit issued byte spans qualified by actual named sources; owners resolve them to storage identities | Kernel-specific footprints, tiling, admission and effect capacity calculations |
| `detail/overlap.h`, compiled `src/overlap.cpp` | Both need cold proofs for repeating occupied spans, including interleaving | Semantic bit conflicts, view rules and diagnostic meaning |
| `detail/native_chain.h` | Aligned bounded straight-through tables, flattened payload arguments and early completion are the same mechanism | Native carrier, mask meaning, stage grain, semantic body and error vocabulary |

Define maintenance dependencies separately from mutation destinations. A row
signature may need untouched fields, while an invalidation callback needs only
coordinates. Let the law request the values it needs and invoke it for each
selected row. SeriesPack's `sum_change` and TuplePack's observation projections
provide examples with different requirements.

An ordinary operation must retain the strengths of its native body. Bind
applicability and traversal outside the hot loop; erase a useful whole operation.
Avoid routing points through a general range driver or outlining one helper per
gathered row. Neither inline composition nor CPS should silently introduce a
materialized payload seam. Test the native ABI rather than assuming a C++ vector
aggregate returns in registers; AVX2's default SysV pair does not.

An owner needs enough information to admit source and destination extents,
input/selection disjointness, actual byte writes, maintenance dependencies and
effect capacity before mutation. Checked failures are call-local and leave all
outputs of that call unchanged. Hooks are no-fail/no-suspend. Persistent summary
writes need their own coverage, or the operation can leave private contributions
for the owner to apply. The owner controls leases, cancellation, resumable live
state, semantic editions and coordinated publication; in-place mutation is valid.

Use executable examples to teach ordinary calls, composition and ownership.
Validate the wire law independently, then exercise boundaries, inactive masks,
substituted storage, effects and owner lifetimes. Measure ordinary operations
and representative compound consumers on the target ISAs, including fallback
maps. Keep research decisions and comparative history with their Workbench evidence.
