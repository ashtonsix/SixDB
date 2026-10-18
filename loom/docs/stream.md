# Bounded independent streams

[`IndependentStream<Key, Quantum, Lookahead>`](../include/loom/stream.h) keeps a
known-key prefetch/consume loop inside bounded native visits. It borrows a
`span<const Key>` and stores the next completed index; it owns no scheduler,
reader, sink, allocation or physical protection. The
[complete example](../examples/stream.cpp) serves a stream and an unrelated
dependent chain in one actual `Ring<void*>` rotation.

Use this shape when the next independent keys are already available. A
[dependent continuation](ring.md#dependent-prefetch-and-admission) still derives
its next address from prior data and yields at that dependency. Both can share
the same Ring. For several independent resumable item stages, [Cohort](ring.md#batching-and-service-budgets)
retains individual active states instead.

## Binding and native use

Construct the stream from its stable input span before admission, with prepared
state and a result sink in the operation's typed frame. The caller establishes
their lifetime and access rights before admission; the stream binds or retains
none of them.
Construction issues no prefetch or useful work, and a refused Ring push consumes
neither. The cursor is three machine words on the supported ABIs, with no
separate job or staged-probe state per key. Its bodies may perform a lookup,
reduction or other bounded work; their meaning belongs to the caller.

`advance(prefetch, consume)` accepts synchronous noexcept bodies taking
`const Key&`. It returns true when every input index has completed. Both bodies
are called directly as lvalues without copying or erasing them. Each actual
native callback recovers its own typed frame, so unrelated kinds need no shared
type tag or second dispatcher:

```cpp
LOOM_NATIVE_CC void visit(Ring::Cursor ring, Ring::Slot* slot, void* opaque) {
  auto& frame = *static_cast<Frame*>(opaque);
  auto sum = frame.sum;
  const bool done = frame.stream.advance(
      [&](const Key& key) noexcept { frame.prefetch(key); },
      [&](const Key& key) noexcept { sum += frame.consume(key); });
  frame.sum = sum;
  if (done) [[clang::musttail]] return Ring::finish(ring, slot, opaque);
  [[clang::musttail]] return Ring::yield_unchanged(ring, slot, opaque);
}
```

Use concrete functors with visible call operators. Pinned Clang call-site
inlining keeps the authored body inside the loop even when it is large; an
unknown runtime function target can still add a call per key. Nested owner calls
retain their own [inlining decisions](ring.md#compose-ready-work-before-rotating).
Inspect the consume kernel as well as dispatch. The
[consumer reduction comparison](../../workbench/notebook/retired-spikes.md#hashtable-promotion-and-curation--october-17-2026)
holds scheduling fixed while changing cross-value vectorization and retaining
SIMD within each value. Its CPU-dependent results keep that choice with the
consumer's complete procedure; hot and combined qualification remain pending.

Keep reductions local to a quantum and publish them once before the final tail
transfer, as the example does. Preserve the callback's original frame pointer
for `yield_unchanged`.

### Hint policy

Bind demands and adopt the hardware/pressure snapshot at the ordinary owner
boundary through the [hint interface](hardware.md#bind-demand-and-adopt-policy).
Reader bodies can use that mapping or a qualified fixed control. An ordinary
body calls `loom::prefetch(policy, demand, address)`; `Ring::prefetch` delegates
to the same mapping. The helper owns sequencing, chooses no hardware procedure
and performs no clock or policy lookup per key. Neither the synchronous loop nor
its ordinary hint body needs a configured Ring.

The snapshot overload selects a Hint at runtime. The compiler may hoist invariant
loads, but the loop interface does not promise to move selection outside the
loop. Charge the remaining emitter work rather than assuming equivalence to
fixed template hint controls.

### Synchronous use

`prefetch_for_each<Lookahead>(keys, prefetch, consume)` completes the same loop
synchronously for a `span<const Key>`. It instantiates a one-shot cursor whose
grain covers the whole input; that local cursor and its completion state can
optimize away. It requires no Worker, Ring admission or physical allocation.
Direct and scheduled entries can share one typed loop and complete consume body.
Choose the entry at the ordinary operation boundary from the complete procedures'
measured costs; the cursor performs no heat prediction.

### Lifetime and callback rules

Keep cursor, keys, reader, sink and every accessed byte valid through active
work and overflow until their last native use has returned to the owner. They
must also survive a removing `discard` or `release_if` call until it returns.
Finish before rebinding or destroying them. The
[common lifetime rules](usage.md#binding-and-lifetime) distinguish a complete
synchronous borrow from an escaping physical use.
Cleanup does not undo results already published by completed quanta. The owner
settles partial results and terminal outcomes, then closes its Ring before Pool
shutdown.

Bodies must not throw, suspend, recursively service/reenter the stream or owning
Ring, mutate the cursor, or change its key input. They must not retain references
to the helper's temporary locals. Cursor observations describe returned
boundaries: it publishes the completed index after the whole quantum, so there
is no partial-quantum recovery protocol. Allocation or producer/port waits belong
outside this body, with ordinary admission or the enclosing Task protocol.

## Request order and work bounds

`Quantum` must be positive and is explicit. Each nonempty call consumes
`min(Quantum, remaining)` requests in input order. Empty input is valid;
advancing an empty/completed stream returns true without invoking either body.
Completion counts consumed input indices, independently of what the bodies find
or emit. A lookup body must still complete its work for misses.

The first nonempty call invokes prefetch for the first `min(Lookahead, N)`
indices. For each consumed index `i`, it then invokes prefetch for `i+Lookahead`
when that index exists, before invoking consume for `i`. Across a complete
drain, each input index invokes each body exactly once. Duplicate-valued keys
remain distinct requests and may target the same address repeatedly. This
counts body invocations, not actual hardware hints, hits or readiness.

After each returned quantum, the prefetched frontier is mathematically
`min(N, processed+Lookahead)`. It remains valid when Quantum is smaller than
Lookahead; reentry does not prime again or require a separate frontier field.
Bounds subtract before adding, avoiding size overflow. Lookahead zero is legal
and invokes prefetch immediately before consuming each key, with no lead time.
Lookahead counts requests, not cache lines, pages or hardware trackers. Its
default of 32 is an available loop shape, not a calibrated hardware policy.

Bootstrap and consumption happen in the same first visit. If the whole input
fits one quantum, that visit can finish before another work kind runs; several
short streams do not automatically give one another prefetch lead. Earlier
caller-issued hints or admission dependencies can supply lead for known
addresses, but the cursor does not prime a scattered batch at construction or
insert a hint-only rotation. The mixed example demonstrates later-quantum
coexistence, rather than underfilled startup latency hiding.

The helper calculates each visit's hinted prefix and consume-only tail once at
its boundary. The steady hinted loop needs no per-key end-of-lookahead test.

One Ring visit includes at most Quantum complete consume bodies and Quantum
future prefetch bodies; its first visit can add up to Lookahead bootstrap
prefetch bodies. Ring capacity counts frames, and `spin({visits})` counts native
callbacks. Neither bounds the elapsed time of these bodies or the number of
memory stages inside a complete consume body. Larger quanta amortize dispatch
but delay other kinds and owner service; qualify grain against both throughput
and those service gaps. There is no nested drain in the helper.

## Behavior checks

[`loom_stream_check`](../test/stream.cpp) checks exact hint/consume indices and
outputs against independent sequence laws, including duplicate values, all
misses, ragged/empty spans, Quantum smaller than Lookahead, maximal template
bounds and repeated completion. It also checks actual heterogeneous Ring
rotation against an independent queue model, with finite admission, overflow,
cancellation, partial readmission and discard. `loom_example_stream` exercises
the maintained mixed-operation example. Both run under `loom_validate`.
