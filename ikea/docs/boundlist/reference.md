# Boundlist (historical compatibility)

The maintained module is now [Intervals](../intervals/usage.md). StreamPack owns
compact geometry and retirement. This page records the old Boundlist surface
retained for historical controls; it is not a new consumer contract.

Boundlist describes **contiguous intervals using ordered cuts**. It locates an
interval by rank, finds the interval containing a point, and bounds the ranks
overlapping a range. The consumer owns anything associated with an interval.

Link `ikea::boundlist` and include `<ikea/boundlist.h>`. The
[usage guide](usage.md) shows storage setup and complete consumer patterns.

## Cuts and ranks

Consecutive cuts `cut[r]` and `cut[r+1]` define rank `r` as
**`[cut[r],cut[r+1])`**: include the left cut, exclude the right.
An ordered list of `n+1` cuts defines `n` intervals. For example,
`[10,15,16]` defines `[10,15)` and `[15,16)`. Shared cuts make the
geometry contiguous. Equal cuts define a zero-size interval, which owns no points.

![Six contiguous intervals share consecutive cuts. The query from 9 inclusive to 36 exclusive overlaps ranks 1 through 4. Excluding rank 2 leaves its cuts unchanged.](images/ranges.svg)

A source supplies one **exclusive end per interval** plus its origin. Thus source
indices are interval ranks, including when an adapter borrows a complete cut array.
The last end is the extent. An empty source has extent equal to origin.

## One geometry contract

All ordinary geometry operations accept the same small source contract:

| Member | Meaning |
| --- | --- |
| `size()` | Interval count, as `unsigned`, up to `maximum_count`. |
| `origin()` | Initial cut. |
| `at(rank)` | Exclusive end of that interval. |

Ends must be nondecreasing and at least the origin. Ordering is sufficient;
coordinates need no subtraction, universal integer width or payload metadata.
The owner admits those laws and retains coherent backing throughout an operation.
Optional `extent()` and typed dispatch hooks let implementations avoid repeated
loads or format decisions; they add no required geometry state.

There are several ways to supply that contract:

| Source | Limits and storage |
| --- | --- |
| `from_cuts(span)` | Accepts `n+1` cuts, copying the origin/count and borrowing the ends; at least one cut is required. |
| `from_ends(ends,origin)` | Adds the initial cut to any ordered endpoint projection exposing `size()` and `at()`. |
| Custom source | Projects the three members directly from existing storage. |
| `view` / `const_view` | Compact mutable/read-only geometry: up to 256 intervals, origin zero, each size up to 65535. |

Renew an adapter after changing its origin, count or backing placement.

Borrowed and custom sources can cover larger counts, nonzero origins, full-width
integers, strings or other ordered keys. The compact format's limits describe that
encoding, rather than restricting the common operations. Its three encodings and
schema omissions are described below.

## Geometry operations

| Operation | Result |
| --- | --- |
| `locate(source,rank)` | Optional `interval<Point>{first,limit}`; absent for an invalid or explicitly excluded rank. A zero-size rank succeeds. |
| `reverse(source,point)` | The containing rank, or `source.size()` when outside the extent or explicitly excluded. |
| `overlap(source,low,high)` | `rank_window{first,limit}` bounding positive-size intervals intersecting `[low,high)`; empty is `{n,n}`. |
| `range(source,first,count,sink)` | Calls `sink(rank,interval<Point>)` for assigned ranks in that rank interval, after preflighting the whole interval. Returns a rank error without callbacks on invalid input. |

Point ownership uses the first end **strictly greater than** the point. In the
diagram, point 18 belongs to rank 2; point 56 lies outside the extent. A range
intersects an interval when its end is `> low` and its start is `< high`.
Reversed or empty queries have empty results; queries may extend beyond the geometry.
The diagram's `[9,36)` query returns ranks `[1,5)`.

Raw geometry operations include retired ranks. Selection is a separate, explicit
binding; it never changes the cuts or extends a neighbor into an excluded span.

### Select ranks explicitly

`selected_source{geometry,rank_selection{bits}}` attaches a rank bitset. Set bits
mean eligible; `inverted=true` supports a tombstone bitmap. Nonempty bitsets need only
`ceil(n/8)` readable bytes, with no word padding requirement. An empty
`rank_selection{}` selects nothing; `rank_selection::all()` selects every rank.

A compact view's `live_ranks()` returns the complement of its retirement bitmap,
or all ranks when `immortal`. Reading that companion is opt-in:

```cpp
namespace bl = ikea::boundlist;
const bl::selected_source live{source, source.live_ranks()};
```

`reverse(live,20)` returns `n` when rank 2 is excluded: the point does not become
owned by rank 3. `locate` likewise rejects excluded ranks. A selected `range`
visits only eligible ranks, including eligible zero-size ranks. Selected `overlap`
trims excluded and zero-size edge ranks, but internal holes remain inside its
candidate window. Traverse that window through the same selected source to skip them.

### Endpoint searches are smaller building blocks

`lower_bound(source,q)` returns the first eligible endpoint **>= q**;
`upper_bound(source,q)` returns the first eligible endpoint **> q**.
Both return `size()` when absent, and both find a **first** endpoint.
For ends `[3,7,7,10]`, querying 7 returns lower bound 1 and upper bound 3.

These searches require only `size()` and `at()`. `direct_source<T>` borrows an
ordered array; it is also a useful input to `from_ends`. Native unsigned loads may
be unaligned within the supplied readable bytes. Wider or signed integer queries
are classified before narrowing. Other domains use their own ordering.

A selected endpoint search may skip the containing rank to return a later eligible
end. That is useful for searching boundaries, but differs from point ownership.
Use `reverse` when asking which interval contains a point. Policies involving
independent occupied minima/maxima belong to the consumer and can compose these
endpoint searches there.

## Three geometry encodings

The compact format describes contiguous intervals starting at position zero. An
interval's **exclusive end** is the sum of sizes through that rank. Its start is zero
at rank 0, otherwise the previous end. The last end is the whole source's **extent**.

For sizes `[5,0,8]`, the ends are `[5,5,13]`. Rank 0 owns `[0,5)`, rank 1 owns no
positions, and rank 2 owns `[5,13)`. Position 5 therefore belongs to rank 2. Equal cuts
make a zero-size interval; they do not create a gap. Zero size and retirement are
independent: a live or retired interval can have size zero.

There are three encodings of the same geometry. Checkpointed encodings use groups of
16 ranks; a checkpoint `C[g]` is the absolute start of group `g`.

| Encoding | Stored information | Main tradeoff |
| --- | --- | --- |
| **Absolute Ends** | Absolute exclusive end `E[r]` per rank. | Cheap direct access and endpoint comparisons; fields follow the whole extent bound. |
| **Sizes** | Size `R[r]` per rank, plus one checkpoint per 16 ranks. | Narrow fields and cheap resizing; random starts need a local prefix sum. |
| **Relative Ends** | Group-local cumulative end `L[r]` per rank, plus the same checkpoints. | Direct local lookup and endpoint comparisons; local ends can be wider than sizes. |

![The same five-rank excerpt encoded as absolute ends, sizes and relative ends, with the group origin and arithmetic needed to locate rank 18.](images/encodings.svg)

A group has one checkpoint, including the first group's zero origin. There are
`(assigned+15)/16` meaningful checkpoints and **no terminal checkpoint**; the directory
reserves capacity for all 16. The final extent supplies the last global end. Relative
ends reset at each group, so their raw field array is not globally ordered.

### Prepare the schema and its field widths

Prepare an immutable `layout` from `description`. Its `family` chooses
`geometry::absolute_ends`, `sizes` or `relative_ends`; Sizes remains the default.
The old names `direct` and `width` are aliases for the first two values, not extra
formats.

The schema specifies two independent bounds: per-interval size maximum **M** (`maximum`)
and total extent limit **B** (`extent_limit`). M is at most 65535 and B at most 256M.
For each field, preparation chooses the smallest native carrier among u8/u16/u32 that
can hold its proven bound:

| Field | Bound |
| --- | --- |
| Absolute end or checkpoint | B |
| Individual size | min(M,B) |
| Relative end | min(16M,B) |

Widths are fixed schema facts; a smaller current allocation or occupancy cannot narrow
them. For example, M=4095 permits u16 local ends, while M=4096 can require u32 unless B
proves a tighter bound.

An optional `fixed_size` derives all geometry without stored ends or sizes. A zero-size
domain likewise needs no stored boundaries. `immortal` omits tombstones and therefore
cannot express retirement. These omissions do not remove assigned ranks or their count.

## Geometry access and mutation

### Bind storage before reading or writing it

A compact view borrows three disjoint byte windows. They may be spans of one owner
allocation. The layout reports their required extents and alignment. Mutation calls
also take an effects sink that reports source-qualified byte writes to the owner;
the effect plane number identifies the window. Effects do not themselves publish a
transaction or protect readers.
Effect ranges name encoded metadata bytes, not coordinates in the partitioned domain.

| Window / effect plane | Contents |
| --- | --- |
| Directory /0 | Optional 32 B tombstone bitmap, then 256 absolute ends or 16 checkpoints. Fixed/zero geometry omits both end and checkpoint arrays. |
| Lengths /1 | Sizes or Relative Ends fields, in initialized 16-rank groups; absent for Absolute Ends/fixed/zero geometry. |
| Control /2 | u16 assigned count. Variable checkpointed geometry also stores u32 extent at byte 2. |

Directory storage remains full capacity; lengths grow with assigned groups. Unassigned
tails in the final initialized length group are zero and readable, but are not ranks.
Lengths require the stored carrier's natural alignment. All multibyte fields are little
endian. There is no padding or tombstone/checkpoint-interleaving schema switch.

The setup sequence distinguishes placement from contents:

1. Prepare the immutable layout.
2. `view::bind` checks directory/control minimum extents, lengths alignment, pointer
   ranges and disjointness, including nonoverlap with the layout object. It does **not**
   inspect encoded contents.
3. For new storage, `initialize` constructs an empty assignment. For external stored
   content, `validate` checks count, monotonicity/sizes, checkpoints, extent and
   initialized tails before reads.

Successful checked mutations preserve those content rules. Ordinary rebinding after
growth need not rescan unchanged, already-admitted contents. The layout and named view
must stay at stable addresses while an access, phase or effect record borrows them.

### Choose the intended edit

| Checked command | Meaning |
| --- | --- |
| `initialize` | Reset directory and control to an empty assignment. |
| `append` | Assign the next slot; initialize a new readable 16-rank group as needed. |
| `retire` | Set the containing tombstone bit; leave geometry unchanged. |
| `set_boundary` | Move one exclusive end while preserving later ends. |
| `resize` | Change one size and translate every subsequent endpoint. |

For example, starting with ends `[5,9,12]`, setting rank 0's boundary to 6 produces
`[6,9,12]`; resizing rank 0 from 5 to 6 produces `[6,10,13]`. Both commands check
size/order/extent bounds and effect capacity before storing. Fixed geometry rejects
incompatible changes. All checked mutation refusals preserve bytes and effect state.

Geometry edits change only metadata. The consuming structure coordinates the
corresponding data change and publication. Inserting, removing or reordering middle
ranks constructs a new representation with an explicit rank remap.

## Composition and retained versions

### Keep ready geometry work with its consumer

A typed access exposes `start`, `slot_size`, physical `reverse`, carried traversal
and native group decoding. Dispatch once around the complete consumer rather than once
per rank. A sequential traversal carries its position; a native `read16` lets a fused
consumer use offsets, sizes and an active mask without an obligatory intermediate array.
Raw decoding includes every assigned rank. Wrap the typed access in `selected_source`
to intersect its native active mask with an explicit selection. Read callbacks cannot
mutate or rebind the source.

The admitted sequential encoder carries new geometry while a consumer chooses movement
and ordering. An in-place rewrite must capture original boundaries before overwriting
them; the shared original-geometry walker and encoder support that coordinated rewrite.

### Retain read dependencies and report writes separately

Read-projection visitors describe an operation's metadata dependencies:
`visit_start_metadata` reports the start path and `visit_metadata` adds size/liveness.
Consumers account for their own additional metadata and payload dependencies. These
are point-read projections, not complete reverse/search footprints. A typed
checkpoint search can read all 16 initialized checkpoints even at partial occupancy.
The owner retains the relevant control, planes and readable padding; a point visitor
is not a universal search grant.

Writes use `before(source,byte_write)` to identify the original source and actual issued
stores, including preserving vector lanes. These **effects** are neither read protection,
beforeimages nor a transaction. A policy can coarsen damage or erase it for authorized
scratch work. Before execution, the owner supplies effect capacity and exclusive authority
over every issued byte.

Inputs, effect storage, source/view/layout objects and writable outputs must satisfy
their alias and lifetime contracts. Hooks cannot fail, allocate, suspend or invalidate
admitted inputs between preflight and stores. Typed `rebind` requires the exact same
layout object, not merely an equivalent description.

### Expose an unresolved read only when useful

`prepare_lower_bound` and `prepare_upper_bound` return a phase whose `finish()`
produces the ordinary search result. When `has_local_read()` is true,
`local_window()` identifies a useful next-read hint. The caller can prefetch or wait
through Loom; Boundlist itself neither waits nor enqueues work. Ready arithmetic and
native/CPS stages remain bounded and inline where useful.

![Checkpointed and prepared numeric searches expose unresolved local work while one retained version covers boundaries, eligibility, backing and derived search state.](images/phases.svg)

The local window is a hint, **not the complete read/protection footprint**. Selection
can still read eligibility; numeric prediction can correct outside its proposed bracket.
Across any wait, the owner retains one coherent version of boundaries, count,
eligibility, backing and derived search state. Selection-aware completion also borrows
the original named source; retaining a temporary typed dispatch object is insufficient.

## Portable descriptor

The portable descriptor is eight bytes. New standalone **version 4** uses byte 0: bits
5–7=`4`, bits 0–1=encoding 0/1/2, bit 2=reserved zero, bit 3=immortal, bit
4=fixed-size-present. Bytes 1–2 hold M, bytes 3–5 hold B and bytes 6–7 hold fixed size
(zero when absent). Version 3 decoding preserves its original two-encoding carrier laws
and re-encodes that version. Prepared objects and pointers are not portable descriptors.
Conversion constructs a new representation; changing a tag does not convert its bytes.

[Owner integration](../integration.md) covers Loom/Orbital visibility, retention and
effects. Performance evidence and algorithm selection live
with the [Boundlist study](../../../workbench/spikes/boundlist/README.md).
