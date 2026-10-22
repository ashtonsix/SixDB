# Using Boundlist (historical compatibility)

The maintained module is now [Intervals](../intervals/usage.md). StreamPack owns
compact geometry and retirement. This page records the old Boundlist surface
retained for historical controls; it is not a new consumer contract.

Boundlist partitions an ordered domain into contiguous half-open intervals.
A rank identifies one interval. The same geometry operations work with existing
cuts, a decoder projection or Boundlist's compact mutable encoding.

Link `ikea::boundlist` and include `<ikea/boundlist.h>`. The
[reference](reference.md) specifies the contracts and bytes.

## Bind cuts, then use interval ranks

Pass the whole cut array to `from_cuts`; it borrows the backing and normalizes
`n+1` cuts to `n` interval ranks. Keep the cuts ordered and coherent throughout
an operation. The adapter copies the origin and count while borrowing the ends;
renew it after changing the origin, count or backing placement.

```cpp
#include <ikea/boundlist.h>
#include <array>
#include <cassert>

namespace bl = ikea::boundlist;
const std::array cuts{0u, 8u, 18u, 26u, 35u, 46u, 56u};
const auto geometry = bl::from_cuts(std::span(cuts));
assert(geometry.size() == 6);
const auto bounds = bl::locate(geometry, 2);
assert(bounds && bounds->first == 18 && bounds->limit == 26);
assert(bl::reverse(geometry, 18u) == 2);
assert(bl::reverse(geometry, 56u) == geometry.size()); // final cut excluded
const auto window = bl::overlap(geometry, 9u, 36u);
assert((window == bl::rank_window{1, 5}));
```

`reverse` finds the containing interval; absence is the interval count.
`overlap` returns a half-open **rank** window, so `[1,5)` means ranks 1 through 4.
It accepts queries extending beyond either end of the geometry. Equal cuts define
zero-size intervals: `locate` can return one, but it owns no points and cannot
intersect a coordinate range.

If storage already contains one exclusive end per interval, use
`from_ends(endpoint_source,origin)`. A custom geometry needs only `size()` as
`unsigned`, `origin()` and ordered `at(rank)` returning an exclusive end.
These sources can use nonzero origins, wider keys and more than 256 intervals.
No compact schema or retirement metadata is required.

## Attach selection only when needed

Raw geometry includes every assigned rank. Attach a bitset to select ranks without
changing their boundaries:

```cpp
const std::array<std::byte, 1> bits{std::byte{0x3b}}; // eligible: 0, 1, 3, 4, 5
const bl::selected_source selected{geometry, bl::rank_selection{bits}};
assert(bl::reverse(selected, 20u) == selected.size()); // rank 2 excluded
assert(bl::reverse(selected, 26u) == 3);
assert((bl::overlap(selected, 9u, 36u) == window));
assert(bl::range(selected, window.first, window.limit - window.first,
    [&](unsigned rank, auto interval) {
        consume_interval(rank, interval.first, interval.limit);
    }));
```

The callback visits ranks 1, 3 and 4. The excluded rank still spans `[18,26)`;
its coordinates are not reassigned. Selected overlap trims its window's edges,
but internal excluded or zero-size ranks can remain. Use the same selector when
traversing that window. `range` validates the complete rank interval before any
callback; eligible zero-size ranks are visited too.

A nonempty `rank_selection` supplies `ceil(n/8)` readable bytes, with set bits
meaning eligible; `inverted=true` accepts a tombstone plane. Empty selection
selects none; `rank_selection::all()` selects all. Retain the bitset along with
the geometry it describes.

## Reuse existing encodings

An endpoint source needs only `size()` and ordered `at(index)`. Project those from
an already-bound SeriesPack/TuplePack decoder, then add the origin with `from_ends`
when interval operations are needed. Keep decoder attachment and schema preparation
outside the query loop. The [composition example](../../examples/boundlist/composition.cpp)
shows a packed endpoint projection and prepared checkpoint search.

Endpoint operations remain available independently: `lower_bound(source,q)` finds
the first eligible end **>= q**; `upper_bound` finds the first **> q**. Both return
`size()` when absent. For the geometry above, lower bound of 18 is 1 and upper
bound is 2. An explicitly selected endpoint search may return a later rank after
skipping the owner; use `reverse` for ownership, where exclusion returns no match.

Native unsigned arrays permit unaligned reads within their granted bytes. Signed
or wider integer queries are checked before conversion to a narrower stored domain.
Other ordered domains require no arithmetic or universal integer representation.

## Store compact subdivision geometry

Use the compact format for up to 256 contiguous intervals starting
at zero. An interval's size is the distance between its cuts; its exclusive end is the
sum of sizes through that rank.
For sizes `[5,0,8]`, rank 0 owns `[0,5)`, rank 1 owns no positions and rank 2 owns
`[5,13)`. Its explicit cuts are `[0,5,5,13]`; zero is implicit in the stored format.

Assigned ranks remain stable. A tombstone marks retirement without changing a slot's
geometry; it does not remove the rank or move later slots.

Choose schema facts first. `fixed_size` derives geometry without stored ends or sizes.
A zero-size domain also needs no boundaries. `immortal` removes tombstones and makes
retirement unavailable. Otherwise choose the codec by the useful work:

| Encoding | Use when |
| --- | --- |
| **Absolute Ends** | Direct points and endpoint comparisons matter, especially when the whole extent already fits a narrow field. |
| **Sizes** — default | Individual sizes are narrow, size-only work matters, or sizes change frequently. Starts need a prefix sum within a 16-rank group. |
| **Relative Ends** | Group-local ends fit the same field width as sizes while global ends are wider, and point/reverse reads matter. |

Absolute Ends reserves its whole endpoint directory up front. Checkpointed formats grow
their local-field groups with assigned ranks. Relative Ends can need wider fields at a
size boundary: a per-slot maximum of 4095 permits u16 local ends, while 4096 can require
u32 unless the total extent limit proves a tighter bound. Keep the Sizes default unless
complete-consumer measurements justify another choice. The [encoding tour](reference.md#three-geometry-encodings)
shows the arithmetic and field-width rules.

### Prepare once, then bind and initialize

`maximum` bounds an individual slot size; `extent_limit` independently bounds their
total. They determine immutable schema field widths, not how much memory must be
allocated now. The compact view uses three byte windows:

| Window | Purpose |
| --- | --- |
| Directory | Tombstones, then absolute ends or group checkpoints. |
| Lengths | Individual sizes or group-local ends; absent for Absolute Ends. |
| Control | Assigned count and, where needed, extent. |

The layout reports `directory_bytes()`, `lengths_bytes(assigned)`, `control_bytes()`
and `lengths_alignment()` for admission. Windows can share an owner allocation, but
the supplied spans must be disjoint. Schema omissions remove the corresponding fields.

Mutation calls take an effects sink. The journal below records source-qualified byte
writes for the owner's publication policy; it does not commit a transaction or protect
readers. Supply its capacity before execution. Keep the immutable layout and named view
at stable addresses while accesses or effect records borrow them.

In this Sizes example, directory capacity is 64 bytes, full lengths capacity is
256 bytes, and control is 6 bytes. An owner can instead grow lengths by initialized
16-rank groups under the same schema.

```cpp
#include <ikea/effects.h>
#include <cassert>

const auto format = bl::layout::prepare({
    .family = bl::geometry::sizes, .maximum = 255, .extent_limit = 4096});
assert(format);
std::array<std::byte, 64> directory{};
std::array<std::byte, 256> lengths{};
std::array<std::byte, 6> control{};
auto source = bl::view::bind(*format, {directory, lengths, control});
assert(source);
std::array<ikea::owner_write, 32> records;
ikea::source_write_journal effects{records};
assert(bl::initialize(*source, effects));
for (unsigned size : std::array{8u, 10u, 8u, 9u, 11u, 10u})
    assert(bl::append(*source, size, effects)); // the six intervals above
```

`bind` admits placement without inspecting contents. `initialize` constructs new empty
content; use `source->validate()` for external stored contents. Do not repeatedly
validate an unchanged admitted block in a hot loop. The layout and named view stay
at stable addresses while accesses or effects borrow them. Allocation growth does not
change the schema's field widths.

### Read the compact view through the same geometry operations

The view is already a geometry source. Its retirement bitmap is an optional
companion exposed by `live_ranks()`:

```cpp
const auto location = bl::locate(*source, 2);
assert(location && location->first == 18 && location->limit == 26);
assert(bl::reverse(*source, 20u) == 2);
assert(bl::retire(*source, 2, effects));
assert(bl::reverse(*source, 20u) == 2); // raw geometry is unchanged
const bl::selected_source live{*source, source->live_ranks()};
assert(bl::reverse(live, 20u) == source->size());
assert(!bl::locate(live, 2));
assert((bl::overlap(live, 9u, 36u) == bl::rank_window{1, 5}));
```

The [ordinary example](../../examples/boundlist/ordinary.cpp) includes these calls
and source-qualified effects. It needs only geometry storage.

### Choose the geometry edit before moving data

`set_boundary` changes one exclusive end while preserving later ends. `resize` changes
one size and translates all subsequent ends. Starting from ends `[5,9,12]`, moving the
first boundary to 6 yields `[6,9,12]`; increasing the first size to 6 yields
`[6,10,13]`.

Both operations change only geometry metadata. The owner performs any corresponding movement
and publishes the geometry and data together. Checked refusal preserves bytes and
effects. Inserting/removing middle ranks or sorting builds a replacement with explicit
rank repair. Value admission, movement and packing belong to the consuming structure.

## Keep ready geometry work with its consumer

For a sequential run, use `range` to carry each start into the next interval,
rather than calculating a prefix for every rank. Resolve the schema once around
the complete operation when combining several reads:

```cpp
bl::author::dispatch(*source, [&](auto access) {
    const bl::selected_source live{access, access.live_ranks()};
    assert(bl::range(live, first, count, [&](unsigned rank, auto bounds) {
        consume_interval(rank, bounds.first, bounds.limit);
    }));
});
```

The ordinary `range` preflights rank bounds. Typed kernels require admitted
contents, ranks and coherent backing. Read callbacks cannot mutate or rebind the
source. If only a start is needed, typed `start(rank)` avoids reading size too.

For native consumers, `native::read16(access,group)` returns offset/size vectors
and an `active` mask for assigned ranks. Pass a `selected_source` wrapping that
access to intersect the mask with selection. Keep vectors with the fused sink or
scoped chain, avoiding obligatory stack arrays. Initialized partial-group padding
is readable but never active.

`author::encoder` carries the new position and local end during an admitted
sequential rewrite. The consumer chooses ordering, count, retirement and any data
movement. An in-place rewrite captures original boundaries before overwriting
them; the shared original-geometry walker supports that coordinated operation.

## Choose search execution for the workload

Ordinary calls are the starting point. The default combines conditional-select
narrowing with explicit SIMD convergence. Numeric bindings can retain prediction state
when their checks find it useful, with exact repair for skew and duplicates. These are
internal choices, not a caller-selected lerp/shotgun/bisection enum.

| Consumer | Starting choice | What to measure |
| --- | --- | --- |
| A few point/range queries | Ordinary lower/upper/overlap calls | Complete interval use, not only the returned rank. |
| Small hot native endpoint arrays | Compare default against `author::scan_source<T>` | Dependent latency and independent throughput; scans can lose as lists grow or queries qualify early. |
| Repeated queries over a large immutable numeric source | Cache `author::bind_search(direct_source)` | Binding cost and reuse count, including skew/duplicates. |
| Several independent native queries | `author::lower_bounds` / `upper_bounds` over arrays | Full query preparation and use of selected intervals; these interleave unresolved probes. |
| Checkpoints available, local group cold | Prepared local phase, optionally scheduled through Loom | Frame/refill/drain cost and actual unresolved reads. |
| Consecutive ranks | Carried range walk or native `read16` | Avoid reconstructing each local prefix separately. |

For repeated queries on one coherent version, source-level preparation can be reused:

```cpp
#include <ikea/boundlist/author/search.h>
const auto searched = bl::author::bind_search(ends);
const auto found = bl::lower_bound(searched, 8u);
const auto found_ranks = bl::author::lower_bounds(
    std::array{searched, searched}, std::array{8u, 35u});
```

Rebuild derived bindings after endpoint, count or placement changes. An unchanged
pointer does not keep a prediction model valid. Bind small/one-shot sources directly
when setup cannot be amortized. Measure the actual source shape; a short SIMD scan can
beat even a good interpolation estimate.

### Stage only a useful dependency

A prepared phase splits a search into ready routing work and a `finish()` that returns
the ordinary result. For checkpointed deltas or relative ends, routing can identify one
local 16-field group. The owner may hint that read and yield through Loom if it is cold.
A phase does not enqueue itself; use actual Ring comparisons before waiting on a hot
group.

This example borrows two groups of narrow sizes at a nonzero 64-bit origin.
Each group has 16 initialized, readable fields, and its checkpoint is the group's start:

```cpp
#include <cstdint>

std::array<std::uint8_t, 32> sizes;
sizes.fill(7);
constexpr std::uint64_t origin = 1ull << 40;
const std::array<std::uint64_t, 2> checkpoints{origin, origin + 16 * 7};
const bl::delta_source<std::uint8_t, std::uint64_t> bounds{
    sizes.data(), checkpoints.data(), 32, 16, origin + 32 * 7};
const auto pending = bl::author::prepare_lower_bound(bounds, origin + 130);
if (pending.has_local_read()) {
    hint(pending.local_window());
    // Caller may yield through Loom, retaining the coherent version.
}
const auto completed_rank = pending.finish(); // rank 18
```

The window is a hint, not the complete read/protection footprint. Explicit selection
may read eligibility, and a numeric prediction may repair outside its proposed bracket.
Retain boundaries, count, eligibility, backing and derived bindings as one coherent
version through completion. Selection-aware completion borrows the original named
source. The [composition example](../../examples/boundlist/composition.cpp) shows the
backing, packed adapter, reusable bindings and independent queries.

Writes separately report source-qualified issued bytes, including preserving vector
stores. Reserve effect capacity before execution; hooks cannot fail or suspend.
Effects do not protect retained readers. Follow the [owner integration
guide](../integration.md) for allocation, visibility and retention. Old readers
need old coherent geometry and data; copying an allocation handle does not retain them.
