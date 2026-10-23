# Using StreamPack

StreamPack stores byte values in up to 256 stable, zero-based **ranks**. Each rank has
a reservation of **R** bytes and a value occupying its first **U** bytes, with
`0 <= U <= R <= 65535`. The rest is slack. Reservations concatenate from offset zero;
their total size is the block's **extent**. A live empty value has a rank even when R
is zero. A tombstone (**D**) marks retirement without moving later reservations.

Link `ikea::streampack` and include `<ikea/streampack.h>`. The
[reference](reference.md) specifies storage and exact command contracts.

## Understand the composition boundary

[Intervals](../intervals/usage.md) supplies borrowed ordered search and geometry
kernels. StreamPack owns its compact reservation image, readable tails, tombstones,
retirement, used lengths, fused byte-value operations and coordinated writes. The
collection supplies allocation, retained versions, logical holders and publication.

Use one StreamPack layout and one named view for a block. They compose geometry
and values once; no second geometry attachment or per-rank wrapper is needed.
Geometry and U can share a lengths window. Call value operations through StreamPack
so payload, U, retirement and geometry remain consistent.

Intervals applies only explicit operation eligibility. StreamPack's value API
applies its own retirement policy: `length` and `value` reject retired ranks, while
`reverse` returns `{rank,tombstone}` for any physical reservation. Intervals instead
returns a half-open interval or a rank/count sentinel. If U belongs in a consumer's
own record/tuple plane, compose that plane with Intervals and the payload there.
Stock StreamPack stores U or derives `U=R`; it has no external U provider.

## Construct a small block

Prepare the schema once and share its immutable layout. `maximum` limits one reservation;
`byte_limit` independently limits total extent. Here the defaults select Sizes geometry
and grouped reservation/used fields. The schema allows 255-byte reservations and a
4096-byte total.

The owner supplies four disjoint byte windows:

| Window | Purpose in this example | Capacity |
| --- | --- | --- |
| Payload | Reservations and their value bytes | 4096 B |
| Directory | 32 B tombstones, then sixteen u16 checkpoints | 64 B |
| Lengths | Sixteen u8 R, then sixteen u8 U per initialized group | 512 B at full capacity |
| Control | Assigned count and extent | 6 B |

Use the layout's extent/alignment methods for general owner code. Lengths can grow by
16-rank groups; the example provides full metadata capacity to keep setup short.
The windows may share an allocation, but their supplied spans must not overlap.

`view::attach` checks placement without inspecting contents. New storage needs
`initialize`; external content needs `validate`. `attach_unchecked` reuses only
placement checks and never validates contents. Checked mutation calls take `ikea::journal&`:
the journal below records source-qualified byte writes for the owner to resolve.
It does not commit a transaction or protect retained readers. Keep the layout and named
view stable while accesses or effects borrow them.

```cpp
#include <ikea/streampack.h>
#include <ikea/effects.h>
#include <array>
#include <cassert>

namespace sp = ikea::streampack;
const auto format = sp::layout::make({.maximum = 255, .byte_limit = 4096});
assert(format);
std::array<std::byte, 4096> payload;
std::array<std::byte, 64> directory;
std::array<std::byte, 512> lengths; // u8 fields need no extra alignment
std::array<std::byte, 6> control;
auto source = sp::view::attach(*format, {payload, directory, lengths, control});
assert(source);
auto& block = *source;
std::array<ikea::journal::entry, 64> writes;
ikea::journal effects{writes};
assert(sp::initialize(block, effects));
```

Binding does not require all payload capacity to be touched. Append initializes only
the new reservation and newly used metadata groups, including their readable padding.
Do not repeatedly validate unchanged, already-admitted contents in the hot path.

Append assigns the next rank. Pass the value bytes and the desired reservation:

```cpp
const std::array greeting{std::byte{'h'}, std::byte{'i'}};
const std::array cat{std::byte{'c'}, std::byte{'a'}, std::byte{'t'}};
assert(sp::append(block, greeting, 4, effects)); // rank 0: U=2, R=4
assert(sp::append(block, {}, 0, effects));       // rank 1: live empty value
assert(sp::append(block, cat, 5, effects));      // rank 2: U=3, R=5
assert(block.size() == 3 && block.extent() == 9);
```

The starts are 0, 4 and 4. Append zeroes new slack; it stores no header in the payload.
Rank capacity and byte capacity are separate: the block can exhaust either first.

## Read values or find byte owners

`value` and `length` check rank and liveness. Returned value spans contain U bytes,
so they exclude slack. `reverse` instead answers physical ownership: offsets in slack
or retired reservations still have an owner. Zero-size reservations own no bytes.

```cpp
const auto first_value = sp::value(block, 0);
assert(first_value && first_value->size() == 2);
assert(sp::value(block, 1)->empty());
assert(*sp::length(block, 2) == 3);
const auto owner = sp::reverse(block, 8);
assert(owner && owner->rank == 2 && !owner->tombstone);
assert(!sp::reverse(block, 9)); // extent itself is excluded
```

`overlap(block,low,high,only_live)` bounds physical reservations overlapping the byte
interval `[low,high)`. It returns `{first,limit}`, including `first` and excluding
`limit`, or `{n,n}` when empty, where `n=block.size()`. Retired storage participates by
default. Internal zero-size slots, and retired holes in the live-only form, can remain
inside that window.
This is reservation lookup; filtering strings or other encoded values belongs to a
consumer.

### Read at the useful grain

Use a carried traversal for consecutive ranks. Use selection for caller-specified
order; it preserves duplicates and skips retired ranks. Both validate all requested
coordinates before calling the sink.

```cpp
#include <cstdint>

assert(sp::range(block, 0, block.size(), [&](unsigned rank, sp::bytes value) {
    consume(rank, value);
}));
const std::array<std::uint16_t, 4> requested{2, 0, 2, 1};
assert(sp::select(block, requested, [&](unsigned rank, sp::bytes value) {
    consume(rank, value);
}));
```

The range emits ranks 0, 1, 2, including the live empty value. Selection emits 2, 0, 2, 1.
Callbacks cannot mutate/rebind the source, and borrowed bytes must remain valid for
their use.

The scratch-backed `select` overload decodes all starts once for that call. It needs
`ceil(block.size()/16)*16` writable u32 entries, disjoint from storage, rank selection,
borrowed controls and callback state. In this three-slot example, 16 entries suffice:

```cpp
std::array<std::uint32_t, 16> starts;
assert(sp::select(block, requested, starts, [&](unsigned rank, sp::bytes value) {
    consume(rank, value);
}));
```

Paying for all starts can help many shuffled requests; direct selection avoids that
setup for sparse requests. There is no universal crossover count. Charge scratch
preparation to its operation. The call retains no decoded state; if an authored consumer
keeps starts longer, any geometry change invalidates them.

## Replace, retire and pack

`replace` keeps the reservation, start and rank. With U present, a replacement may
be shorter or longer within R; old slack is not automatically cleared. A logical edit
needing a different R appends elsewhere and lets the owner retarget its holder.
Retirement sets D and leaves geometry unchanged.

```cpp
const std::array replacement{std::byte{'b'}, std::byte{'y'}, std::byte{'e'}};
assert(sp::replace(block, 0, replacement, effects)); // U=3, still R=4
assert(block.extent() == 9);
assert(sp::retire(block, 0, effects));
assert(sp::reverse(block, 1)->tombstone);
```

`soft_pack` removes retired reservations while preserving every assigned rank and
tombstone. Its boolean controls whether live reservations also shrink to U:

```cpp
assert(sp::soft_pack(block, false, effects)); // keep live slack
assert(block.size() == 3 && block.extent() == 5);
assert(sp::reverse(block, 0)->rank == 2);
assert(sp::soft_pack(block, true, effects));  // discard live slack too
assert(block.size() == 3 && block.extent() == 3);
assert(sp::value(block, 1)->empty());         // live empty rank survived
assert(block.validate());
```

Decoded starts and physical descriptors must be refreshed after packing. Fixed nonzero
geometry refuses packing that would change its reservations. Packing does not itself
return pages to an allocator or wipe every old payload byte beyond the new extent.

### Build a replacement with selected transfer

`transfer` constructs a disjoint destination in the requested order. It preflights
every selection and destination bound before initialization. Unlike `select`, it refuses
a retired requested rank. Duplicates are retained.

Given an owner-supplied named `destination` view and its separate
`rewrite_effects` journal, this creates three new ranks from the current example:

```cpp
const std::array<std::uint16_t, 3> order{2, 1, 2};
assert(sp::transfer(block, order, destination, true, rewrite_effects));
assert(destination.size() == 3 && destination.extent() == 6);
```

The destination contains `cat`, empty, `cat`, with R reduced to U. Its schema may choose
another supported geometry encoding or omit U when all selected reservations are full.
The owner records the old-to-new rank mapping, repairs holders and publishes the
replacement. Sorting, deduplication and repartitioning are owner policy; transfer
supplies the byte/value mechanics.

## Choose schema facts and encoding for the workload

Choose omissions before field placement:

| Promise | Schema choice | Consequence |
| --- | --- | --- |
| Every reservation is exactly filled | `full=true` | No U storage; append/replace must have value length equal to R. |
| Every reservation has one fixed size | `fixed_reservation` | No per-rank geometry; U can still vary unless full. |
| Ranks never retire | `immortal=true` | No tombstones; retire is unavailable. |
| All reservations are zero-size | Zero maximum/fixed size | Count and optional tombstones remain; geometry/U fields disappear. |

The Sizes default stores narrow R and a checkpoint per 16 ranks. Absolute Ends stores
exclusive ends in the directory and favors a short point path. Relative Ends stores
local cumulative ends, avoiding a local prefix sum when finding starts. The
[geometry tour](reference.md#choose-reservation-geometry) shows the arithmetic;
the [StreamPack schema reference](reference.md#make-a-shared-layout) describes their value
composition.

Maximum reservation and total byte limit can change which fields need widening.
For example, common-short and long-tail dictionaries can use:

```cpp
sp::layout::options common{.maximum = 255, .byte_limit = 16384, .full = true};
sp::layout::options tail{.maximum = 65535, .byte_limit = 65535, .full = true};
```

Both omit U. Global coordinates fit u16; Sizes uses u8 R in the common dictionary and
u16 R in the tail. The tail admits a single 65,535-byte value without paying for u32
coordinates sized to the theoretical 256 × 65,535 extent. Allocation size alone cannot
establish that persistent schema bound.

Absolute Ends pays its whole directory up front. Sizes/Relative Ends grow their local
groups with occupancy. For the tail schema above, full Absolute Ends needs 546 metadata
bytes versus Sizes' 582 at 256 assigned ranks; Sizes needs only 102 at 1–16 ranks.
These are layout totals including directory/lengths/control, excluding allocator gaps,
not measured timings.

For checkpointed geometry with U, grouped placement keeps each 16-rank geometry/U pair
together for complete value work. `placement::separated` is geometry-first planar storage
for demonstrated U-only workloads; it reserves the full geometry prefix even when few
ranks are assigned. There is no second plane-order choice. Placement is normalized away
when no pair exists. Alignment follows actual field widths, so a 48 B or 96 B group
does not require that alignment. A wider Relative Ends field does not widen U with it.

## Keep complete hot consumers together

For repeated point work, resolve schema dispatch once around the complete consumer.
Use checked `reader.get` when ranks/liveness vary; admitted callers can use
`reader.value` or `reader.locate` directly. Here `ranks` is a sparse request list:

```cpp
sp::author::visit(block, [&](auto reader) {
    for (unsigned rank : ranks) {
        const auto value = reader.get(rank);
        if (value) consume(rank, *value);
    }
});
```

The access borrows the named `block` view. Keep that view, its layout and storage
stable while any copied access remains in use. `author::visit` rejects temporary
views; name an `as_const()` result first when composing a const consumer. One-shot
free reads such as `sp::value(block.as_const(), rank)` remain valid.

For a sequential run, keep using `range`: it carries geometry rather than recomputing
a prefix per rank. For many blocks sharing one exact immutable layout object,
`reader.rebind(other_named_view)` reuses preparation with different windows. The owner
proves the new block's content admission and exact shared layout identity. Recreating
preparation at every lookup loses that benefit.

`<ikea/streampack/author/native.h>` exposes `native::read16`: sixteen offsets and U
lengths in registers plus an original-rank live mask. Keep decoding and its
predicate/reduction in one typed body so the packet need not spill across an aggregate
return. It reuses Intervals kernels inside its owned compact geometry and value-length projection.
The [composition example](../../examples/streampack/composition.cpp) shows scalar and
native carriers in Ikea chains.

Use CPS around a useful unresolved dependency and keep ready arithmetic inline. Ikea
chains do not suspend into Loom. The owner chooses Ring versus direct execution from
complete-consumer measurements; extra stages do not create independence in one serial
lookup. `visit_value_metadata` reports point-value metadata dependencies for that policy.
It does not issue prefetches, grant readable backing or describe every search footprint.
[Building fast consumers](../consumers.md) covers these choices across Ikea.

## Grow storage and retain coherent readers

Grow payload and metadata under one fixed schema. The owner can seal and start another
block when either ranks or the byte limit run out. Request backing for the initialized
extent needed now; reserve address space separately. In a shared allocation, metadata
before payload leaves a tail that can later be reclaimed. Independent windows also work.

On sealing, use `block.extent()`, including retired reservations and slack, to retain
the whole interpreted payload. Include all metadata and readable padding too. A sum of
live U lengths is not an equivalent retention bound.

Growth can preserve addresses or relocate. End borrowed access and resolve aliased
inputs before relocation, then rebind the changed windows. Old readers need the old
payload and geometry/U/tombstone/control interpretation together. A copied allocation
handle or a `shared_ptr` to the writer does not establish retained backing or permission
to change visible bytes.

For Orbital, native capture consumes its Allocation. A still-private append writer can
share immutable payload prefixes using actual range Holds and separately versioned
metadata; capture/holder costs count. To release unused private suffix access, use
`allocation::relinquish_tail`, then request backing maintenance with `trim_cold` and
rebind to the contracted windows. Retained readers still need their actual backing.
The [allocation contract](../../../orbital/README.md#allocation) owns growth conflicts,
settlement and accounting. Neither exhausting ranks nor packing automatically frees
physical pages.

Successful mutation reports issued byte spans, including preserved metadata stores and
count/extent updates. Use `effect_capacity_*` to budget additional journal records before execution;
unchecked author hooks cannot fail or
suspend. Checked refusal preserves bytes and effects. The owner resolves source identities,
protects readers and publishes logical holder/summary changes separately from physical
damage. [Owner integration](../integration.md) explains that publication boundary.

## Examples and evidence

The [ordinary example](../../examples/streampack/ordinary.cpp) covers construction,
empty values, retirement, packing and effects. Build `ikea_streampack_check`,
`ikea_streampack_boundaries_check`, `ikea_streampack_encodings_check` and the two
examples, or run `ikea_validate`. [Behavior checks](../../test/streampack/README.md)
describe the independent oracles and boundary coverage.

The [current portfolio](../../../workbench/spikes/boundlist/PORTFOLIO.md) records memory
and complete-consumer measurements across the geometry/value split. The
[original implementation study](../../../workbench/spikes/streampack/IMPLEMENTATION.md)
and [extraction results](../../../workbench/spikes/boundlist/GRADUATION.md) retain older
controls. Keep dependent latency, independent reciprocal throughput and memory separate;
those histories guide choices but do not establish a universal fastest encoding.
