# Using Boundlist

Boundlist searches ordered boundaries and manages reservation metadata without
owning the corresponding payload. A directory can use it to route a key or find
the first and last children whose occupied ranges overlap a query. StreamPack
uses its reservation format to locate variable-length byte values.

Link `ikea::boundlist` and include `<ikea/boundlist.h>`. Start with the
[ordinary example](../../examples/boundlist/ordinary.cpp) and
[composition example](../../examples/boundlist/composition.cpp).
The [reference](reference.md) defines ordering, population and lifetime precisely.
The [search qualification](../../../workbench/spikes/boundlist/GRADUATION.md)
and [geometry/value ownership assessment](../../../workbench/spikes/boundlist/OWNERSHIP.md)
keep performance evidence and the reasons for the chosen seams.

## Route a point or range

A source borrows ordered endpoints and supplies its assigned count. Searches
return a rank, with `source.size()` meaning no answer. The count includes empty
and retired ranks; eligibility is a separate fact.

```cpp
namespace bl = ikea::boundlist;
const bl::direct_source maxima{maximum_keys.data(), unsigned(maximum_keys.size())};
const auto child = bl::lower_bound(maxima, key); // first maximum >= key
```

For unique keys and exact child maxima, this single boundary search is the
usual routing operation. An absent key can still route to a child: the owner
checks the records there. Do not search both minima and maxima for this case.
`upper_bound(source, key)` instead finds the first endpoint strictly greater
than the key. Duplicates do not add work to the unique-key path.

For range overlap, supply independently ordered **occupied minima and maxima**
with matching count and population:

```cpp
const bl::direct_source minima{minimum_keys.data(), unsigned(minimum_keys.size())};
const auto children = bl::overlap(maxima, minima, bl::query_range{low, high});
for (unsigned r = children.first; r != children.limit; ++r) {
    // Check eligibility if there are holes, then filter records in this child.
}
```

The default query is `[low, high)`. Set `low_closed` and `high_closed` for other
endpoint policies. The result is `[first, limit)`, or `{count, count}` when empty.
Internal holes remain holes; occupied extrema can enclose local gaps. Equal
minima/maxima describe a populated singleton, not an empty child.

Use `populated_source{source, population{bytes}}` when a bitset identifies
eligible ranks. A set bit normally means eligible; `inverted = true` accepts a
tombstone bitmap. Boundary calls skip ineligible ranks; overlap trims the result's
edges. The bitmap needs exactly `ceil(count/8)` readable bytes, without word padding.

## Choose storage without changing the search

| Source | Useful when | Placement and execution |
| --- | --- | --- |
| `direct_source<T>` | Endpoints are already native, or the domain has an order but no useful delta encoding | Borrowed pointer/count. Unsigned native lanes use conditional-select bisection and an explicit SIMD convergence window. Other ordered types use ordinary bisection. |
| `width_source<W, Point>` | Ordered positions have narrow nonnegative deltas | W is u8/u16; one group-start checkpoint per 16 widths, including the origin. Point can be u8/u16/u32/u64. Each width group is fully initialized/readable. |
| A custom source | Endpoints are packed, derived, or have another ordered domain | Supply `unsigned size()` and `at(rank)`. Bind a packed decoder once; compare directly without making a decoded array. Optional native boundary/phase entries can preserve a specialized kernel. |
| `sidecar::view` or its typed access | Mutable reservations need R, optional U and optional tombstones | The curated 256-rank Direct/Width format described below. The same ordinary boundary calls work on it. |

Admit `size() <= maximum_count` before converting a wider container count to
unsigned at binding. Queries outside an unsigned source's lane domain are classified before narrowing.
A u16 source can therefore accept a u64 query without wrapping it into the list.
The generic ordered-source contract has no 64-bit key ceiling. Each source has
unsigned local ranks; partition a directory that needs wider outer indices.

Include `<ikea/boundlist/author/search.h>` for measured execution alternatives.
`author::scan_source<T>` substitutes a four-accumulator SIMD scan for a small hot
native list. It supports u16/u32/u64 and admits at most 65535 ranks. Use ordinary
`lower_bound`/`upper_bound` on it; choose at binding rather than inside each query.
A full scan's predictable work can help a hot list but reads the whole list.
It is not the default for cold or larger directories.

For repeated searches of an unchanged native numeric list, prepare a search
binding once:

```cpp
const auto searched_maxima = bl::author::bind_search(maxima);
const auto child = bl::lower_bound(searched_maxima, key);
```

The binding privately chooses ordinary search or a checked interpolation bracket
from count and sampled geometry. It caches a reciprocal, so a selected query
does not divide. Both bracket edges are checked; an inaccurate prediction falls
back to bisection, preserving exact lower/upper-bound answers with duplicates.
Small or strongly irregular lists retain ordinary search. The same binding works
in `overlap` and population composition; it adds no stored-byte encoding.
Rebuild it after **any endpoint, count or placement change**. It is derived state
for one coherent version, not a live view of later mutations. Keep source setup
outside the repeated query loop and account for its cost when versions are short
lived. A binding uses 40 bytes for u16/u32 and 48 for u64 on supported platforms.

Use that preparation primarily for repeated point queries on larger regular
lists. Keep ordinary sources for small lists and long duplicate runs. Complete
two-extrema consumers have a different crossover: the qualified N4096 u32 range
consumer improves on NEON but loses several ns on AVX-512. A prepared binding is
not a blanket replacement for every endpoint source. The
[scorecard](../../../workbench/spikes/boundlist/evidence/graduation/scorecard.md)
keeps those cases beside the wins, including setup cost and descriptor bytes.

`author::lower_bounds(sources, queries)` and `upper_bounds` interleave native
probes for a fixed-size array of direct sources or prepared search bindings. They preserve query
order and include domain preparation. This requires independent queries; it
does not accelerate a single dependent traversal. The production comparison
keeps the complete eight-query consumer beside eight independently fused calls.

## Split a cold read at a real dependency

A Width source can route through available checkpoints before fetching its local
width group. Ordinary calls inline both steps. For a caller that already owns a
Loom operation, prepare the same search and retain the result across a wait:

```cpp
const auto pending = bl::author::prepare_lower_bound(bounds, key);
const auto next_bytes = pending.local_window();
// Owner may hint these bytes and yield. Retain coherent source and backing.
const auto rank = pending.finish();
```

The raw Width phase is 16 bytes: a selected width pointer, first rank and local
threshold. The global point/checkpoint width no longer participates in finishing.
A population wrapper additionally borrows the named source for final selection.
Keep that source and its format stable until `finish()`. Raw geometric phases
snapshot their routing; they borrow backing bytes, not the source object or
query. All completion dependencies must describe the same retained version.
The author factory conservatively requires a named source because population or
custom phases can borrow it.

`local_window()` is a useful next hint target, **not** a read grant or a promise
that finishing will not miss. Eligibility may also be read on completion.
Raw direct-source preparation completes its search immediately and has no local
window. A prepared numeric binding defers endpoint reads: its phase points to
the proposed bracket, or the full array when ordinary search was selected.
Correction may read outside the proposed bracket. The owner chooses whether splitting pays, supplies actual read
coverage, and enters Loom outside Boundlist.

The typed reservation-sidecar access also exposes the Width phase, so a composed
StreamPack consumer can prepare from its existing typed access. Dispatch the
whole operation once. Its admitted directory proves all sixteen checkpoints
readable, allowing a fixed SIMD selector with inactive results masked. A generic
`width_source` instead promises only its assigned checkpoints; it does not inherit
that padded-read requirement.
When using the free preparation factory on a typed access, keep that access
alive through population selection. Returning its borrowed phase from a dispatch
callback would outlive the access. The access's member `prepare<edge>` returns
only geometric routing; its snapshot does not perform eligibility selection.

## Manage reservation metadata

`boundlist::sidecar` stores reservation geometry and population for up to 256
ranks, with reservations of 0–65535 units, a schema maximum and an independent
total limit. It has no value length or payload interpretation. This is a curated
physical recipe, not a ceiling on generic Boundlist coordinates.

Prepare `sidecar::layout` once, then bind a **named** `sidecar::view` to three
disjoint windows: directory, lengths and control. Binding proves placement;
initialize fresh bytes or validate recovered contents before using them.

- **Direct** stores exclusive reservation ends. Random location uses the current
  and preceding ends; ordered traversal carries the preceding end.
- **Width** stores one group-start checkpoint per sixteen reservation widths.
  There is no terminal checkpoint. The length region grows in complete G16 groups.
- `immortal` omits tombstones; `fixed_reservation` derives geometry. Zero geometry
  stores neither widths nor endpoints. Keep facts the schema can guarantee.

`append(dst, reservation, effects)` assigns the next rank. `retire` marks a rank
without changing its boundaries. `locate` returns its offset and reservation,
while `range` carries the preceding boundary through live ranks. `reverse` maps
a physical position to its reservation owner **including tombstones**; zero
reservations own no positions. `sidecar::overlap(low, high)` finds the first/last
positive reservations overlapping `[low, high)`, optionally excluding retirement.
Logical occupied-extrema overlap is the root `boundlist::overlap` operation.

Boundlist does not compact storage, choose replacement reservations, move
children/values or repair rank holders. StreamPack owns value packing and
selected value transfer. Other owners choose their own geometry-rebuild policy;
use initialized destination geometry and append chosen reservations. Do not
apply storage compaction semantics to a keyspace merely because both have bounds.

For fused consumers, dispatch once around the complete operation and retain the
typed `sidecar::author::access`. It names the source and shared immutable format;
`rebind` requires that exact layout identity. Native `read16` yields offsets,
reservations and a live mask. Its scalar chain carrier likewise carries R.
StreamPack owns its separate value packet, whose second field is U.

Author geometry writes expose small origin/width/slot and count/extent kernels
for an admitted enclosing rewrite. These do not choose new widths or establish
coherent publication. The typed `slots(first, count, original_start, sink)`
traversal includes retired and zero-width ranks, passing rank, original offset
and R. It captures each original reservation before calling the sink, so a
consumer can move data and rewrite the already-read slot in the same pass.
Keep unread geometry, count and format stable throughout that traversal.

Effects identify the actual named source and issued byte ranges. They do not
select a version or grant read protection. A StreamPack composition retains one
coherent source and maps geometric effects into its own planes; it does not
publish on a temporary metadata projection. See the [reference](reference.md)
for exact storage, tail and lifetime requirements.
