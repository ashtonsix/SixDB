# Using Boundlist

Use Boundlist to route a point or bound the ranks that overlap a range. Supply existing
ordered boundaries for an index, or bind the curated sidecar when you want compact,
mutable subdivision metadata. Use StreamPack when those subdivisions contain
variable-length byte values. Link `ikea::boundlist` and include `<ikea/boundlist.h>`;
the [reference](reference.md) owns exact storage and lifetime rules.

## Route existing boundaries

For unique keys with exact occupied child maxima, start with one lower-bound search.
Equal maxima and duplicate keys remain valid; use a range query when the answer can span
several children.

```cpp
namespace bl = ikea::boundlist;
const bl::direct_source maxima{max_values.data(), unsigned(max_values.size())};
const auto rank = bl::lower_bound(maxima, key); // first maximum >= key
if (rank != maxima.size()) consume_child(rank);
```

`size()` is the absent sentinel. `upper_bound` selects the first endpoint strictly
greater than the query. Native arrays can be unaligned when their declared bytes are
readable. Wider or signed queries are classified before narrowing. A source can instead
borrow an already-bound SeriesPack/TuplePack decoder: implement `size()` and ordered
`at(rank)`, keeping decoding and schema preparation outside the probe loop. Do not
decode a temporary endpoint array merely to use the search interface. Custom comparison
domains need no integer conversion.

To find the first and last children potentially overlapping `[low,high)`, supply
matching ordered occupied minima and maxima:

```cpp
const bl::direct_source minima{min_values.data(), unsigned(min_values.size())};
const auto window = bl::overlap(maxima, minima, bl::query_range{low, high});
for (unsigned r = window.first; r < window.limit; ++r)
    if (eligible(r)) scan_child(r, low, high);
```

Attach the same `population` through `populated_source` when Boundlist should trim
ineligible ranks at the edges. It does not remove internal holes from the result.
Singleton bounds can contain keys; equal adjacent physical ends instead describe a
zero-size interval. The [range diagram](reference.md#points-ranges-and-eligibility)
shows that distinction.

## Pick a fast search path

| Consumer | Starting choice | What to measure |
| --- | --- | --- |
| A few point/range queries | Ordinary lower/upper/overlap calls | Complete child/value consumption, not only the returned rank. |
| Small hot native endpoint arrays | Compare default against `author::scan_source<T>` | Dependent latency and independent throughput; scans can lose as lists grow or queries qualify early. |
| Repeated queries over a large immutable numeric source | Cache `author::bind_search(direct_source)` | Binding cost and reuse count, including skew/duplicates. |
| Several independent native queries | `author::lower_bounds` / `upper_bounds` over arrays | Full query preparation and selected children; these interleave unresolved probes. |
| Checkpoints available, local group cold | Prepared local phase, optionally scheduled through Loom | Frame/refill/drain cost and actual unresolved reads. |
| Consecutive ranks | Carried range walk or native `read16` | Avoid reconstructing each local prefix separately. |

The default combines conditional-select narrowing with explicit SIMD convergence. Opt-in
scans keep the ordinary result semantics. Numeric bindings can retain prediction state
where their checks find it useful; exact repair handles skew and duplicates. The
algorithm has no caller-selected lerp/shotgun/bisection enum. Do not recreate a private
algorithm menu around every call.

```cpp
#include <ikea/boundlist/author/search.h>
const auto searched = bl::author::bind_search(maxima); // once per coherent version
const auto rank = bl::lower_bound(searched, key);
const auto ranks = bl::author::lower_bounds(
    std::array{searched, searched}, std::array{key_a, key_b});
```

Rebuild derived bindings after endpoint/count/placement changes. Keeping the pointer
unchanged does not keep a prediction model valid. Bind small/one-shot sources directly
when setup cannot be amortized. Measure the actual source shape; a short SIMD scan can
beat even a good interpolation estimate.

## Store compact subdivision geometry

Choose semantic omissions first, then a codec. A fixed size stores no ends or sizes.
`immortal` removes tombstones. Otherwise choose:

- **Absolute Ends** for a short point path or direct endpoint comparisons,
  especially when the whole extent already fits a narrow carrier.
- **Sizes** when individual sizes are narrower than local ends, size-only work
  matters, or sizes change frequently.
- **Relative Ends** when local ends fit the same carrier as sizes but global ends
  are wider, and point/inverse reads matter.

These are workload choices, not a universal ranking. Absolute Ends reserves its whole
end directory up front; checkpointed formats grow their local-field groups with
occupancy. Relative Ends can widen at a carrier boundary: M=4095 permits u16 local ends,
while M=4096 can require u32. Check the schema's independent extent bound before paying
that widening. Keep the existing Sizes default unless your consumer earns a different
choice in complete measurements.

```cpp
namespace sc = bl::sidecar;
const auto format = sc::layout::prepare({
    .family = sc::geometry::relative_ends,
    .maximum = 4095, .extent_limit = 1'048'320});
assert(format);
// Owner supplies disjoint, sufficiently sized and aligned windows.
auto source = sc::view::bind(*format, {directory, lengths, control});
assert(source);
ikea::source_write_journal effects{records};
assert(sc::initialize(*source, effects));
assert(sc::append(*source, 5, effects));
assert(sc::append(*source, 0, effects));
assert(sc::append(*source, 8, effects));
assert(sc::reverse(*source, 5)->rank == 2); // rank 1 owns no positions
```

Keep `format` and the named `source` stable while accesses/effects borrow them. Use
`directory_bytes()`, `lengths_bytes(assigned)`, `control_bytes()` and
`lengths_alignment()` for physical admission. Grow initialized groups under one fixed
schema; changing allocation size does not change carriers. Bind admits placement,
`validate` admits external contents, and `initialize` constructs new empty content. Do
not repeatedly validate an unchanged admitted block in a hot loop. The [ordinary
example](../../examples/boundlist/ordinary.cpp) is executable and includes retirement,
overlap and source-qualified effects.

## Fuse geometry with the consumer

Dispatch around a useful operation, not each slot:

```cpp
sc::author::dispatch(*source, [&](auto a) {
    // Ordinary admission already proved this rank run.
    a.range(first, count, [&](unsigned rank, sc::location p) {
        consume_child(rank, p.offset, p.size);
    });
});
```

A range carries the preceding end or position. For native consumers,
`sidecar::native::read16(a,group)` returns offsets/sizes plus a live mask; keep its
vectors in the fused sink or scoped chain instead of materializing a stack array. One
group may be a partial final G16; its inactive lanes are masked. Point consumers that
only need a start should use `start`, avoiding an unnecessary current-end/size read.
StreamPack's start+U path follows that rule.

`author::encoder` is temporary computation for an admitted sequential rewrite. It
carries the new position and local end, emits caller-chosen sizes/checkpoints, and
chooses no sorting, movement, count or retirement policy. A consumer that rewrites the
same storage must capture original boundaries before overwriting them; the shared
original-geometry walker does this. StreamPack composes these bodies for its value
packing and transfer. Do not build a second reservation manager or allocator around
them.

For numeric edits, choose the intended law: `set_boundary` preserves later ends;
`resize` translates them. The owner moves or repairs corresponding data/children at the
same publication cut. Inserting/deleting middle ranks or sorting produces a new stream
and explicit remap. Source-qualified effects describe issued bytes, including preserving
vector stores; they are not MVCC protection by themselves.

## Stage only a useful dependency

Checkpointed deltas and relative ends expose a local group through a prepared phase.
This example uses deltas with a nonzero 64-bit origin:

```cpp
const bl::delta_source<std::uint8_t, std::uint64_t> bounds{
    sizes.data(), checkpoints.data(), count, 16, extent};
const auto pending = bl::author::prepare_lower_bound(bounds, key);
if (pending.has_local_read()) {
    hint(pending.local_window());
    // Caller may yield through Loom, retaining the coherent version.
}
const auto rank = pending.finish();
```

Ready work stays inline; an Ikea phase does not enqueue itself. Hints neither make bytes
readable nor pin their version. Retain boundaries, eligibility, backing and any derived
binding through completion. Population selection borrows the original named source. The
[composition example](../../examples/boundlist/composition.cpp) also shows packed
endpoints, reusable numeric bindings and independent queries. Use actual Loom Ring
comparisons before introducing a wait on a hot group.

For payload growth, compaction and retained readers, follow [StreamPack
usage](../streampack/usage.md) and the [owner integration guide](../integration.md). Old
scopes need the old coherent geometry and data; aliases of an allocation handle do not
create that retention.
