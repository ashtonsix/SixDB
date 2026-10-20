# Boundlist reference

`<ikea/boundlist/search.h>` provides ordered searches without linking a storage
implementation. `<ikea/boundlist.h>` adds the reservation sidecar. The
[usage guide](usage.md) explains operation and execution choices.

## Ordered sources

An admitted projection has `unsigned size()` and ordered endpoint values. The
fallback reads `at(unsigned rank)` for ranks below size. Ordering, initialized
bytes/objects and coherent lifetime belong to the owner. `maximum_count` is
`UINT_MAX`; count is also the absence sentinel. Wider outer directory indices
compose local sources rather than being silently narrowed into one source.

`lower_bound(source, q)` finds the first eligible rank whose endpoint is `>= q`;
`upper_bound` uses `> q`. They return count on absence. Optional
`next_eligible(first, limit)` and `previous_eligible(limit)` select population;
the latter returns size on absence. Endpoints remain ordered even in ineligible
ranks. No population is inferred from endpoint equality.

A custom source can provide `boundary<edge>(q)` as an already-fused geometric
search, or `prepare<edge>(q)` returning a phase with `finish()`. Those entries
return the same geometric boundary before population selection. Ordinary calls
prefer an available fused `boundary`; author preparation prefers `prepare`.
Share their classification and kernel laws while allowing immediate results to
return directly, rather than forcing ready phases through a local-read path.
The fallback needs only ordered
comparison and does not impose an integer encoding or metric. Native unsigned
integral queries use signedness-safe classification before conversion; custom
comparators must themselves preserve their domain's ordering.

`direct_source<T>{data, count}` borrows endpoints. Native unsigned integer loads
are unaligned-safe and stay inside the assigned array, including partial SIMD
tails. Other T require actual readable objects and their ordinary comparisons.

`width_source<W, Point>{widths, checkpoints, count, stride, extent}` admits:

- W is u8 or u16; Point is unsigned integral. `stride` is in W elements and
  addresses each G16 group, so interleaved sibling fields are possible.
- `ceil(count/16)` checkpoints include the origin and omit a terminal checkpoint.
  Each is the start position before that group's widths. Sums and extent fit
  Point; all checkpoints, widths and extent describe one coherent version.
- Each active group has sixteen initialized/readable W elements. Widths after
  assigned count in the last group are zero. Endpoint `r` is its checkpoint plus
  the inclusive width prefix. Extent is the final assigned endpoint, or origin
  for an empty source.

`population{bytes, inverted}` reads `ceil(count/8)` bytes. Bits outside count are
ignored. `populated_source` decorates an ordered source without changing its
endpoint encoding. `overlap(maxima, minima, query_range)` requires matching
counts and population, ordered occupied extrema and `min[r] <= max[r]` for each
eligible rank. Query closure controls the two boundary comparisons. It returns
`[first, limit)` after trimming eligibility at the edges, or `{size, size}`.
Internal holes and record-level gaps require owner checks.

## Authored search composition

`<ikea/boundlist/author/search.h>` supplies prepared searches, opt-in scan sources,
reusable numeric search bindings and interleaved native batches. These do not
allocate or schedule. Link `ikea::boundlist` for numeric source preparation.

`bind_search(direct_source<T>)` borrows one coherent source snapshot. For native
u16/u32/u64 it derives source-level search state; other ordered domains retain
their ordinary source. It preserves the source's size, endpoint and boundary
meaning. Derived state becomes stale after any endpoint, count or placement
change, even if the old pointer remains valid. The owner must rebuild it for a
new version. It neither validates ordering nor owns backing bytes.

Private applicability and bracket-size heuristics select checked interpolation
or ordinary native search. Every predicted interval is verified before narrowing;
neither sampled regularity nor a prediction is a correctness premise. Setup
reads a small sample of endpoints; per-query preparation is included in search.
The binding is 40 bytes for u16/u32 and 48 for u64 on supported 64-bit platforms.

`prepare_lower_bound(named_source, q)` and `prepare_upper_bound` return an object
with `has_local_read()`, `local_window()` and `finish()`. All borrowed completion
dependencies must remain stable and describe one retained version. A built-in Width phase
snapshots only its selected width pointer and scalar state; its size is 16 bytes
on supported 64-bit platforms. Built-in geometric phases do not borrow the source
object or query. Population composition additionally retains a named-source
pointer, requiring that source and its format to remain stable through finish.
A custom phase defines its own additional borrowing obligations. The author
factory conservatively rejects temporary sources.

For Width, the exposed window covers the selected sixteen widths, or is empty
for an already resolved result. A numeric binding's phase instead snapshots its
array pointer, query and local search interval (32 bytes on supported 64-bit
platforms). Its window is the proposed bracket or full array; bracket correction can
read elsewhere in the assigned array. An out-of-domain or extrema-resolved query
has an empty window. Neither kind of window describes all possible reads or
confers read permission. Preparation does not wait for residency. The owner must retain eligibility and any
other completion dependencies independently. A new coherent version requires
new preparation, even when old storage addresses survive.

`scan_source<T>` supports u16/u32/u64, requires count <=65535 at construction and
otherwise follows direct-source admission. `lower_bounds`/`upper_bounds` accept
matching fixed arrays of native unsigned `direct_source<T>` or the numeric
bindings returned by `bind_search`, and integral queries.
Each result has its own source's count sentinel. They interleave bisection and
SIMD convergence; they do not acquire or validate storage per probe.

## Reservation sidecar

Namespace `boundlist::sidecar` owns the reservation schema, views and typed
metadata access. StreamPack aliases the schema types; its own view adds payload,
and its typed access extends the same metadata body. The
[stored representation](#stored-representation) below defines the version-2
eight-byte schema, Direct/Width, G16, optional R/U/D and padding. The recipe has
bounded reservation coordinates; generic ordered sources do not inherit those
encoding bounds.

### Stored representation

`description` contains family, length placement, maximum reservation, block byte
limit, optional fixed reservation, full-reservation flag and immortal flag. Every
sidecar has capacity **256** (`slot_capacity`); capacity is not a schema choice. The
byte limit is at most 256 × maximum and can be zero. When omitted in aggregate
construction, it defaults to 256 × maximum. A fixed reservation cannot exceed
maximum. Values and reservations can both be zero; a retired zero-width slot still
has a rank and tombstone.

All multibyte fields are little endian. N = 256 and G = 16; let
W be one byte when the reservation upper bound is ≤255, otherwise two, and O be
one/two/four bytes for a block byte limit ≤255/≤65535/larger. Fixed reservations
supply the tighter bound for W. D occupies N/8 bytes when tombstones are present.
Within D, rank r is bit r%8 of byte r/8; one means retired. Ranks are 0–255;
count remains u16 to represent both empty and fully assigned blocks without a
sentinel. Checked rank arguments remain wide enough to reject out-of-range input.

| Window / effect plane | Stored fields |
| --- | --- |
| Directory / 0 | D first, followed by N exclusive endpoints (Direct) or ceil(N/G) group-start checkpoints (Width), each O bytes. No leading or terminal sentinel. Fixed/zero geometry omits these fields. |
| Lengths / 1 | Direct: U groups only. Width grouped: 16 R then optional 16 U, repeated. Width separated: full-capacity R plane then a U plane. Fixed geometry: U groups only. Omitted U has no storage. |
| Control / 2 | Assigned count as u16. Variable Width adds u32 occupied extent immediately at byte 2; Direct and fixed geometry derive extent. |

The directory is reserved at full capacity. `lengths_bytes(assigned)` includes
whole initialized groups, so prefix/native reads never overread a partial final
group. `lengths_bytes()` gives full capacity. Grouped Width requires its group-size
alignment (16/32/64 bytes); other recipes require W-byte alignment. Directory and
control use bounded byte loads and need no additional alignment. The API does not
require windows to be separately allocated or separately published Parts.
Reservations are positions in the owner's unit; StreamPack uses bytes.

When U is omitted, both Width placements store a single R stream, with no gap for
an absent U plane; the alignment rule above still applies. Placement does not
change Direct or fixed geometry. `byte_limit` is an encoding bound independent of
the supplied spans. It determines endpoint/checkpoint encoding and must not be
inferred from an allocation's current size.

Unassigned tail R/U fields in the last assigned group are zero. Unassigned
Direct endpoints and future checkpoints are initialized by construction but have
no semantic value. A checkpoint is the sum of reservations before its group.
Positive reservations are disjoint. Used lengths never exceed reservations.
Occupied extent is the sum of all assigned reservations, retired or live.

`layout::encode()` produces an eight-byte version-2 description:

| Byte offset | Encoding |
| --- | --- |
| 0 | Version 2 in bits 5–7; Width bit 0, separated lengths bit 1, full bit 2, immortal bit 3, fixed reservation present bit 4. |
| 1–2 | Maximum reservation, u16. |
| 3–5 | Block byte limit, u24. The largest legal value is 16,776,960 (256 × 65,535). |
| 6–7 | Fixed reservation, u16; must be zero when absent. Presence distinguishes fixed zero from variable geometry. |

There is no encoded capacity, reserved padding or fixed-width sentinel. Unknown
versions, invalid sizes, noncanonical absent-fixed bytes and invalid bounds are
rejected. The experimental 16-byte version-1 description is not accepted. The u24
field is cold schema storage; hot endpoint/checkpoint lanes still use native
one/two/four-byte loads. Native layout objects and pointers are never the wire
format. The owner persists schema identity and the three metadata windows coherently
with any corresponding data.

### Admission and operations

`storage<Byte>{directory, lengths, control}` contains three disjoint borrowed
spans. They may come from one owner allocation. `view::bind` checks placement;
`validate` checks complete assigned contents, checkpoint/end laws, lengths,
zero SIMD tails and count/extent. `assume_valid` requires the corresponding
owner proof. Source, immutable layout and control/effects objects cannot alias
writable storage. Live access/effects cannot outlive or move the named view.

Width checkpoint search/reverse through the typed sidecar can read the complete
sixteen-checkpoint geometry window, including inactive initialized storage. Its
read protection must cover that window, plus any selected width/population reads.
Inactive checkpoint comparisons are masked; their values do not select ranks.
Generic `width_source` reads only its assigned checkpoints and has no such extra
grant. `visit_value_metadata(rank, ...)` describes a value lookup, not the read
footprint of a search or reverse operation. Effects describe issued writes,
separately from either kind of read coverage.

| Command | Meaning |
| --- | --- |
| `locate(source, rank)` | Live rank's offset, U and R; errors for out-of-range/retired ranks. |
| `range(source, first, count, sink)` | Checked consecutive metadata traversal; the sink receives rank and location for live ranks. |
| `reverse(source, offset)` | Physical reservation owner and tombstone, including slack; absent at/beyond extent. Zero reservations own no bytes. |
| `overlap(source, low, high, only_live)` | Half-open physical reservation overlap, clipping at extent; excludes zero reservations at the edges. Internal zero/dead holes can remain. |
| `initialize(dst, effects)` | Initializes count/control and directory, with no assigned length groups. |
| `append(dst, U, R, effects)` | Adds a rank at extent, initializes its full G16 tail when needed, then publishes metadata effects. |
| `replace(dst, rank, U, effects)` | Changes U inside existing R; requires a live rank and the schema's full/fixed laws. |
| `retire(dst, rank, effects)` | Sets its tombstone; does not reclaim R or infer U. |
| `soft_pack(dst, shrink, effects)` | Preserves ranks, sets dead R to zero and optionally live R to U; owner separately moves corresponding data. |
| `transfer(src, selection, dst, shrink, effects)` | Preflights selected live metadata and rebuilds a destination in selected order; owner coordinates corresponding data. |

Checked refusal preserves bytes and effects. Admitted reads require validated
contents; checked mutations preserve those laws but do not recover malformed
input. Hooks/sinks are infallible and cannot suspend inside a bounded operation.
Ordinary mutation entries reject temporary destinations. Capacity/error rules and
metadata transfer selection use the same laws as StreamPack, without requiring
its payload window.

Effects are `byte_write{plane, offset, size}` qualified by the actual named source:
plane 0 directory, 1 lengths, 2 control. They cover issued stores, including
preserving neighbors and initialized padding, not only changed bits. They are
not publication or read protection. StreamPack maps these ordinals to its
planes 1, 2, 3 and retains plane 0 for payload. The original StreamPack source
identity survives; adapters do not publish effects on a transient projection.

Standalone metadata packing/transfer is useful only when the owner coordinates
its payload/child meaning. Allocation, retained-reader protection, version
selection, occupancy/heat and atomic publication remain owner responsibilities.
