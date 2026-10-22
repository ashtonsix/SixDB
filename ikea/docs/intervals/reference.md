# Intervals reference

Intervals supplies borrowed ordered search and contiguous interval geometry.
The consumer owns storage, admission, eligibility meaning, associated values and
publication. Link `ikea::intervals`; ordinary calls and source types are available
from `<ikea/intervals.h>`. The [usage guide](usage.md) gives worked examples and
consumer choices. No compact descriptor, allocation or bitmap belongs to Intervals.

## Endpoints, cuts and ranks

Consecutive cuts `cut[r]` and `cut[r+1]` define rank r as
**`[cut[r],cut[r+1])`**: include the left cut and exclude the right.
An ordered list of n+1 cuts defines n intervals. `[10,15,16]` defines `[10,15)`
and `[15,16)`. Equal cuts define a zero-size interval, which owns no points.
Ranks remain stable when an external filter excludes them.

![Six adjacent intervals share their cuts. The query from 9 inclusive to 36 exclusive intersects ranks 1 through 4. Rank 2 can be excluded without extending a neighbor into its coordinates.](images/ranges.svg)

The source representation uses one **exclusive end per interval**, plus an origin
when geometry is needed. Source indices are interval ranks, including through
`from_cuts`. The last end is the extent; an empty geometry's extent is its origin.
The absent rank and empty-window sentinel use the source's count n.

## Two source contracts

| Source capability | Required members and laws |
| --- | --- |
| Ordered endpoint search | `size()` returning `unsigned`, up to `maximum_count == UINT_MAX`; `at(rank)` ordered nondecreasing for assigned ranks. |
| Interval geometry | The endpoint contract plus `origin()`; every end is at least the origin. Optional `extent()` must equal the last end or empty origin. |

Ordering is sufficient. Logical values need the comparisons used by the query:
lower bound uses `!(value < query)`, upper bound uses `query < value`.
No equality operator, numeric conversion or subtraction is required. Integral
comparisons preserve mixed signedness and width. The owner admits ordering,
initialized/readable elements, coherent interpretation and lifetime.

Sources are lightweight projections, not owners. Ordinary synchronous calls may
receive a temporary projection because it need survive only the call. Adapters
copy their source controls while borrowing backing; renew after changing copied
origin/count/placement. Retained work has the additional lifetime rules below.

| Adapter | Interpretation |
| --- | --- |
| `direct_source<T>{data,count}` | Ordered endpoint array. Native unsigned loads can be unaligned; other T elements are actual live objects. |
| `from_ends(ends,origin)` | Copies an endpoint projection and adds the initial cut. Origin has its endpoint type. |
| `from_cuts(span)` | Requires at least one cut, even for empty geometry; copies origin/count and borrows the remaining ends. |
| Custom source | Projects the required members directly, including arbitrary-width logical keys or decoder output. |

There is no common 256-rank cap. That is a StreamPack image limit and a Frame
run-domain choice, not a restriction on Directory's ordered sources.

## Search and geometry operations

| Operation | Result and checks |
| --- | --- |
| `lower_bound(source,q)` | First eligible endpoint >= q, or n. |
| `upper_bound(source,q)` | First eligible endpoint > q, or n. |
| `locate(source,rank[,filter])` | `optional<interval<Point>>{first,limit}`; absent for invalid/excluded rank. Zero-size ranks succeed. |
| `start(source,rank[,filter])` | Optional start only, with rank/selection checks. |
| `start_unchecked(source,rank)` | Start without rank/selection checks; admitted typed composition only. |
| `reverse(source,q[,filter])` | Physical containing rank, or n outside `[origin,extent)` or when its owner is excluded. |
| `overlap(source,low,high[,filter])` | `rank_window{first,limit}` bounding positive-size intervals intersecting `[low,high)`; empty is `{n,n}`. |
| `range(source,first,count,sink[,filter])` | `expected<void,error>`; visits eligible ranks after preflighting the complete rank interval. Invalid input returns `error::rank` without callbacks. |
| `with_geometry(source,body)` | Invokes a complete typed body, dispatching representation once where the source supports it. |

For `[3,7,7,10]`, lower bound of 7 is 1 and upper bound is 3. For cuts
`[0,5,5,13]`, point 5 belongs to rank 2, and point 13 is outside. Geometry
intersects a query when `end > low && start < high`. Empty/reversed queries return
empty; a query can extend beyond either end. Internal zero-size or excluded ranks
can remain in an overlap window. Traversal applies the same selection and visits
eligible zero-size ranks too; it does not rerun a positive-intersection predicate
for every internal rank.

Geometry does not redirect excluded point ownership. A selected endpoint search
may instead return a later eligible endpoint: these are different questions.
Independent occupied minima/maxima, lossy prefixes and duplicate-fence correction
belong to the consumer's routing policy.

Optional typed `start`, `slot_size`, `owner_rank`, `visit_intervals`,
`with_geometry`, `boundary<E>` and `prepare<E>` hooks retain stronger paths.
They must implement the same semantics on admitted input. `owner_rank` is called
only after origin/extent checks; `visit_intervals` receives a validated rank run.
Read callbacks cannot mutate or rebind the source. No universal decoded packet or
flattened compact descriptor is required.

## External rank selection

`rank_selection{bytes,inverted=false}` borrows a byte-readable bitmap. A nonempty
bitmap supplies at least `ceil(n/8)` bytes. Set bits mean eligible, or ineligible
when inverted. No word padding is required. Unused high bits are ignored.
`rank_selection{}` selects none; `rank_selection::all()` selects all.

`selected_source{source,filter}` copies the source projection and intersects the
filter with any existing selection. Nesting cannot re-enable a rank. It supports
ordinary endpoint and geometry operations, typed dispatch and prepared completion.
Operation-local geometry overloads accept the filter as their last argument.
An empty operation-local range filter returns after rank validation without
entering geometry. Borrowed filter storage remains coherent with its rank image.

Intervals does not allocate or update selection, expose retirement, or interpret
NULL/presence. An owner can invert tombstones into eligibility while keeping the
bitmap and retirement policy itself. Frame's coarse block filter must skip an
excluded block before descriptor/geometry/body access; an outer empty group must
skip its contents too. That mask is not an Intervals entry-rank selection.

## Checkpointed representations

The same boundary arithmetic can be supplied by absolute ends, sizes plus region
origins, or region-relative ends. These are source interpretations; Intervals does
not assign a persistent tag or byte schema.

![Absolute ends, sizes and relative ends encode the same excerpt. A checkpoint at rank 16 is 34. All three locate rank 18 at start 37 and end 42.](images/encodings.svg)

Let G be checkpoint spacing and `C[g]` the absolute start of region g.
Sizes store `W[r]`; an end is `C[r/G] + sum(W[r/G*G..r])`.
Relative ends store `L[r]`; an end is `C[r/G] + L[r]`.
Relative ends reset at each region, so the raw array is not globally ordered.
Each representation has `ceil(n/G)` meaningful checkpoints, including the first
origin, and **no terminal checkpoint**. The supplied extent is the final end.
Zero-size regions can produce equal checkpoints.

| Source | Aggregate members, in order | Domain |
| --- | --- | --- |
| `delta_source<W,P>` | `widths, checkpoints, count, stride, extent` | G16; W is u8/u16, P unsigned. |
| `relative_end_source<L,P>` | `ends, checkpoints, count, stride, extent` | G16; L and P unsigned. |
| `sparse_sizes<G,W,P,CP=0,Independent=false>` | `widths, checkpoints, count, first, limit, maximum=max(W), stride=16` | G in 16..256, a multiple of 16; W u8/u16. |
| `sparse_ends<G,L,P,CP=0>` | `ends, checkpoints, count, first, limit` | Same G; L and P unsigned; contiguous local ends. |

The G16 sources expose endpoints, and can be wrapped with `from_ends` for
geometry. The sparse sources expose geometry directly, including start-only,
size, owner and carried traversal. `maximum` is an admitted bound on every width,
not a dynamic clamp. The first checkpoint equals `first`; `limit` equals the
final end (or first for empty geometry).

### Readable backing and carriers

| Storage | Required grant |
| --- | --- |
| Direct endpoint array | n initialized elements; exact partial tails are supported. |
| G16 widths or relative ends | 16 initialized/readable fields per assigned native group, including a partial final group. Unassigned local tail fields are zero. |
| Sparse sizes | `ceil(n/16)` initialized/readable native windows; unassigned final size lanes zero. `stride >= 16` in W elements separates native windows and may cross sibling fields. |
| Sparse relative ends | Contiguous initialized/readable local ends through the final native window, with zero inactive tail. |
| Checkpoints with CP=0 | The assigned `ceil(n/G)` initialized elements; no extra vector-tail grant is implied. |
| Sparse checkpoints with CP>0 | At least CP initialized/readable elements and assigned checkpoint count <= CP. Inactive lanes remain logically excluded. |

Native loads must stay within these grants. A contiguous hint window over strided
fields spans intervening siblings; the enclosing byte span must also be readable.
Initialized does not mean assigned, eligible or writable. Writes additionally
need exclusive authority over every issued byte.

G16 delta sums and absolute coordinates must fit P; relative local spans must fit
L and global coordinates P. Sparse running coordinates use
`common_type_t<P,uint32_t>` to avoid narrow prefix arithmetic, while stored
checkpoints/origin/extent must still fit P. Owners prove arithmetic cannot wrap;
unsigned storage alone does not establish that proof. Native unsigned loads are
byte-based, so alignment is not an extra read requirement. Mutable editor spans
below refer to properly aligned W/P objects.

Increasing G can save checkpoint bytes and writes while increasing point prefix
work or widening local ends. CP is a **read grant**, not assigned count or a
promise that additional checkpoints have logical meaning. StreamPack's G16 image
retains its own full-directory and initialized-tail laws in
[its reference](../streampack/reference.md); sparse sources do not silently change
that persisted format.

![Checkpoint spacing changes directory bytes and prefix work while the native read width remains 16 fields.](images/sparse.svg)

### Explicit G64 prefix execution

`sparse_sizes::for_independent_reads()` returns the same borrowed storage/control
projection with its independent prefix policy enabled. For G64 with all four
native windows admitted, supported native bodies mask/combine their work before
one reduction. Shorter tails keep exact-window execution. Inverse/endpoint routing
continues through the serial implementation. Other spacings receive no new kernel.

Serial is the default. This policy is qualified for random independent rank work
on the measured u8 native profiles and V2 u16. It loses important dependent,
clustered and range cases; x86 u16 keeps a serial fallback and does not earn a
recommendation. It adds no stored sidecar or checkpoint. Spacing and execution
policy are chosen independently by the owner; the
[performance findings](../../../workbench/spikes/intervals/RAISE-BAR.md) state
actual workloads, timings, instruction/resource analysis and counterexamples.

## Immutable projections

`run_end_source{ends_minus_one,count}` borrows nondecreasing u8 fields and returns
`unsigned(field)+1`: logical exclusive ends 1..256, origin 0. It needs count
initialized bytes and no compact control, bitmap or checkpoint. Zero-size ranks
can be represented by equal ends, except that the first logical end is at least 1.
Native-byte search translates integral queries before comparing; it preserves
negative, zero, 256 and wider out-of-domain cases. Counts below 16 retain the
comparison projection; `with_geometry` resolves that tiny projection outside a
complete body. The common API does not require Frame to replace its own stronger
raw projection. Whole-getter qualification earns native-byte search for curated
unpredictable point programs at16+ runs; keep the predictable raw program and
tiny fallback. Signed previous-value predictions, full-width residual prefixes
and delta exception reconstruction remain Frame algebra.

`fixed_source<P>{count,first,step}` represents nonnegative arithmetic geometry
without a boundary plane. The owner proves all coordinates fit P without wrapping.
A zero step gives only zero-size ranks, so checked point ownership never divides
by zero. The same search and geometry operations apply.

## Prepared execution and retained versions

`prepare_lower_bound(named_source,q)` and `prepare_upper_bound(named_source,q)`
return a phase with `has_local_read()`, `local_window()` and `finish()`. Rvalue
sources are rejected. The public phase retains a pointer to the named source;
keep its address, count, selection, interpretation and backing coherent until
finish. Ready work may complete entirely during preparation. Otherwise, the
window describes a useful next read; finish returns the ordinary eligible rank.

![Checkpoint and numeric preparation expose useful local work; a caller may prefetch or wait while retaining the complete coherent version.](images/phases.svg)

For G16, the local hint covers 16 fields. Sparse sizes can expose up to G fields,
including intervening stride bytes; relative sources expose their admitted local
region. None is the complete protection footprint: checkpoint routing has already
read metadata, eligibility can still be read, and a numeric prediction can repair
outside its bracket. A point-read visitor from another owner is not a universal
search grant.

`<ikea/intervals/author/search.h>` also exposes prepared numeric bindings,
`scan_source<T>` and independent `lower_bounds` / `upper_bounds` arrays. Numeric
binding preparation lives in `src/intervals/search.cpp`; derived state is stale
after endpoint, count or placement changes. The advanced author's unselected raw
phases capture their routing/backing projection; selection-aware phases borrow
the original named source. This permits a raw phase to outlive a temporary typed
access while its owning backing remains retained. It does not waive selection's
source lifetime, or the public phase's named-source contract.

Prepared state supplies execution choices, not a universal numeric ordering or
schema. Intervals does not schedule, wait, retain an image, acquire authority,
publish or reclaim storage. Different checkpoint layouts and retained versions
can coexist while their owner keeps each operation's dependencies coherent.

## Borrowed checked edits

`size_editor<G,W,P>{fields,checkpoints,assigned,limit,origin,extent_limit,maximum}`
borrows writable contiguous W/P spans and `unsigned& assigned`, `P& limit`.
G is a multiple of 16 in 16..256, W is u8/u16, P unsigned.
The editor, spans, controls, input/effect storage and writable outputs must satisfy
owner-proved disjointness and lifetime rules. Keep the original named editor
stable until its effects are resolved. Its byte effect planes are:

| Plane | Byte coordinates |
| --- | --- |
| 0 | Size fields, including initialized native tails. |
| 1 | Checkpoints. |
| 2 | Assigned-count object. |
| 3 | Extent object. |

`capacity()` is the fields span rounded down to complete 16-field windows, capped
at `maximum_count`. Provide enough checkpoints for that capacity.
`validate()` checks capacity, maximum/carrier and extent bounds, assigned fields,
meaningful checkpoints, final extent and zero initialized native tail. It does
not admit pointer aliasing or write authority. New empty controls use assigned=0
and limit=origin. There is no allocating constructor or blanket initialize call.

| Command | Behavior |
| --- | --- |
| `source()` | Snapshot projection over current count/extent and borrowed fields; renew after edits to copied controls. |
| `append(value,effects)` | Assigns the next rank; zeroes a newly readable 16-field window and writes a new checkpoint when needed. |
| `resize(rank,value,effects)` | Replaces one size and translates later region checkpoints and final extent. Equal size is a successful no-op. |

Commands require admitted contents and exclusive authority. Checked rank/value,
capacity/extent and effect-capacity refusal happens before stores and preserves
bytes/effect output. Every issued store calls `effects.before(*this,byte_write)`
on the **original editor** before writing, including tail initialization. The
sink supplies `remaining()`; hooks cannot fail, allocate, suspend or mutate inputs.
Preserving stores still count as damage. Effects are neither beforeimages, reader
protection nor a transaction.

Resize is not a single-boundary edit: ends `[5,9,12]` become `[6,10,13]` when the
first size grows by 1. Holding later ends fixed would instead produce `[6,9,12]`
and is not an editor operation. The owner coordinates associated data movement,
rank remaps for reordering, publication and retained readers.

## Ownership, execution profiles and verification

Intervals depends on neither StreamPack nor Frame. StreamPack owns its portable
compact schema, legacy/current descriptor decoding, bitmap, retirement and value
operations; Directory and Frame keep their own representations. Private compact
controls are tested under StreamPack, not exposed as another public geometry
owner. The old Boundlist public headers, target and examples are removed.
Historical research and source recovery remain in the
[earlier study](../../../workbench/spikes/boundlist/README.md).

Explicit native kernels are selected for AArch64 NEON and x86 AVX2/AVX-512 by the
build profile. Generic ordered sources remain valid without those routes. Use
separate build directories and run binaries only on compatible CPUs; see
[Building](../../../BUILDING.md). No runtime CPU dispatch is implied.

[Maintained checks](../../test/intervals/README.md) and standalone examples cover
the contracts; StreamPack tests cover its image/value laws and owner effects.
The [graduation record](../../../workbench/spikes/intervals/GRADUATION.md),
[native performance study](../../../workbench/spikes/intervals/RAISE-BAR.md) and
[live caller audit](../../../workbench/spikes/intervals/AUDIT.md) distinguish
correctness, actual complete-consumer timings, exact instruction comparisons and
microarchitecture models. Model throughput and check runtime are not caller latency.
