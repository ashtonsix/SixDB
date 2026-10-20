# Promote the bounds layer, preserve the fast storage paths

This records the closed prototype study. Subsequent authorized production work
and qualification are tracked in [IMPLEMENTATION.md](IMPLEMENTATION.md) and
[GRADUATION.md](GRADUATION.md).

Create **`ikea::boundlist`** from StreamPack's sidecar machinery. Keep the existing
Direct and Width formats and their compact typed access bodies. Add ordered
boundary search and first/last overlapping-rank operations that can work without
a StreamPack payload. This is an extraction with useful new search/composition
capabilities; it does not need a new universal encoding.

## The small surface to retain

- Ordered lower/upper boundary search: first eligible endpoint `>= point` or
  `> point`, returning the assigned count on absence. Query preparation must
  preserve order outside a narrow stored domain rather than silently narrow.
- Range overlap returning a half-open rank window. State the endpoint policy
  and whether it is an exact interval/extrema result or a candidate window.
  Holes inside that window remain holes. Empty and reversed queries are empty.
- Typed location, carried traversal and selected/native-group access, alongside
  the existing initialize/append/edit/retire/rebuild/transfer capabilities.
  Preserve StreamPack's admission, aliasing and source-effect contracts while
  moving their metadata implementation below StreamPack.

Keep physical byte ownership separate from live-successor search. StreamPack
reverse lookup still includes retired reservations and unused reservation slack.
Used length, reservation width and population are distinct facts. A live value
can have zero used bytes; an occupied keyspace segment can have equal extrema.

For unique keys with exact inclusive occupied maxima, point routing needs one
`>=` search followed by a local child lookup. Do not make this common path pay
for duplicate-run discovery. Lossy prefixes require owner-level correction;
uniqueness of full keys alone does not admit the exact-maxima fast path.

## Storage and composition

Retain StreamPack's version-2 byte law, 256-slot capacity and group-of-16 Width
checkpoints for its compatibility path. Do not impose those limits on generic
boundlist sources. The prototype checks ordered multiword and string domains,
including a virtual upper fence beyond the last ordinary integer point.
Arithmetic width encodings and lerp need stronger domain capabilities than
comparison alone.

Bounds, optional used lengths and optional liveness can have separate semantic
owners while sharing an allocation, interleaved groups or readable tails.
Omitting U and fixed/full/immortal cases remain useful. StreamPack owns payload
meaning; boundlist need not acquire it. Keep SeriesPack/TuplePack composition
available where the encoding pays, without selecting packed endpoints as the
default from byte counts alone.

Move shared kernels and metadata description/admission beneath StreamPack:
`ikea/boundlist.h`, `ikea/boundlist/{description,view,author,detail}` and an
`ikea::boundlist` CMake target. StreamPack depends on that target, never the
reverse. Do not copy the prototype's reverse include dependency, raw full-word
population grant, public experiment enum, or seven-word flattened adapter.
Preserving the current compact access shape matters: complete-reader results
show some modest regressions through that adapter.

## Search and execution decisions

Use conditional-select bisection followed by a native SIMD window as the general
numeric Direct search body. Retain explicit full-count SIMD scans for small,
known-size hot sources where the platform preset benefits; retain interleaved
independent searches for throughput. Resolve execution choice when binding a
consumer, rather than adding a per-call algorithm argument. Do not promise one
universal scan cutoff across endpoint widths, CPUs and latency/throughput goals.

For Width, preserve the existing narrow local prefix/owner kernels and group
checkpoints. Expose checkpoint selection and the qualified local read window
through typed composition. Keep ordinary hot consumers inline; the caller can
use existing CPS/Loom facilities between unresolved header, local-window and
child dependencies. The measured cold benefit comes from this staging and
bounded read footprint, rather than from a new scheduler inside boundlist.

Do **not** adopt the tested one-lerp/shotgun combination, generic widened paired
Width prefix, whole-array SIMD scan at large N, or paid Direct checkpoint guide
as universal defaults. The guide is a useful control, but adds storage and
maintenance; Width already stores checkpoints. Hint-driven galloping and more
sophisticated bounded interpolation remain unselected, not disproved.

## Mutation, publication and migration

Keep caller allocation and Orbital publication outside the primitive. Damage
names the original source and all issued store ranges, including preserved
packed neighbours. Protection/read grants cover actual native loads and
initialized padding. A rank that is logically unassigned can still lie in a
retained reader's SIMD load: it is not automatically writable in place.

The actual Loom/Orbital histories qualify extraction with append, replacement,
retirement, packing, remaps and retained readers. They use full-copy versions;
they do not establish selective geometry sharing or a new MVCC cost advantage.
Publish bounds/count, sibling metadata and child/rank repairs coherently through
the owner. No new allocator, heat oracle or publication abstraction is needed.

During implementation, move the established bodies before switching consumers,
keep independent controls and wire fixtures, and qualify both standalone
boundlist headers and StreamPack regressions. Add executable examples for byte
reservations and ordered keyspace routing, including gaps and duplicate runs.
New mutable keyspace split/merge economics and complete directory layout belong
to Engine; this spike
has not measured a complete new mutable index.

The [findings](FINDINGS.md) supported this promotion. The subsequent production
implementation and qualification are recorded in [GRADUATION.md](GRADUATION.md).
