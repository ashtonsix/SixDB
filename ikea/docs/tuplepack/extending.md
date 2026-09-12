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
before making changes. All children share their original-row packet shape.
`erased_mutation<Input, Maintenance, Rows>` erases one whole range; the chosen
concrete traversal remains inside its function. Rows defaults to one and remains
part of the erased contract even when two shapes have identical input C++ types.

Bound constructors can also be leaves of one-row groups. This admits every unit's
input and capacity before initializing any part of a larger record. Their destination
footprint includes all unit bits, including zeroed spare bits. A before-observation
must not read uninitialized construction storage; use a new-only law or private
caller contributions for that case.

An observation projection is independent of the mutation tree.
`composition::projection(readers...)` produces a tuple of packets; a caller can
provide another projection with `size()` and `get_unchecked(row)`. `observation`
uses its law's `needs_before` and `needs_after` independently. The law's `observe`
receives the original row followed by whichever values it requested. No-value
invalidation still runs. Within each mutation window, old observations precede
all child stores and new observations follow them, including children sharing
physical bytes. A range repeats this bracket per window; whole-call admission
precedes every window, but observations are not a whole-range snapshot.

A packet projection supplies `rows` and `get_unchecked(first, active)`.
Its law uses `observe_batch(first, active, ...)`, with the requested before/after
packets following those arguments. Its shape must match the mutation's Rows.
All old values precede all child stores; all new values follow them. Point
observers also work with packet mutations: the shell retains each active row's
demanded before-state until the complete packet has been written.

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

`native_reader` and `native_writer` infer Rows from ordinary bound operations.
The reader separates checked `admit(first, active)` from register-valued
`get_unchecked`; a retained extent proof can cover many calls. The writer offers
checked `set/replace`, explicit trusted entries, and is a mutation-group leaf.
The same whole-call admission, effects and observation shell therefore encloses
register-valued operations. `word<I>`
extracts a compile-time 64-bit piece into a GPR for caller-owned struct assembly.
Callers perform wider signed/float semantics explicitly; Ikea does not infer
those from byte code ranks.

`reader<64, Rows>` and `writer<64, Rows>` select a native execution grain without
changing storage: 64×1, 32×2, 16×4, 8×8, 4×16, 2×32 or 1×64 projected bytes×rows.
The active mask names original rows. `native::row_mask<Rows>(active)` expands it
into a byte mask when a transform needs one. Returned zero bytes do not replace
the row mask. The [packet example](../../examples/tuplepack/packets.cpp) builds a
native update over one-byte tuples with sparse rows and a final tail.

Keep shared hot bodies inlinable through their wrappers. An outlined helper
lambda capturing a native carrier can introduce aggregate ABI transfers even
when the leaf kernel itself is always-inline. Inspect the finished consumer and
explicit compiled endpoint when changing dispatch or traversal boundaries.

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
`src/tuplepack/prepare.cpp` and `packet_prepare.cpp`; description recovery in
`description.cpp`; scalar point/native endpoints in `kernels.cpp`; packet
endpoints in `packet.cpp`; cold route lowering in `routes.cpp`.
Native instruction bodies and exact bounded memory helpers live
under `tuplepack/detail/native/`; `author/` exposes the supported composition
surface. `detail/mutation.h` shares whole-call admission between buffered/native
leaves and recursive groups; `detail/window.h` owns original-row windows and
maintenance brackets. Specialized traversal stays with the physical body.

Use the independent wire, operation, packet, execution and owner tests for changes to
their respective contracts. Then measure the ordinary endpoint and representative
consumer, not only the inner kernel. Retain a specialization when its supported
use and measured benefit justify the extra code/control footprint.
