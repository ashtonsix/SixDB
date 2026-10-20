# Using Boundlist

Boundlist finds ranks in ordered boundaries. A **rank** is a zero-based position:
it can identify a child in an index, or a slot in contiguous storage. A source with `n`
entries has ranks 0 through `n-1`. Start with existing endpoint arrays for query routing.
Use the curated sidecar when you want Boundlist to
encode and maintain subdivision geometry. StreamPack builds byte values on that
sidecar; it is not required for either use.

Link `ikea::boundlist` and include `<ikea/boundlist.h>`. The
[reference](reference.md) explains the model, stored formats and exact contracts.

## Route a point through existing boundaries

For children ordered by key, keep their exact occupied maxima in a nondecreasing array.
A lower-bound search returns the **first** maximum greater than or equal to the key.
With unique keys, that is the only possible child; it still checks whether the key
exists within its records. When no maximum qualifies, the result is `source.size()`,
which is not a valid rank.

`direct_source` borrows the array; it neither copies nor sorts it. Keep the array
readable and unchanged for the operation. Every rank is initially eligible, meaning that
a search may return it. This example searches six children:

```cpp
#include <ikea/boundlist.h>
#include <array>

namespace bl = ikea::boundlist;
const std::array<unsigned, 6> max_values{7, 10, 24, 31, 40, 52};
const bl::direct_source maxima{max_values.data(), unsigned(max_values.size())};
const auto rank = bl::lower_bound(maxima, 10u); // rank 1: maximum 10
if (rank != maxima.size()) consume_child(rank);
```

`upper_bound(maxima,10u)` instead returns rank 2: the **first** maximum strictly
greater than 10. Both calls search for a first match, never a last match. Equal endpoints
are supported. For all-eligible endpoints `[3,7,7,10]`, the equal-to-7 ranks form
`[lower_bound(7),upper_bound(7)) = [1,3)`, containing ranks 1 and 2.

## Find the children overlapping a range

For a range query, also supply each child's occupied minimum. Minima and maxima must
each be nondecreasing and refer to the same ranks. They are inclusive occupied extrema:
a child with minimum 10 and maximum 10 contains a key, rather than being empty.

The default query `[low,high)` includes `low` and excludes `high`. Its candidate
children have `maximum >= low` and `minimum < high`. `overlap` returns
`rank_window{first,limit}`, including `first` and excluding `limit`. An empty result is
`{n,n}`. Thus a returned `{1,5}` covers ranks 1, 2, 3 and 4:

```cpp
const std::array<unsigned, 6> min_values{3, 10, 18, 26, 35, 46};
const bl::direct_source minima{min_values.data(), unsigned(min_values.size())};
const auto window = bl::overlap(maxima, minima, bl::query_range{9u, 36u});
for (unsigned r = window.first; r < window.limit; ++r)
    scan_child(r, 9u, 36u);
```

Child extrema can enclose gaps, so the child still filters its actual records.
`query_range` also has `low_closed` and `high_closed` flags, defaulting to `true` and
`false`. For an inclusive query `[9,36]`, pass `query_range{9u,36u,true,true}`.
The returned rank window always excludes its limit, regardless of the query flags.
If duplicate keys can span children, use this range operation rather than assuming one
lower-bound result identifies every match.

### Exclude unpopulated or retired children

So far every rank was eligible. To exclude some, attach a `population` bitset through
`populated_source`. It has one bit per rank: set means eligible, unless `inverted` is
true for a tombstone-style mask. Supply `ceil(n/8)` readable bytes. Excluded endpoints
must still be ordered and readable because search can probe them.

Apply the same eligibility to the matching minima and maxima. This example excludes
rank 2 and uses `population::next` in the loop to skip internal holes:

```cpp
const std::array<std::byte, 1> bits{std::byte{0x3b}}; // eligible: 0, 1, 3, 4, 5
const bl::population live{bits};
const bl::populated_source live_maxima{maxima, live}, live_minima{minima, live};
const auto selected = bl::overlap(live_maxima, live_minima, bl::query_range{9u, 36u});
for (unsigned r = selected.first; r < selected.limit; r = live.next(r + 1, selected.limit))
    scan_child(r, 9u, 36u);
```

The result remains `{1,5}`: Boundlist trims ineligible edge ranks but leaves internal
holes. That loop visits ranks 1, 3 and 4. A bit is eligibility, not the number of records
in a child. The [range diagram](reference.md#points-ranges-and-eligibility) shows this
example, including the populated singleton at rank 1.

## Reuse an existing packed source

An endpoint source only needs `size()` and ordered `at(rank)`. A consumer can project
those from an already-bound SeriesPack/TuplePack decoder instead of building a temporary
endpoint array. Keep decoding and schema preparation outside the probe loop; the
[composition example](../../examples/boundlist/composition.cpp) shows such an adapter.

Native unsigned arrays permit unaligned loads within their declared readable bytes.
Wider or signed integer queries are classified before narrowing. Custom ordered domains
can use their own comparisons without an integer conversion. These source choices keep
the same first-match and rank-window semantics.

## Store compact subdivision geometry

Use the curated sidecar for up to 256 contiguous slots starting at zero. A slot's size
is its reserved extent; its exclusive end is the sum of sizes through that rank.
For sizes `[5,0,8]`, rank 0 owns `[0,5)`, rank 1 owns no positions and rank 2 owns
`[5,13)`. Those physical half-open intervals differ from the inclusive occupied
minimum/maximum pairs used for index routing.

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

In this small Sizes example, directory capacity is 64 bytes, full lengths capacity is
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
assert(sc::append(*source, 5, effects));
assert(sc::append(*source, 0, effects));
assert(sc::append(*source, 8, effects));
```

`bind` admits placement without inspecting contents. `initialize` constructs new empty
content; use `source->validate()` for external stored contents. Do not repeatedly
validate an unchanged admitted block in a hot loop. The layout and named view stay
at stable addresses while accesses or effects borrow them. Allocation growth does not
change the schema's field widths.

### Read a slot or find its physical owner

`locate` checks rank and liveness, then returns the slot's offset and size.
`reverse` maps a position below extent to `{rank,tombstone}`, including storage owned
by a retired slot. It finds the first exclusive end **greater than** the position;
zero-size slots never own positions.

```cpp
const auto location = sc::locate(*source, 2);
assert(location && location->offset == 5 && location->size == 8);
assert(sc::reverse(*source, 5)->rank == 2);
assert(!sc::reverse(*source, 13)); // extent itself is excluded
assert(sc::retire(*source, 0, effects));
const auto owner = sc::reverse(*source, 4);
assert(owner && owner->rank == 0 && owner->tombstone);
```

Retiring leaves geometry unchanged; it does not move later slots. Physical
`sc::overlap(*source,low,high,only_live)` returns a rank window for reservations
overlapping `[low,high)`. Retired storage participates unless `only_live=true`.
Internal zero-size slots can remain; with `only_live=true`, retired holes can remain
inside the window too. The [ordinary example](../../examples/boundlist/ordinary.cpp)
includes these reads and source-qualified
effects; it needs no payload allocation.

### Choose the geometry edit before moving data

`set_boundary` changes one exclusive end while preserving later ends. `resize` changes
one size and translates all subsequent ends. Starting from ends `[5,9,12]`, moving the
first boundary to 6 yields `[6,9,12]`; increasing the first size to 6 yields
`[6,10,13]`.

Neither operation moves data or children. The owner performs any corresponding movement
and publishes the geometry and data together. Checked refusal preserves bytes and
effects. Inserting/removing middle ranks or sorting builds a replacement with explicit
rank repair. Use [StreamPack](../streampack/usage.md) for byte-value replacement,
packing and selected transfer.

## Keep hot geometry work with its consumer

For a sequential run, use a carried range rather than rebuilding every start. Resolve
the schema once around the complete operation:

```cpp
sc::author::dispatch(*source, [&](auto a) {
    // Caller has admitted this rank interval and stable contents.
    a.range(first, count, [&](unsigned rank, sc::location p) {
        consume_child(rank, p.offset, p.size);
    });
});
```

The typed access is for admitted contents and ranks; ordinary `sc::range` checks the
interval first. Callbacks visit live ranks and cannot mutate or rebind the source.
For point consumers needing only a start, use `start` rather than reading size too.
StreamPack's start-plus-used-length path does this.

For native consumers, `sidecar::native::read16(a,group)` returns offsets, sizes and a
live mask. Keep those vectors with the fused sink or scoped chain rather than
materializing a stack array. The partial final group has readable padding and masked
inactive lanes; they are not extra ranks.

`author::encoder` is temporary computation for a pre-admitted sequential rewrite.
It carries the new position and local end, and emits sizes/checkpoints. It chooses no
sorting, movement, count or retirement policy. An in-place consumer must capture
original boundaries before overwriting them; the shared original-geometry walker does
this. StreamPack composes these bodies for value packing and transfer.

## Choose search execution for the workload

Ordinary calls are the starting point. The default combines conditional-select
narrowing with explicit SIMD convergence. Numeric bindings can retain prediction state
when their checks find it useful, with exact repair for skew and duplicates. These are
internal choices, not a caller-selected lerp/shotgun/bisection enum.

| Consumer | Starting choice | What to measure |
| --- | --- | --- |
| A few point/range queries | Ordinary lower/upper/overlap calls | Complete child/value consumption, not only the returned rank. |
| Small hot native endpoint arrays | Compare default against `author::scan_source<T>` | Dependent latency and independent throughput; scans can lose as lists grow or queries qualify early. |
| Repeated queries over a large immutable numeric source | Cache `author::bind_search(direct_source)` | Binding cost and reuse count, including skew/duplicates. |
| Several independent native queries | `author::lower_bounds` / `upper_bounds` over arrays | Full query preparation and selected children; these interleave unresolved probes. |
| Checkpoints available, local group cold | Prepared local phase, optionally scheduled through Loom | Frame/refill/drain cost and actual unresolved reads. |
| Consecutive ranks | Carried range walk or native `read16` | Avoid reconstructing each local prefix separately. |

For repeated queries on one coherent version, source-level preparation can be reused:

```cpp
#include <ikea/boundlist/author/search.h>
const auto searched = bl::author::bind_search(maxima);
const auto found = bl::lower_bound(searched, 10u);
const auto found_ranks = bl::author::lower_bounds(
    std::array{searched, searched}, std::array{10u, 35u});
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
guide](../integration.md) for visibility and retention, and
[StreamPack usage](../streampack/usage.md) for payload growth and packing. Old readers
need old coherent geometry and data; copying an allocation handle does not retain them.
