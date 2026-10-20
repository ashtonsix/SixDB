# Boundlist

Boundlist maps ordered points and ranges to ranks. An index can use it to find which
children might overlap a query; StreamPack uses the same geometry to locate
variable-length byte reservations. Boundlist holds no payload, allocation, transaction
or scheduler state.

There are two ways to supply boundaries. **Ordered sources** borrow existing arrays or a
consumer's decoder, with caller-chosen counts and domains. The **curated sidecar**
stores up to 256 numeric subdivisions with compact geometry and optional retirement
bits. Neither requires StreamPack. StreamPack adds byte values, optional used lengths
and packing to that geometry.

Start with [usage and composition](usage.md) for calls and fast consumer patterns. This
document explains the model, stored formats and exact obligations.

## Points, ranges and eligibility

A source exposes `size()` and ordered `at(rank)` endpoints. `lower_bound(source,q)`
returns the first eligible endpoint **>= q**; `upper_bound` uses **> q**. Both return
`size()` when absent. Duplicate endpoints are supported. For unique keys and exact
occupied child maxima, one lower-bound search routes to the first possible child;
duplicate handling adds no second search to that operation.

For range queries, supply matching ordered occupied minima and maxima:
`overlap(maxima,minima,query_range{low,high})`. The default query is `[low,high)`; the
query type can also express either endpoint as open or closed. The result is
`rank_window{first,limit}`, with a one-past-last limit. Empty answers are `{n,n}`. Both
arrays must describe the same ranks and eligibility. Their extrema can still enclose
gaps inside children, so the consumer checks actual records.

![Occupied child ranges, a populated singleton and an ineligible internal rank. The query from 9 inclusive to 36 exclusive returns candidate ranks 1 through 4; eligibility still applies inside the window.](images/ranges.svg)

Eligibility is independent of endpoint equality. A populated child with occupied bounds
`[10,10]` contains a key. An empty physical interval `[37,37)` owns no positions.
`population` supplies `ceil(n/8)` readable bytes: set bits mean eligible, or ineligible
when inverted. It is a bitset, not a child cardinality. A `populated_source` adds that
selection to any geometry source. Internal holes remain holes inside an overlap result.

Native unsigned sources accept wider or signed integer queries without narrowing away
their order. Generic ordered domains can instead use their own comparisons, including
byte strings, wider keys and virtual top fences. Local ranks and counts are `unsigned`,
bounded by `maximum_count`; a larger structure composes these with its own outer
identity. No universal 64-bit keyspace or arithmetic distance is required by the search
contract.

## Three geometry encodings

The curated sidecar assigns stable ranks to nonnegative slot sizes. An assigned rank may
be live, retired or zero-size. Starting at zero, sizes concatenate into exclusive ends.
For group width 16, `C[g]` is that group's absolute origin.

| Encoding | Stored information | Main tradeoff |
| --- | --- | --- |
| **Absolute Ends** | Absolute exclusive end `E[r]` per rank. | Cheap direct access and endpoint comparisons; fields follow the whole extent bound. |
| **Sizes** | Size `R[r]`, plus one checkpoint per 16 ranks. | Narrow fields and cheap resizing; random positions need a local prefix sum. |
| **Relative Ends** | Local cumulative end `L[r]`, plus one checkpoint per 16 ranks. | Direct local lookup and endpoint comparisons; local ends can be wider than sizes. |

![The same five-rank excerpt encoded as absolute ends, sizes and relative ends, with the group origin and arithmetic needed to locate rank 18.](images/encodings.svg)

There are `(assigned+15)/16` meaningful checkpoints, including the first origin; there
is **no terminal checkpoint**. The directory reserves capacity for all 16 checkpoints.
The final extent supplies the last global end. Relative ends reset at each group, so
their complete raw field array is not globally ordered.

Prepare an immutable `sidecar::layout` from `description`: `family`, per-slot `maximum`,
independent `extent_limit`, optional `fixed_size` and `immortal`. The families are
`geometry::absolute_ends`, `sizes` and `relative_ends`. The old `direct`/`width` names
identify the same first two values; they do not select additional formats. The default
remains Sizes.

Let M be the schema size maximum and B its extent limit. The smallest native carrier
among u8/u16/u32 is chosen for each proven bound:

| Field | Bound |
| --- | --- |
| Absolute end or checkpoint | B |
| Individual size | min(M,B) |
| Relative end | min(16M,B) |

The curated M is at most 65535 and B at most 256M. Carriers are immutable schema facts,
independent of current occupancy or allocation extent. Fixed size derives all geometry;
a zero-size domain needs no stored boundaries. `immortal` omits retirement bits.
Changing a stored size to zero does not retire its rank.

The three disjoint windows may share one owner allocation:

| Window / effect plane | Physical law |
| --- | --- |
| Directory /0 | Optional 32 B tombstone bitmap, then 256 absolute ends or 16 checkpoints. Fixed/zero geometry omits both end and checkpoint arrays. |
| Lengths /1 | Sizes or Relative Ends fields, in initialized G16 groups; absent for Absolute Ends/fixed/zero geometry. |
| Control /2 | u16 assigned count. Variable checkpointed geometry also stores u32 extent at byte 2. |

All multibyte fields are little endian. Unassigned tails in the last initialized length
group are zero and readable, but are not ranks. Directory storage remains full capacity;
lengths grow with assigned groups. Required lengths alignment is the stored carrier's
natural alignment. There is no tombstone/checkpoint interleaving, padding or alignment
selection in the schema.

The portable descriptor is eight bytes. New standalone **version 4** uses byte 0: bits
5–7=`4`, bits 0–1=encoding 0/1/2, bit 2=reserved zero, bit 3=immortal, bit
4=fixed-size-present. Bytes 1–2 hold M, bytes 3–5 hold B and bytes 6–7 hold fixed size
(zero when absent). Version 3 decoding preserves its original two-encoding carrier laws
and re-encodes that version. Prepared objects and pointers are never portable
descriptors. Conversion constructs a new representation; changing a tag on existing
storage does not convert bytes.

## Physical ownership and mutation

`sidecar::reverse(source,point)` answers physical ownership:
`position_owner{rank,tombstone}` for a point below extent, otherwise no result. It finds
the first exclusive end **> point**, includes retired storage, and never assigns a point
to a zero-size slot. This differs from eligible endpoint search. An arbitrary
occupied-maxima array alone does not prove containing intervals.

`locate` returns `{offset,size}` for a live rank; invalid or retired ranks are errors.
`range` checks its rank interval before callbacks and visits live ranks. The typed
access exposes `start`, `slot_size`, physical `reverse`, carried traversal and native
group decoding for admitted consumers.

| Checked command | Meaning |
| --- | --- |
| initialize | Reset directory and control to an empty assignment. |
| append | Assign one more slot; initialize a new readable G16 group as needed. |
| retire | Set the containing tombstone bit; leave geometry unchanged. |
| set_boundary | Move one exclusive end while preserving later ends. |
| resize | Change one size and translate every subsequent endpoint. |

`set_boundary` and `resize` are different operations. Both check size/order/extent
bounds and effect capacity before storing; fixed geometry rejects incompatible changes.
They move no payload or children. The owner coordinates any corresponding data change.
StreamPack deliberately does not expose these as uncoordinated value edits. Reordering
or inserting middle ranks is an owner-selected rebuild, with rank repair, rather than a
disguised resize. Value compaction and selected transfer belong to StreamPack.

`bind` admits window extents, alignment, disjointness and descriptor placement; it does
not inspect content. Initialize new storage or validate external content before reading.
`validate` checks count, monotonicity/sizes, checkpoints, extent and initialized tails.
Successful checked commands preserve that admission. All checked mutation refusals
preserve bytes and effect state.

## Composition and retained versions

Read-projection visitors keep protection with the operation: `visit_start_metadata`
reports the start path, `visit_metadata` adds size/liveness dependencies, and
StreamPack's `visit_value_metadata` adds its independent U field. They describe point
reads, not all possible reverse/search reads. A typed checkpoint search can read all 16
initialized checkpoints even at partial occupancy. The owner retains the relevant
control/planes and readable padding; a point visitor is not a universal search grant.

`before(source,byte_write)` names the original source and actual issued stores,
including preserving vector lanes. It is neither read protection, a beforeimage nor a
transaction. A policy may coarsen damage or erase it for authorized scratch work. The
owner admits effect capacity and exclusive authority over **every issued byte** before
execution. Inputs, effect storage, source/view/layout objects and writable outputs must
satisfy their alias and lifetime contracts. Hooks cannot fail, allocate, suspend or
invalidate admitted inputs between preflight and stores.

A named view and its immutable layout stay at stable addresses while an access, phase or
effect record borrows them. `rebind` requires that same layout identity. Read callbacks
cannot mutate or rebind the source. Native packets and CPS stages stay bounded and
inline where useful; neither performs an implicit scheduler wait.

![Checkpointed and prepared numeric searches expose unresolved local work while one retained version covers boundaries, eligibility, backing and derived search state.](images/phases.svg)

`prepare_lower_bound`/`prepare_upper_bound` can expose one unresolved local read,
followed by `finish`. The window is a **hint**, not the complete read/protection
footprint. Selection can still read eligibility; numeric prediction can correct outside
its suggested bracket. The owner retains one coherent version of boundaries, count,
eligibility, backing and derived search state across any wait. Population-aware
completion also borrows the original named source; keeping a short-lived typed dispatch
object alive is not a substitute for that source.

For byte values, [StreamPack](../streampack/reference.md) adds optional U, U/payload
validation, replacement, packing and transfer without another geometry binding or
per-rank indirect call. [Owner integration](../integration.md) covers Loom/Orbital
visibility, retention and effects. Performance evidence and algorithm selection live
with the [Boundlist study](../../../workbench/spikes/boundlist/README.md).
