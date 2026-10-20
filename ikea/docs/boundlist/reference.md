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

Namespace `boundlist::sidecar` owns a curated reservation-geometry schema,
views and typed kernels. It has no U, blob-value packet or storage-compaction
policy. StreamPack composes this geometry with its own optional used-length law.
Generic ordered sources retain their wider coordinate/count domains.

### Stored representation

`description` contains `family`, `maximum`, `extent_limit`, optional
`fixed_reservation`, and `immortal`. Capacity is **256**; group width is **16**.
The total limit is at most 256 × maximum and defaults to that product. Fixed
reservation cannot exceed maximum. A retired zero-width slot retains its rank.

Fields are little endian. W is one byte for a reservation upper bound <=255,
otherwise two; a fixed reservation supplies the tighter bound. O is one/two/four
bytes for total limit <=255/<=65535/larger. The optional D bitmap occupies 32 B:
bit r%8 of byte r/8 is one for retirement.

| Window / effect plane | Stored fields |
| --- | --- |
| Directory / 0 | D first, then 256 exclusive endpoints (Direct) or sixteen group-start checkpoints (Width), each O bytes. Fixed/zero geometry omits these fields. |
| Lengths / 1 | G16 reservation widths for variable Width; no fields for Direct/fixed geometry. |
| Control / 2 | Assigned count u16; variable Width also stores u32 extent at byte 2. Direct/fixed derive extent. |

Directory storage is full capacity. `lengths_bytes(assigned)` includes complete
initialized G16 groups. Inactive widths in the last assigned group are zero;
inactive endpoints/checkpoints are initialized but have no semantic value.
Lengths require W-byte alignment; directory/control use bounded byte loads.
Supplied windows are disjoint but may share an allocation or owner Part.

A checkpoint is the sum of reservations preceding its group, including the
origin and omitting a terminal sentinel. Extent is the sum of all assigned R,
retired or live. The total limit is a schema encoding bound independent of the
current allocation's size. The schema has no placement switch.

`layout::encode()` produces an eight-byte **version-3** geometry description:

| Bytes | Meaning |
| --- | --- |
| 0 | Version 3 in bits 5–7; Width bit 0, immortal bit 3, fixed reservation present bit 4. Bits 1–2 are zero. |
| 1–2 | Maximum reservation, u16. |
| 3–5 | Total limit, u24; at most 16,776,960. |
| 6–7 | Fixed reservation, u16; zero when absent. Presence distinguishes fixed zero. |

Version-2 StreamPack descriptions are not accepted as geometry-only schemas.
StreamPack retains its separate version-2 encoding and existing physical recipes.
Persist portable schema bytes and corresponding windows coherently, never the
native prepared layout or its borrowed pointers.

### Admission, mutation and composition

`view::bind` checks sizes, alignment, overflow and window disjointness. Initialize
new storage or call `validate` for recovered bytes. Validation checks count,
end/checkpoint laws, reservation bounds, extent and zero Width tails. It grants
neither ownership nor a selected version. Checked commands preserve admitted
contents; they do not recover malformed input.

| Operation | Meaning |
| --- | --- |
| `locate(source, rank)` | Live rank's offset and reservation; rejects invalid/retired rank. |
| `range(source, first, count, sink)` | Carries geometry through live ranks; sink receives rank and geometry location. |
| `reverse(source, offset)` | Physical owner and tombstone below extent; includes retired reservations. |
| `overlap(source, low, high, only_live)` | Half-open positive reservation overlap; internal zero/dead holes can remain. |
| `initialize(dst, effects)` | Empty count/control and full initialized directory. |
| `append(dst, reservation, effects)` | Adds the next rank, initializes a new R group when needed and refreshes count/extent. |
| `retire(dst, rank, effects)` | Sets a tombstone without moving boundaries. Requires D. |

There is no U-only `replace`, `soft_pack` or selected `transfer` operation in
Boundlist. Owners choose destination geometry and coordinate their data.
StreamPack supplies value-aware packing/transfer and complete acceptance checks.

Author `write_origin_unchecked`, `write_width_unchecked`, `write_slot_unchecked`,
encode admitted caller choices for variable geometry. The caller proves it is
not fixed before entering these field-store kernels. `publish_size_unchecked`
and `finish_rewrite_unchecked` refresh the applicable count/extent controls.
A slot rewrite receives the chosen start and reservation; the caller
maintains coherent successor boundaries and total bounds. The `covered`
argument means the R store is already covered by an issued byte range
in this same operation. It never grants access to sibling consumer fields.

Typed geometry access can compose with an enclosing source whose immutable
format supplies strided R. It contains no used-length dependency. Native packets
carry offsets/R; StreamPack owns its U-bearing value packet and scalar handoff.
`slots(first, count, begin, sink)` traverses all admitted assigned ranks,
including retired/zero reservations. The caller proves `begin` equals the
original start of `first`. The sink receives rank, original offset and R and may
rewrite already-read geometry; it must preserve format, count and unread fields.
Fixed/Direct/Width traversal is selected outside the loop. `range` adds live
selection; StreamPack composes its value projection and packing in that same walk.

`visit_start_metadata` and `visit_liveness_metadata` expose separate read
projections. `visit_metadata` combines geometric location and liveness reads.
StreamPack's `visit_value_metadata` omits the current Direct end when start+U
suffice. Each describes actual possible reads, not ownership or readiness.

Width typed search/reverse may read all sixteen initialized checkpoints,
including inactive ones; results mask inactive ranks. Protect that window plus
selected R and population reads. Generic `width_source` promises only assigned
checkpoints. A prepared local window is a prefetch target, not a complete grant.

Mutations require exclusive authority over actual issued stores, including
preserved neighbours and complete-group initialization. Effects use the original
named source with plane 0 directory, 1 lengths, 2 control. They describe writes,
not read protection or publication. Temporary writable destinations are rejected;
format/view/effect controls must not alias outputs. Hooks are infallible and cannot
allocate, suspend or invalidate inputs. Checked refusal preserves bytes/effects.
