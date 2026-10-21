# Using Boundlist

Boundlist maintains contiguous intervals and searches their cuts. A rank is the
zero-based index of one interval. Consecutive cuts define `[start,end)`: the end of one
interval is the start of the next. Boundlist handles geometry and optional retirement;
the consumer owns anything associated with those intervals.

Link `ikea::boundlist` and include `<ikea/boundlist.h>`. The
[reference](reference.md) explains the model, stored formats and exact contracts.

## Search existing cuts

For cuts `[0,8,18,26,35,46,56]`, there are six intervals:
`[0,8)`, `[8,18)`, `[18,26)`, `[26,35)`, `[35,46)` and `[46,56)`.
They meet at shared cuts. Represent them as an implicit origin of zero plus one
exclusive end per interval; an end's index then equals its interval rank.

```cpp
#include <ikea/boundlist.h>
#include <array>
#include <cassert>

namespace bl = ikea::boundlist;
const std::array<unsigned, 6> end_values{8, 18, 26, 35, 46, 56};
const bl::direct_source ends{end_values.data(), unsigned(end_values.size())};
const auto rank = bl::upper_bound(ends, 18u);
assert(rank == 2); // interval [18,26)
assert(bl::upper_bound(ends, 56u) == ends.size()); // outside the extent
```

`upper_bound` returns the **first** end strictly greater than the point. That is the
containing interval for points inside this zero-origin extent. Check points against
any nonzero origin separately. `direct_source` borrows the array without copying or
sorting it; keep it ordered, readable and unchanged throughout the operation.

`lower_bound` instead returns the first end **greater than or equal to** the query:
`lower_bound(ends,18u)` is 1, whereas `upper_bound(ends,18u)` is 2. The former finds
the cut at 18; the latter finds the interval starting there. Both return `size()`
when no endpoint qualifies, and neither searches backwards for a last match.

This example omits the first cut from storage. If your array includes **all** cuts,
its entries index cuts rather than intervals. With cuts `[10,15,16]`, first reject
points outside `[10,16)`, then subtract one from `upper_bound(cuts,point)` to get
the interval rank. Do not mix the full-cut and end-only index conventions.

## Find the intervals overlapping a range

The compact mutable format supplies `sidecar::overlap` directly; the next section
sets one up. For an existing end-only source, the same window can be calculated
using two boundary searches. This example assumes `0 <= low < high <= extent` and
no retirement selection:

```cpp
const auto first = bl::upper_bound(ends, 9u);       // rank 1: end > low
const auto last = bl::lower_bound(ends, 36u);      // rank 4: end >= high
const bl::rank_window window{first, last + 1};     // ranks [1,5)
assert((window == bl::rank_window{1, 5}));
```

A `rank_window` includes `first` and excludes `limit`. This query overlaps ranks
1, 2, 3 and 4. The [diagram](reference.md#points-ranges-and-eligibility) shows their
contiguous geometry and the effect of retiring an internal rank.

Retirement does not change cuts. A point in a retired interval still belongs to it;
a lookup for the **next live** rank answers a different question. To filter end-search
results, attach a `population` through `populated_source`:

```cpp
const std::array<std::byte, 1> bits{std::byte{0x3b}}; // live: 0, 1, 3, 4, 5
const bl::population live{bits};
const bl::populated_source live_ends{ends, live};
assert(bl::upper_bound(live_ends, 20u) == 3); // next live end; NOT owner of point 20
for (unsigned r = live.next(window.first, window.limit); r < window.limit;
     r = live.next(r + 1, window.limit))
    consume_interval(r);
```

The loop visits ranks 1, 3 and 4. Rank 2 still spans `[18,26)`; it is skipped, not
removed or absorbed into rank 3. `population` supplies `ceil(n/8)` readable bytes,
with set bits meaning eligible unless `inverted=true`. It changes which ranks may
be returned, not endpoint ordering or readability requirements.

## Reuse an existing packed source

An endpoint source only needs `size()` and ordered `at(index)`. A consumer can project
those from an already-bound SeriesPack/TuplePack decoder instead of building a temporary
endpoint array. Keep decoding and schema preparation outside the probe loop; the
[composition example](../../examples/boundlist/composition.cpp) shows such an adapter.

Native unsigned arrays permit unaligned loads within their declared readable bytes.
Wider or signed integer queries are classified before narrowing. Other comparison
domains use their own ordering without conversion to a universal integer. The source
controls the represented endpoints; the search still returns an index into that source.

## Store compact subdivision geometry

Use the compact format (`sidecar` in the API) for up to 256 contiguous intervals starting
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
allocated now. The sidecar uses three byte windows:

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

namespace sc = bl::sidecar;
const auto format = sc::layout::prepare({
    .family = sc::geometry::sizes, .maximum = 255, .extent_limit = 4096});
assert(format);
std::array<std::byte, 64> directory{};
std::array<std::byte, 256> lengths{};
std::array<std::byte, 6> control{};
auto source = sc::view::bind(*format, {directory, lengths, control});
assert(source);
std::array<ikea::owner_write, 32> records;
ikea::source_write_journal effects{records};
assert(sc::initialize(*source, effects));
for (unsigned size : std::array{8u, 10u, 8u, 9u, 11u, 10u})
    assert(sc::append(*source, size, effects)); // the six intervals above
```

`bind` admits placement without inspecting contents. `initialize` constructs new empty
content; use `source->validate()` for external stored contents. Do not repeatedly
validate an unchanged admitted block in a hot loop. The layout and named view stay
at stable addresses while accesses or effects borrow them. Allocation growth does not
change the schema's field widths.

### Locate an interval or find the rank containing a point

`locate` checks rank and liveness, then returns the slot's offset and size.
`reverse` maps a position below extent to `{rank,tombstone}`, including retired
intervals. It finds the first exclusive end **greater than** the position;
zero-size intervals never own positions.

```cpp
const auto location = sc::locate(*source, 2);
assert(location && location->offset == 18 && location->size == 8);
assert(sc::reverse(*source, 18)->rank == 2);
assert(!sc::reverse(*source, 56)); // extent itself is excluded
assert(sc::retire(*source, 2, effects));
const auto owner = sc::reverse(*source, 20);
assert(owner && owner->rank == 2 && owner->tombstone);
assert((sc::overlap(*source, 9, 36, true) == bl::rank_window{1, 5}));
```

Retiring leaves geometry unchanged; it does not move later intervals.
`sc::overlap(*source,low,high,only_live)` returns a rank window for intervals
overlapping `[low,high)`. Retired intervals participate unless `only_live=true`.
Internal zero-size slots can remain; with `only_live=true`, retired holes can remain
inside the window too. The [ordinary example](../../examples/boundlist/ordinary.cpp)
includes these reads and source-qualified
effects; it needs no payload allocation.

### Choose the geometry edit before moving data

`set_boundary` changes one exclusive end while preserving later ends. `resize` changes
one size and translates all subsequent ends. Starting from ends `[5,9,12]`, moving the
first boundary to 6 yields `[6,9,12]`; increasing the first size to 6 yields
`[6,10,13]`.

Both operations change only geometry metadata. The owner performs any corresponding movement
and publishes the geometry and data together. Checked refusal preserves bytes and
effects. Inserting/removing middle ranks or sorting builds a replacement with explicit
rank repair. Value admission, movement and packing belong to the consuming structure.

## Keep hot geometry work with its consumer

For a sequential run, use a carried range rather than rebuilding every start. Resolve
the schema once around the complete operation:

```cpp
sc::author::dispatch(*source, [&](auto a) {
    // Caller has admitted this rank interval and stable contents.
    a.range(first, count, [&](unsigned rank, sc::location p) {
        consume_interval(rank, p.offset, p.size);
    });
});
```

The typed access is for admitted contents and ranks; ordinary `sc::range` checks the
interval first. Callbacks visit live ranks and cannot mutate or rebind the source.
For point consumers needing only a start, use `start` rather than reading size too.
A consumer with its own value-length field can pair that field with the start directly.

For native consumers, `sidecar::native::read16(a,group)` returns offsets, sizes and a
live mask. Keep those vectors with the fused sink or scoped chain rather than
materializing a stack array. The partial final group has readable padding and masked
inactive lanes; they are not extra ranks.

`author::encoder` is temporary computation for a pre-admitted sequential rewrite.
It carries the new position and local end, and emits sizes/checkpoints. It chooses no
sorting, movement, count or retirement policy. An in-place consumer must capture
original boundaries before overwriting them; the shared original-geometry walker does
this. The consuming structure coordinates that geometry rewrite with data movement.

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

The window is a hint, not the complete read/protection footprint. Population selection
may read eligibility, and a numeric prediction may repair outside its proposed bracket.
Retain boundaries, count, eligibility, backing and derived bindings as one coherent
version through completion. Population-aware completion borrows the original named
source. The [composition example](../../examples/boundlist/composition.cpp) shows the
backing, packed adapter, reusable bindings and independent queries.

Writes separately report source-qualified issued bytes, including preserving vector
stores. Reserve effect capacity before execution; hooks cannot fail or suspend.
Effects do not protect retained readers. Follow the [owner integration
guide](../integration.md) for allocation, visibility and retention. Old readers
need old coherent geometry and data; copying an allocation handle does not retain them.
