# Composing and extending TuplePack

Start with the executable [composition example](../../examples/tuplepack/composition.cpp).
Its mutation writes A and B, while maintenance observes A, B and untouched C.
A new child has a different extent, bit offset and owner; the parent retains its
logical input type. Old parents retain their original source bindings; aliases
observe writes to shared storage. Preserving a reader's data edition requires
the owner's versioned mapping/visibility protocol. Rebinding does not migrate
bytes or retain a historical version by itself.

`composition::bind_group(children...)` composes whole mutation commands. Groups
can themselves be children. Preparation checks actual destination fields for
overlap, and invocation checks every input packet and the summed effect capacity
before making changes. `erased_mutation<Input, Maintenance>` erases one
whole range; the chosen concrete traversal remains inside its function.

Bound constructors can also be group leaves. This admits every unit's input and
capacity before initializing any part of a larger record. Their destination
footprint includes all unit bits, including zeroed spare bits. A before-observation
must not read uninitialized construction storage; use a new-only law or private
caller contributions for that case.

An observation projection is independent of the mutation tree.
`composition::projection(readers...)` produces a tuple of packets; a caller can
provide another projection with `size()` and `get_unchecked(row)`. `observation`
uses its law's `needs_before` and `needs_after` independently. The law's `observe`
receives the original row followed by whichever values it requested. No-value
invalidation still runs. Old observations precede all child stores; new
observations follow all child stores, including multiple packets sharing bytes.

The projection may alias mutable leaves and read untouched dependencies. Its
view extents and owner leases must be admitted together with mutation resources.
Callbacks should normally accumulate into private retained state. Persistent
summary changes require their own pre-write effects, admission and publication;
capacity in the data journal does not reserve those resources. A shared hash
signature cannot remove a field's bits by subtraction: another field may share
the witness. Exact rollup removal may need a wider reconstruction or witness
counts. See the [maintenance review](../../../workbench/spikes/tuple-layout/maintenance-review.md).

## Keep native payloads native

`author/native.h` exposes explicit ISA bodies and compiled endpoints.
`native::read`/`write` use `IKEA_TUPLE_CC`; AVX2 needs its regcall convention to
avoid the default SysV aggregate return through memory. Inline `read_body` and
`write_body` use the same controls. Their inputs and effects are already admitted;
kernel bodies do not acquire buffers, publish or suspend.

`native_reader` and `native_writer` adapt ordinary plans to these bodies.
`native_writer` is a mutation-group leaf, so the same whole-call admission,
effects and observation shell can enclose a register-valued operation. `word<I>`
extracts a compile-time 64-bit piece into a GPR for caller-owned struct assembly.
Callers perform wider signed/float semantics explicitly; Ikea does not infer
those from byte code ranks.

Batch readers select a useful native execution grain without changing storage:
`batch_reader<4>` returns 16 projected bytes per row and `<2>` returns 32. Their
active mask is a row mask, and inactive pointers may be null. The returned zero
bytes do not replace that mask. Authored consumers can use ISA instructions
directly on the packet.

## Normalize wiring before execution

For selection/shift/mask/disjoint-OR transforms, `author/routes.h` records one
source bit or zero for each output bit. Compose this wiring with
`decoding_routes(layout, map)`, then call `prepare_routes` once into caller-owned
term storage. `apply_routes` recognizes byte permutations/rotations structurally;
otherwise it evaluates the complete general lowering. Controls and applicability
can change after nested substitution without changing the parent operation.

Preparing a combined route can eliminate intermediate packet materialization.
When adding a lowering, check its applicability against an independent bit
reference and test the fallback for layouts outside that proof.

## Share bodies across execution styles

`chain<Slots, Mask>` uses Ikea's shared bounded straight-through mechanism. A stage
body receives bindings, original row, active mask, `native::pipeline_values` and
ordinal, then returns values, mask and an early-completion flag. The same body can
be called inline or wrapped with `chain::stage<Body>`. The table holds up to
`Slots-1` stages plus completion; slots are a power of two, aligned to at least
64 bytes. Flattened vector arguments preserve the carrier at continuation hops.

Enter a pipeline with `chain::run`, which bridges the caller's ABI to its stages.
The compiler-specific entry requirements are documented with the
[shared implementation](../source.md#shared-execution-entry).

The [execution check](../../test/tuplepack/execution.cpp) tests early completion,
native handoff, normalized/general routes and sparse batch coordinates. The
[benchmark](../../../workbench/benchmarks/tuplepack/README.md) compares inline and CPS
decode/repack with the same bodies. Choose enough work per stage to amortize
continuation overhead. Suspension occurs
after returning to the owner, with explicit retained state, not inside a kernel.

## Change the right source boundary

Code-map admission and ISA-control preparation live in compiled
`src/tuplepack/prepare.cpp`; description recovery in `description.cpp`; scalar
point and compiled native endpoints in `kernels.cpp`; cold route lowering in
`routes.cpp`. Native instruction bodies and exact bounded memory helpers live
under `tuplepack/detail/native/`; `author/` exposes the supported composition
surface. Operation shells own range/capacity admission and logical maintenance.

Use the independent wire, operation, execution and owner tests for changes to
their respective contracts. Then measure the ordinary endpoint and representative
consumer, not only the inner kernel. Retain a specialization when its supported
use and measured benefit justify the extra code/control footprint.
