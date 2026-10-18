# Memory continuations

`Ring<Native...>` rotates independent memory continuations with one native
signature. Different callback functions and operation owners can share that
rotation. Owners keep operation state referenced by native pointer arguments
qualified and alive until completion or returned-boundary cleanup; the Ring does
not acquire that ownership itself. Start with the [operation usage guide](usage.md)
for execution choice, admission and lifetime, and [hardware usage](hardware.md#bind-demand-and-adopt-policy)
for demand binding and snapshots. The [hint example](../examples/hints.cpp) is a
complete maintained consumer: configure a bank once, admit independent chains,
prefetch their successors, drain, then close and release storage.

## Callback and tail transfers

The first callback parameter is `Ring::Cursor`, followed by `Ring::Slot*`, any
shared prefix and the declared local arguments. Carry the Cursor unchanged
through each tail transfer; `cursor->` accesses the owning Ring. Loom alone
updates its private countdown. Do not retain a Cursor in operation state or
reuse one from an earlier visit. Three tail transfers use that exact signature
and calling convention:

- `yield(ring, slot, values...)` saves changed scalar/vector arguments. A prior
  `complete(*slot)` retires the frame instead.
- `yield_unchanged(ring, slot, values...)` reuses its stored arguments. Pass the
  original callback arguments to satisfy `musttail`; changes to pointees are
  ordinary operation state, while changes to argument values are not saved.
  This path keeps the frame live and does not inspect the completion flag.
- `finish(ring, slot, values...)` retires the current frame after result
  publication, without storing arguments or setting a completion flag.

Use `[[clang::musttail]] return` for all three. A stage may change
`slot->function` before yielding. A stable one-stage callback can retain its
existing function. Declare callbacks with `LOOM_NATIVE_CC` and the exact native
signature. Native bodies must not throw or pass callback-local objects into later
visits. Omit C++ `noexcept` on these callbacks: the pinned compiler rejects its
combination with `musttail`. Qualify concrete scalar/vector families with that
compiler; the header's calling convention is part of the transfer contract.

The Cursor carries the Ring pointer and remaining visit count through the native
register transfer. Ordinary handoffs test and decrement that count, reload the
next function and arguments, and publish the next frontier before dispatch.
They perform no budget loads/stores or visit-counter updates. Budget exhaustion
or the last completion publishes the remainder once; `spin` derives the exact
visited count on return. Admission, retirement and FIFO replacement need no
rotation rebuild or accounting walk. Scalar/vector operation arguments still
save to and reload from their Slots; they do not persist in registers across
other jobs.

## Prepared state and argument traffic

For facts common to every frame in a rotation, use
`Ring<Local...>::WithShared<Common...>`. Configure with
`ring.configure(worker, capacity, {common...})`; admission still supplies only
`{local...}`. The callback and all three tail transfers accept the common prefix
before the local arguments. `spin` loads the prefix once and forwards it through
each native jump; Slots contain only local arguments. Forward the received
common values unchanged, including through heterogeneous callback families.
Changing a forwarded common value would affect other jobs in that spin while
the cold bound value remained unchanged. The Ring acquires no ownership of
common pointees; they remain qualified whenever pending work can use them,
until a valid replacement or owner close.
Register capacity remains finite: inspect concrete scalar/vector signatures for
spills rather than assuming a wider prefix is free.
An opaque common context pointer still leaves its fields to be loaded by each
callback. Frequently reused immutable kernel addresses and layout facts can
instead be prepared outside visits and carried explicitly in the prefix.
Qualify them for every pending frame, and compare the complete ready and cold
paths: saved field loads can be offset by additional register spills.

Ikea or another operation author owns kernel preparation, format-specific facts
and dependency stages. Reuse those kernels in ordinary execution and Ring
callbacks; keep their native carrier packing private to the operation binding.
Loom owns rotation, service budgets, argument transfer and worker hardware/pressure
policy. Mixed operations keep their distinct prepared facts in operation frames
and share only values that are invariant across the rotation.

The generic jump restores every declared local argument for the next callback.
If a packet is used only by the payload stage, keep it in a stable operation
frame and pass that frame pointer: metadata then never loads or re-saves the
packet. Payload loads and updates it where it is actually used. This also lets
different microtask kinds share a small pointer signature without restoring
each other's unused carriers. Native scalar/vector arguments remain useful
where the stage consumes them; ready kernel-to-kernel CPS within one visit can
retain register values without another Ring boundary.

`set_shared({common...})` replaces that prefix at a returned owner boundary.
Pending frames must retain their meaning and physical qualification under the
replacement. A shared table pointer only fits a rotation where every frame uses
that table; different operation owners can instead share worker policy facts and
keep their own table pointers local. The [hint example](../examples/hints.cpp)
carries a common node base while retaining per-job position, sum and output.

The common two-pointer Slot is 64 bytes, with function, next pointer and arguments
in its first 32 bytes. Other argument families retain their natural alignment.
The one-pointer Slot is 56 bytes with a 24-byte dispatch prefix; in a 64-byte
aligned bank that prefix crosses a cache-line boundary in two of eight Slots.
The layout trades fewer stored bytes for those split loads rather than padding
every argument family to a cache line.
The Ring publishes its next frontier before each callback so callback admission
can follow existing work without an extra author-supplied cursor.

## Compose ready work before rotating

Use an ordinary inlinable body for a small ready operation. Entering CPS charges
entry, context and completion even if every stage tail-transfers without an
ordinary call. Ikea's [ordinary filters](../../ikea/examples/filters/ordinary.cpp)
and [shared-stage example](../../ikea/examples/filters/composition.cpp) show the
two choices using the same useful bodies. The synchronous Ikea chain has its own
typed signature; a Loom callback still requires the exact Cursor/Slot/common/local
signature rather than a cast between stage types.

At an existing useful composition boundary, canonical 1/2/4/8-byte lane meaning
can let different decoders share substantial downstream code. Keep adjacent
cheap comparisons and reductions together after one decode. Retain direct
packed algorithms: converting a narrow packed filter into general decoded lanes
can cost more than the continuation itself. Expose immutable placement/stride
facts inside the actual body when available; checked binding alone does not
make runtime geometry a compile-time constant.

An opt-in head guard or `if (selected)` before a subsequent admitted filter stays
within the current ready stage. A cheap head can avoid a costly suffix, but mixed
easy/hard groups can improve latency while losing throughput. Likewise,
forwarding a sparse result mask can select a slower reader than keeping the
original admitted dense activity and intersecting afterwards. Ikea's
[consumer guide](../../ikea/docs/consumers.md) owns the format-specific choice
guidance; its [filter guide](../../ikea/docs/filters.md) and
[guarded example](../../ikea/examples/filters/guarded.cpp) give the exact calls.
The [measured comparisons](../../workbench/spikes/filter-composition/RUNTIME.md)
support selective sharing, not automatic CPS for each small kernel. The
[guard qualification](../../workbench/spikes/packed-filters/EARLY-GRADUATION.md)
retains winning and losing branches. These are warm finite consumers, not Loom
or DRAM speedup measurements or cache-residency tests.

Threaded callbacks still pay indirect dispatch and lose some compiler hoisting
available to a closed manual loop. Validate cold windows and prepare immutable
kernel facts outside visits where possible. An ordinary call can force a callback
frame onto its common path; inspect the compiled body, including cold failure
paths. An `always_inline` adapter does not force nested owner calls inline. A call
before phase dispatch can charge every phase for its frame and temporary arguments.
The [ARM caller comparison](../../workbench/notebook/retired-spikes.md#hashtable-promotion-and-curation--october-17-2026)
isolates inlining at that call while retaining nested cold helper attributes.
Check actual call removal, code size and complete-operation cost; this private
candidate still needs hot and combined qualification before wider selection.
Rotate at useful dependency boundaries; keep ready arithmetic in the current
visit. Hardware/pressure policy remains a returned-boundary snapshot, with no
hardware query or profile atomic inside a native handoff.

A ready stage can tail-transfer directly to a cold continuation with the same
native signature, forwarding the received Cursor, Slot and common prefix.
That transfer stays inside the current visit: it does not rotate the Ring,
restore another Slot or consume another visit. Keep any progress needed by the
cold continuation in qualified operation state, and bound the combined work
before its final `yield`, `yield_unchanged` or `finish`. Resolve the first deferred
item without replaying its ready prefix; preserve the remaining item order.
This can move ordinary fallback calls and their register saves out of a ready
body. Inspect both bodies: the cold continuation still pays its own calls and
spills, and a wider shared prefix can increase those costs.

That direct tail transfer does not update `slot->function`. Change the stored
function only if later rotation must resume a different stage. Also distinguish
the arguments received by an intermediate callee from those stored in the Slot:
if the direct chain changed local argument values, the final rotation must use
`yield` to save them. `yield_unchanged` preserves the previously stored values;
passing a changed value through its signature does not store it. Final retirement
uses `finish`; `yield_unchanged` does not inspect `complete`.

## Dependent prefetch and admission

Prepare [DemandClass](../include/loom/hints.h) outside visits against the minimum
contiguous window that every later address can use. Admission via
`push(callback, arguments, MemoryDependency{address, demand})` hints an active
frame's initial dependency; overflow waits for promotion before that hint.
A successful push accepts the frame, not a proof that its bytes arrived in cache.
A refusal consumes no inventory or work; the caller retains the unadmitted item.

Inside a dependent chain, consume admitted metadata, derive a qualified next
address, call `ring->prefetch(demand, address)`, then yield so other independent
frames can execute. The [hint example](../examples/hints.cpp) does this after
loading `node.next`. The next load may still stall; neither hint issuance nor
the returned spin count reports readiness. Allocations and stable versions must
already permit the load. An asynchronous producer or I/O wait belongs to the
enclosing Task protocol, not a guessed number of Ring rotations.

Disjoint planes require separate demands. Inactive items must not form payload
addresses. Reuse admitted envelopes while refreshing hardware/pressure snapshots
at returned boundaries; native visits perform only inline code selection and
bounded hint emission. Larger arbitrary contiguous-demand/unique-line emitters
remain [research controls](../../workbench/spikes/loom-locality-execution/FINDINGS.md),
not an additional product API.

## Entry bridge

`spin` enters through one fixed ordinary bridge, outside the per-visit path.
This preserves a realigned ordinary caller's stack base across the native
convention. With the pinned compiler, direct entry can clobber AArch64's frame
base and x86 rejects realigned ASan callers with a native exception boundary.
Only this bridge is excluded from sanitizer instrumentation; callbacks, state
accesses and tail transfers remain checked.

## Batching and service budgets

[IndependentStream<Key, Quantum, Lookahead>](stream.md) retains a known-key
prefetch/find loop across bounded native visits. Each frame stores a borrowed
span and index, while its typed callback keeps the canonical reader and local
reductions inline. It can share the same rotation with dependent stages through
the existing native signature. The [stream example](../examples/stream.cpp)
shows both kinds together with complete cleanup.

`prefetch_for_each<Lookahead>` runs that same loop synchronously without Ring
admission; choose the entry at the operation boundary according to useful
overlap, other work's service needs and measured complete costs.

[Cohort<State, Maximum>](../include/loom/cohort.h) is an optional bounded adapter
for doing a few independent item stages in one native visit. Cold binding checks
the span and active mask; `visit` touches active states, and `advance` removes
states whose callback returns true. Ragged groups keep only their survivors.
Different cohort types and grains can share one Ring signature through a stable
frame pointer. Size admission by useful independent states, and scale the visit
budget to preserve service intervals: a four-state cohort consumes up to four
item-stage visits per callback. Grouping amortizes dispatch but does not supply
missing overlap or justify delaying an operation's dependencies.

Configured width counts frames; count useful active states across cohorts when
choosing admission. Overflow is bounded waiting inventory, not extra current
overlap. Different callback kinds can fill a shared family when one kind has too
little independent work, while retaining their own bindings. Large grains can
amortize dispatch but lengthen dependency gaps and time before owner polling.
Bound item work, then choose visit/time service intervals outside the callbacks.
The current Ring budget is a visit count; it provides no fixed microsecond slice.

## Qualification

The [rotation check](../test/ring_rotation.cpp) compares exact phase/state traces,
admission, finite FIFO inventory, budgets and results with an independent logical
model. It includes heterogeneous stages, ragged completion, repeated callback
pushes, selective cleanup and the explicit pointer-state tail paths.
The [shared-prefix check](../test/ring_shared.cpp) includes local and shared
vectors, context replacement, mixed callbacks and destroyed removed borrowers;
the [cohort check](../test/cohort.cpp) compares mixed grains and ragged masks with
independent per-item execution.

The [stream check](../test/stream.cpp) compares exact hint/consume request indices
and heterogeneous rotation against independent sequence and queue models, with
duplicate keys, all misses, empty input, overflow and returned-boundary cleanup.
