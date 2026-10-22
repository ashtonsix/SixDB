# Intervals

Intervals supplies borrowed ordered search and half-open interval geometry. Link
`ikea::intervals` and include `<ikea/intervals.h>`. It owns no allocation, compact
header, bitmap, retirement policy, payload or publication protocol.

Ordered search needs `size()` returning `unsigned` and ordered `at(rank)` values.
The absent sentinel is `size()`. `lower_bound` finds the first endpoint at least
the query; `upper_bound` finds the first strictly greater endpoint. Comparisons
can be logical objects such as arbitrary-width key references with `operator<`;
no numeric conversion, origin, equality operator or interval arithmetic is needed.
Ordinary synchronous searches may receive a temporary borrowed projection.

```cpp
ikea::intervals::direct_source source{ends.data(), unsigned(ends.size())};
const unsigned lower = ikea::intervals::lower_bound(source, query);
const unsigned upper = ikea::intervals::upper_bound(source, query);
```

Interval geometry additionally supplies `origin()` and ordered exclusive ends.
`from_ends(source, origin)` and `from_cuts(source)` adapt borrowed ordered values.
`locate` returns one half-open interval, `reverse` returns the containing rank,
`overlap` selects intersecting positive-width intervals, and `range` carries a
position through a run. Duplicate/zero-width intervals retain their ranks and
are visited by rank traversal, but own no points. Generic source counts and
coordinate domains are independent of compact StreamPack's 256-slot layout.

A source can provide typed `start`, `slot_size`, `owner_rank`, carried traversal
and prepared searches to avoid redundant work. `with_geometry(source, body)`
resolves representation once around complete owner work. Keep the stronger
StreamPack value body or tiny Frame projection inside that body; no generic
intermediate interval or decoded packet is required. `start` is checked;
`start_unchecked` requires prior owner admission.

## Representations and read grants

`direct_source` borrows native or logical endpoints. `delta_source` and
`relative_end_source` borrow G16 local fields plus checkpoints; stride admits
interleaved companions. `sparse_sizes<G,...>` and `sparse_ends<G,...>` separate
checkpoint spacing from native execution width. Width sums and global origins
must fit their declared carriers; running sparse coordinates are at least u32.
All initialized native tail lanes are readable, and sparse size tails are zero.
An optional `CheckpointCapacity` grants actual readable checkpoint lanes, while
logical count excludes inactive entries. It is not an assigned count.

Serial sparse execution is the default. `source.for_independent_reads()` is an
explicit G64 prefix throughput option over the same bytes. It may read all four
admitted native windows and loses on dependent/clustered workloads and some
ranges. Its shorter tails fall back to exact native windows. Inverse keeps the
serial implementation. Use the [native qualification](../../../workbench/spikes/intervals/RAISE-BAR.md)
for the actual workload/ISA limits, including x86 u16's unprofitable fallback.

`run_end_source` borrows Frame's immutable u8 ends-minus-one. Its logical exclusive
ends are 1..256, including 256 without narrowing. Tiny sources keep a direct
comparison projection. This representation does not cover signed prediction,
full-width residual sums or delta exception reconstruction; Frame owns that algebra.
`fixed_source` represents admitted nonnegative arithmetic geometry without data.

## Selection and readiness

`rank_selection` borrows exactly ceil(count/8) bytes of eligibility bits; inverted
bits may express an owner's tombstones. `selected_source` attaches that filter,
or pass a filter to one interval operation. Excluding a physical point owner
returns absent, never a later owner. The filter allocates and mutates nothing.
NULL validity, presence, retirement and coarse block selection are independent.

Frame's one-bit-per-block selection must skip descriptor, geometry and body before
entering Intervals; empty whole groups must be skipped by the enclosing owner.
An operation-local rank filter inside a selected geometry is a different layer.

`prepare_lower_bound` / `prepare_upper_bound` require a named borrowed source.
Their phase exposes a useful possible `local_window()` and later `finish()`;
that window is not the complete protection/read footprint. Keep the source,
filter, interpretation and coherent retained backing stable through completion.
Different retained images/checkpoint choices may coexist. Scheduling, prefetch,
retention, publication and reclamation belong to the caller.

## Borrowed edits

`size_editor` borrows disjoint mutable fields, checkpoints, assigned-count and
extent controls. The owner admits placement, contents and exclusive authority.
Checked append/resize refuse before stores on invalid rank/value/extent/effects.
Each actual store issues damage on the original named editor before writing,
including tail initialization; hooks are infallible and cannot suspend or mutate
inputs. The editor owns no allocator, format, bitmap or publication. Resize
translates later endpoints; it is not a one-boundary-preserving edit.

See the [examples](../../examples/intervals) and [reference](reference.md).
