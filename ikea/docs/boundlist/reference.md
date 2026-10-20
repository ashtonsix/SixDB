# Boundlist

Boundlist searches ordered boundaries and describes contiguous subdivisions. An index
uses boundary searches to find candidate children for a point or range query.
StreamPack uses subdivision geometry to locate variable-length byte reservations.
Boundlist owns neither the children nor the bytes: it borrows caller-supplied metadata
and performs no allocation, transaction or scheduling.

A **rank** is a zero-based position in a source. A source with `n` assigned ranks has
valid ranks `0` through `n-1`. Boundlist supports two forms of source:

| Form | What the caller supplies | Typical use |
| --- | --- | --- |
| Ordered source | Existing endpoints, or an ordered projection over a decoder. Counts and comparison domains are caller-chosen. | Route queries through an index. |
| Curated sidecar | Storage for Boundlist's compact encoding of up to 256 contiguous subdivisions. | Maintain mutable subdivision geometry, with optional retirement bits. |

Both can be used without StreamPack. The [usage guide](usage.md) walks through ordinary
calls, storage setup and fast consumer patterns. This reference first explains search
results, then subdivision geometry, then the storage and composition contracts.

## Points, ranges and eligibility

### Search an ordered endpoint source

An endpoint source exposes `size()` and `at(rank)`. All assigned endpoints must be in
nondecreasing order, including endpoints at ranks excluded from results. Equal
endpoints are allowed. With no selection attached, every rank is **eligible**: it may
be returned by a search. An optional selection excludes ranks without renumbering them.

Both boundary operations return the **first eligible rank** satisfying their comparison:

| Operation | Condition at the returned rank |
| --- | --- |
| `lower_bound(source,q)` | Endpoint **>= q** |
| `upper_bound(source,q)` | Endpoint **> q** |

Neither returns the last matching rank. If no eligible endpoint satisfies the condition,
both return `source.size()`, the absent sentinel. Test that sentinel before accessing
the result.

For endpoints `[3,7,7,10]` with all ranks eligible, a query of `7` gives lower bound
`1` and upper bound `3`. The equal endpoints occupy the half-open rank window `[1,3)`:
ranks 1 and 2. Here “upper” describes the comparison's strictness, not a search backwards
from the end.

To exclude ranks, wrap a source in `populated_source` with a `population` bitset.
It supplies `ceil(n/8)` readable bytes; set bits mean eligible, or ineligible when
`inverted` is true. This is one bit per rank, not a child cardinality. Selection can
leave holes between returned ranks. It does not relax the source's ordering or
readability obligations.

### Bound the children overlapping a query

A child may contain many keys. Describe each child's occupied bounds using two sources:
`minima.at(r)` is its smallest key and `maxima.at(r)` its largest. These extrema are
**inclusive**: a populated child with bounds `[10,10]` contains the key 10. Both
sources must be nondecreasing and describe the same ranks and eligibility.

For a unique-key point query, `lower_bound(maxima,key)` selects the first possible child
in one search. It still checks whether the key exists among its records. Duplicates do
not add a second search to that call, but finding matches across children needs a range
query.

Call `overlap(maxima,minima,query_range{low,high})`. By default the query includes
`low` and excludes `high`: it is the point interval `[low,high)`. A child is a
candidate when its maximum is `>= low` and its minimum is `< high`.

The result is `rank_window{first,limit}`. It includes `first` and excludes `limit`;
`limit` is one past the last eligible candidate rank. Thus `{1,5}` covers ranks
1, 2, 3 and 4. An empty answer is `{n,n}`. Ineligible ranks at either edge are trimmed,
but internal holes remain; the consumer skips them. Occupied extrema can also enclose
gaps inside a child, so candidate children still check their actual records.

![Occupied child ranges, a populated singleton and an ineligible internal rank. The query from 9 inclusive to 36 exclusive returns candidate ranks 1 through 4; eligibility still applies inside the window.](images/ranges.svg)

The query's `low_closed` and `high_closed` flags independently choose whether each
point endpoint is included. Their defaults are `true` and `false`. An equal-endpoint
query `[q,q]` can find populated singletons; `[q,q)` is empty. Whatever the point
endpoint flags, the returned **rank window always uses an excluded limit**.

### Supply a different endpoint representation

`direct_source<T>` borrows an array. Native unsigned arrays permit unaligned loads
within their declared readable bytes. Other element types must be real `T` objects.
Native unsigned searches classify wider or signed integer queries before conversion,
so an out-of-domain query cannot wrap into the stored domain.

A custom source can instead project `size()` and ordered `at(rank)` from an
already-bound decoder. Comparisons need no arithmetic distance or conversion to a
universal integer domain: byte strings, wider keys and virtual top fences are possible.
Local counts and ranks are `unsigned`, bounded by `maximum_count`; a larger directory
supplies its own outer identity.

## Three geometry encodings

The curated sidecar describes contiguous slots starting at position zero. A slot's
**size** is its reserved extent, and its **exclusive end** is the sum of sizes through
that rank. Its start is zero at rank 0, otherwise the previous end. The last end is the
whole source's **extent**.

For sizes `[5,0,8]`, the ends are `[5,5,13]`. Rank 0 owns `[0,5)`, rank 1 owns no
positions, and rank 2 owns `[5,13)`. Position 5 therefore belongs to rank 2. This physical
interpretation differs from the inclusive occupied child bounds above: a zero-size
physical slot owns nothing, while a populated child with equal minimum and maximum can
contain a key.

Assigned ranks remain stable. A **tombstone** marks a retired rank without changing its
size or later starts. Live and retired slots can both have size zero; setting a size to
zero does not retire its rank. StreamPack supplies byte-value packing and transfer
when the owner wants to reclaim or reorder the corresponding payload.

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

Prepare an immutable `sidecar::layout` from `description`. Its `family` chooses
`geometry::absolute_ends`, `sizes` or `relative_ends`; Sizes remains the default.
The old names `direct` and `width` are aliases for the first two values, not extra
formats.

The schema specifies two independent bounds: per-slot size maximum **M** (`maximum`)
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

## Physical ownership and mutation

### Bind storage before reading or writing it

A sidecar view borrows three disjoint byte windows. They may be spans of one owner
allocation. The layout reports their required extents and alignment. Mutation calls
also take an effects sink that reports source-qualified byte writes to the owner;
the effect plane number identifies the window. Effects do not themselves publish a
transaction or protect readers.

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

### Read locations or physical owners

| Operation | Result |
| --- | --- |
| `locate(source,rank)` | `{offset,size}` for a live rank; invalid or retired ranks are errors. A live zero-size slot succeeds. |
| `reverse(source,point)` | Optional `position_owner{rank,tombstone}` for a point below extent; otherwise no result. |
| `sidecar::overlap(source,low,high,only_live)` | A half-open rank window covering positive physical reservations overlapping `[low,high)`. Retired reservations participate by default. |
| `range(source,first,count,sink)` | Live ranks and their locations, after checking the complete rank interval before callbacks. |

Physical reverse lookup finds the first exclusive end **> point**. It includes retired
storage and never assigns a point to a zero-size slot. It deliberately does not skip
tombstones as eligible endpoint search does. Physical overlap can contain internal
zero-size slots. With `only_live=true`, it trims retired/zero-size edge ranks but can
still contain those holes internally. An arbitrary occupied-maxima array alone does
not establish these contiguous intervals.

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

Geometry edits move no payload or children. The owner coordinates the corresponding
data change and publication. StreamPack does not expose them as uncoordinated value
edits. Inserting, removing or reordering middle ranks instead constructs a new
representation with an explicit rank remap; StreamPack owns its byte-value packing and
selected transfer.

## Composition and retained versions

### Keep ready geometry work with its consumer

A typed access exposes `start`, `slot_size`, physical `reverse`, carried traversal
and native group decoding. Dispatch once around the complete consumer rather than once
per rank. A sequential traversal carries its position; a native `read16` lets a fused
consumer use offsets, sizes and a live mask without an obligatory intermediate array.
Read callbacks cannot mutate or rebind the source.

The admitted sequential encoder carries new geometry while a consumer chooses movement
and ordering. An in-place rewrite must capture original boundaries before overwriting
them. StreamPack composes the shared original-geometry walker and encoder for packing.

### Retain read dependencies and report writes separately

Read-projection visitors describe an operation's metadata dependencies:
`visit_start_metadata` reports the start path, `visit_metadata` adds size/liveness,
and StreamPack's `visit_value_metadata` adds its independent used-length field.
These are point-read projections, not complete reverse/search footprints. A typed
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
eligibility, backing and derived search state. Population-aware completion also borrows
the original named source; retaining a temporary typed dispatch object is insufficient.

## Portable descriptor

The portable descriptor is eight bytes. New standalone **version 4** uses byte 0: bits
5–7=`4`, bits 0–1=encoding 0/1/2, bit 2=reserved zero, bit 3=immortal, bit
4=fixed-size-present. Bytes 1–2 hold M, bytes 3–5 hold B and bytes 6–7 hold fixed size
(zero when absent). Version 3 decoding preserves its original two-encoding carrier laws
and re-encodes that version. Prepared objects and pointers are not portable descriptors.
Conversion constructs a new representation; changing a tag does not convert its bytes.

For byte values, [StreamPack](../streampack/reference.md) adds optional used lengths,
value validation, replacement, packing and transfer without another geometry binding or
per-rank indirect call. [Owner integration](../integration.md) covers Loom/Orbital
visibility, retention and effects. Performance evidence and algorithm selection live
with the [Boundlist study](../../../workbench/spikes/boundlist/README.md).
