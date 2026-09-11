# SeriesPack reference

This is the lookup reference for the current representation and API contracts.
The [SeriesPack introduction](../seriespack.md) teaches their use;
[extending SeriesPack](extending.md) covers implementation and composition work.

## Description and value domain

An array has one unsigned value width and preserves original index order.
Its encoded bytes contain no metadata header. The enclosing owner retains the
following facts separately from the storage:

| Fact | Meaning |
| --- | --- |
| `k`, `description::width` | Full value width in bits, 1..64; values are in `[0,2^k)` |
| `h`, `description::head_bits` | Leading bits stored in byte planes: 0, 8 or 16, with `h<=k` |
| `w=k-h` | Low payload width; zero means heads supply the entire value |
| `description::storage` | `local8` or a supported `striped` payload geometry |
| `description::version` | Encoded-layout version; currently `wire_version == 1` |
| `n` | Logical value count, independent of allocated capacity |
| Placement | Base, accessible byte extent and tile stride for each present stream |

Every u64 bit pattern is valid at `k=64`; range/mask arithmetic must handle that
case without shifting by 64. Width zero, NULLs, signed/float interpretation,
sortedness, uniqueness and record membership are supplied by other components.
Length may be zero or exceed `2^16`; SeriesPack has no Engine segment-size limit.

`format<K, Preset, H>` resolves a static description; `static_format<K, G, H>`
names one explicitly. A dynamic view carries a `description` at runtime.
`specialize<Format>` accepts only an exact description match and reuses the
view's placement admission. None of these operations re-encodes bytes.

The enum values are C++ choices, not stable serialized tags. Persist enough
metadata to recover the actual description. An identifier may resolve it through
a retained schema/format mapping; rerunning a current preset cannot recover an
older construction choice. Execution ISA, tuning
and point-reader strategy are separate from the encoded layout.

## Presets

Preset policy is provisional. `resolve` uses the following construction rules;
unlisted remaining widths use `local8`.

| Presets | Remaining widths selecting `striped` | Head requirement |
| --- | --- | --- |
| `compact` | None | Optional |
| `bulk` / `filter` | 1..7, 12, 20 | `filter` requires 8 or 16 head bits |
| `bulk_arm` / `filter_arm` | 1..7, 10, 12, 14, 15, 20 | `filter_arm` requires 8 or 16 head bits |

All choices obey `h<=k`. Explicit descriptions may select `striped` at any of
its supported remaining widths, on any supported target. ARM recipes do not
require an ARM reader. Presets select no allocation, stride or execution target,
and their names are not comparative performance claims.

## Placement and capacity

Let `T` be positions per physical tile and `B=T*w/8` its payload bytes. A placement
provides payload stride `S` and independent strides `S_j` for head planes.
For `n>0`, `m=ceil(n/T)` complete tiles provide initialized capacity `c=m*T`.
Arithmetic is checked in host-sized counts before addresses are formed.

| Stream | Tile start | Occupied bytes | Minimum address envelope |
| --- | --- | --- | --- |
| Payload, when `w>0` | `payload_base + t*S` | `m*B` | `(m-1)*S+B` |
| Head plane `j` | `head_base[j] + t*S_j` | `m*T` | `(m-1)*S_j+T` |

For `n=0`, all extents are zero. An absent stream has zero extent and is not
accessed. For nonempty arrays, each present payload stride must be at least `B`,
and each present head stride at least `T`. Striped payload bases and strides must be multiples of 32 bytes.
Local payloads and heads have no extra alignment requirement at attachment.
The common 64-byte-aligned dense placement is useful for the locality guarantees
below, rather than a requirement on every view.

`required_extents` computes occupied bytes, envelopes, tile count and capacity.
Gaps between tiles may hold foreign data. Stream envelopes may intersect if their
occupied tile ranges remain disjoint, including a head plane placed in payload
gaps. No load, store, clearing operation or write-effect record may claim those
foreign bytes. Allocator rounding grants no additional access permission.

All unused positions in the final tile encode zero. Full construction reads
only `n` input values and initializes this final slack without overreading the
input. Logical reads and writes remain bounded by `n`. Dense encoded storage,
including heads and slack, is `c*k/8` bytes; it need not be `ceil(n*k/8)`.
Additional reserved bytes do not become initialized values automatically.

## Encoded bytes

Bits in a byte are numbered from the least significant bit, starting at zero.
Whole-byte bodies use little-endian order. The following maps describe encoded
bytes independently of the scalar or SIMD implementation that accesses them.

### Heads

For a value `x`, head plane 0 stores `(x >> (k-8)) & 255` when `h>=8`.
With `h=16`, plane 1 stores `(x >> (k-16)) & 255`. Each head tile contains `T`
bytes in original-index order. Payload holds the low `w` bits. Reconstruct by
concatenating head 0, head 1 when present, and payload. At `w=0` no payload
pointer is read; all-head formats use the local geometry with `T=8`.

A head-only read returns that byte's domain, not the enclosing full value.
An equality match on a head is only a candidate full-value match. A head may
bound an unsigned value to an interval; signed or transformed domains need their
own justification for using such bounds.

### Local packets

Write `w=8*q+r`, with `0<=r<8`. Each tile has `T=8` and `B=w` bytes.
For tile-local position `i`:

| Part | Byte and bit location |
| --- | --- |
| Body byte `j`, `0<=j<q` | Byte `i*q+j` contains value bits `r+8*j .. r+8*j+7` |
| Residual bit `b`, `0<=b<r` | Bit `i` of byte `8*q+b` contains value bit `b` |

The body occupies the first `8*q` bytes and the residual bit planes the next
`r`. Reconstruct as `(body << r) | tail`. For `w=1..7`, only the planes exist;
for `r=0`, only the ordinary little-endian words exist. Unheaded widths
8/16/32/64 therefore have the same value bytes as their fixed-width integer
arrays. The introduction works through a [12-bit packet](../seriespack.md).

### Striped residuals

Supported remaining widths are 1..7, 10, 12, 14, 15 and 20. Let `r=w%8`,
`G=8/gcd(r,8)` and `C=r/gcd(r,8)`. A tile has `G` groups of 32 values, so
`T=32*G`; its residual occupies `C` stripes of 32 bytes. The same lane `l`
within each stripe contributes to position `32*g+l` of group `g`.

For residual widths 3 and 6, these maps give each value bit's position across
the stripe bytes at that lane. Position `p` means stripe `p/8`, bit `p%8`.

```text
r=3: group -> positions of value bits 0,1,2
0 [0,1,2]     1 [3,4,5]      2 [6,7,14]     3 [8,9,10]
4 [11,12,13]  5 [22,23,15]   6 [16,17,18]   7 [19,20,21]

r=6: group -> positions of value bits 0,1,2,3,4,5
0 [0,1,2,3,4,5]       1 [8,9,10,11,6,7]
2 [12,13,14,15,22,23] 3 [16,17,18,19,20,21]
```

For the other residual widths, set `s=g*r`, `a=s/8`, `u=s%8` and `room=8-u`.
If `r<=room`, value bit `b` goes to stripe `a`, bit `u+b`. Otherwise the low
`r-room` bits go to stripe `a+1`, starting at bit zero; the remaining high bits
go to stripe `a`, starting at `u`. This fixes the current high-fragment-first
5/7-bit wire, including values that cross a stripe-byte boundary.

Pure residual stripes occur in natural order at byte offsets `32*j`:

| `w` | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `T` | 256 | 128 | 256 | 64 | 256 | 128 | 256 |
| `B` | 32 | 32 | 96 | 32 | 160 | 96 | 224 |

### Striped bodies

For the wider striped layouts, `Xi` is the body data for original positions
`32*i..32*i+31`, with `q=w/8` little-endian bytes per value. Each `Xi` occupies
`32*q` bytes; each residual stripe `Yj` occupies 32 bytes. Chunks are adjacent
in the following physical order, with no internal padding:

| `w` | `T` | `B` | Chunk order |
| ---: | ---: | ---: | --- |
| 10 | 128 | 160 | `X0 X1 Y0 X2 X3` |
| 12 | 64 | 96 | `X0 X1 Y0` |
| 14 | 128 | 224 | `X0 Y0 X1 Y1 X2 Y2 X3` |
| 15 | 256 | 480 | `X0 Y0 X1 Y1 X2 Y2 X3 Y3 X4 Y4 X5 Y5 X6 Y6 X7` |
| 20 | 64 | 160 | `X0 Y0 X1` (64 bytes per `Xi`) |

Body bits remain above the low residual bits: `(body << r) | tail`.
The stripe bit maps are unchanged by their placement between body chunks.
A logical tail child can therefore occupy separated physical chunks; a single
contiguous span is not a general representation of that child.

## Admission and external bytes

`attach` validates the description, checked extents, alignment and overlap of
occupied ranges. It does not allocate storage, scan values, verify canonical
slack, retain an owner or synchronize access. `assume_valid` omits these checks;
its caller supplies the same facts. The owner must establish the actual encoding
and initialization before an operation accesses it.

Views and bound endpoints borrow storage. A read-only view aliases the same
bytes; it is not a snapshot. Composition expressions additionally refer to the
named source/view objects used by `describe`. Keep those objects and their bytes
valid during use; rebuild expressions after moving the source objects.

Treating supplied bytes as a description does not validate an external file or
schema. Deserialization must establish the retained description, permitted
extents and canonical representation. No general serialized metadata protocol
or canonical-byte validator is supplied by ordinary attachment.

## Checked and trusted operations

The public [operation declarations](../include/ikea/seriespack/operations.h)
separate checked calls from bound trusted methods and static trusted point calls.
Operation calls start from an admitted view; they do not repeat attachment.

| Operation | Value and coordinate contract |
| --- | --- |
| `get` / `set` | Original index in `[0,n)`; full unsigned `k`-bit value |
| `decode([begin,end), out)` | Position `i` goes to `out[i-begin]`; output carrier represents every possible `k`-bit value |
| `encode` | Construct all `n` values and zero final slack; input values fit `k` |
| Selected `write` | Position `i` takes `input[i-begin]`; input covers the full requested range, with only selected slots read/validated |
| `selection::bitmap(origin,count,words)` | Bit zero names `origin`; it is not a list of compacted ordinals |

Inputs may use a narrower unsigned carrier than `k`; wider carriers need a fit
check or a prior proof. Comparison constants may be any u64, including constants
outside the stored domain. Comparisons must handle them correctly without
truncation; there is no implicit u64 representation of `2^64`.

Checked operations finish rejecting validation before any destination or effect
output changes. Operation checks cover invalid ranges/selections, capacity,
value fit, overlap and unsupported execution. This rejection guarantee does not
provide concurrent-reader or crash atomicity. Inputs and selection remain stable
between validation and use. Empty checked ranges/selections make no value
accesses; validation may still reject invalid arguments.

Materialization requires disjoint input/output storage. Writes also keep effect
state/record storage and selection disjoint from the destination and any live
inputs they could overwrite. The owner supplies synchronization for the complete
physical footprint. Enclosing in-place transforms need a separate admission.

Binding selects compiled endpoints over an already admitted view; it does not
prove future call bounds, capacities or input values. Trusted methods reuse these
facts without rejection checks. SeriesPack allocates no input/output/effect
storage and does not suspend or publish changes inside these operations.

## Mutation and growth

Point writes change body/head bytes and the value's bits in shared residual
bytes. They preserve other values and foreign data. Different logical indices
may share physical write bytes, so disjoint-index updates are not necessarily
race-free. An enclosing owner must isolate the full footprint.

Optional `effect_output` appends `byte_span` records covering issued writes,
including read/modify/write bytes and slack initialization. Records can overlap,
repeat or be adjacent; coalescing and record count are not API promises. Capacity
helpers bound additional records beyond an existing prefix. Rejection preserves
that prefix and all bytes. Addresses borrow the destination's current residency;
they are not stable row identities, semantic old/new deltas or publication records.

Growth uses an unpublished larger logical view over initialized capacity. With
local packets, an array of length 10 can stage length 13 in its two initialized
tiles, write positions `[10,13)`, then publish the larger length externally.
New tiles must be initialized before they are used for read/modify/write.
The old view keeps its length, and shared tail bytes still require isolation.
If the owner abandons an append, it restores the former zero slack before reusing
the old canonical representation, or discards the private storage. Width changes,
shifting insertions/deletions, publication and rollback remain enclosing duties.

## Access and locality

Point reads require no materialized tile. Every kernel's access scope is a tile
or an explicitly admitted larger region.
It may read neighboring encoded values within that scope, including final
physical slack. It may not access an extra suffix, a foreign stride gap or another
owner's bytes. Partial materialized inputs/outputs need exact bounds regardless
of the physical tile's capacity. Overlapping machine accesses are legal when
their union is permitted. Read locality does not establish write locality.

Point payload access has a two-adjacent-64-byte-line bound. Local packets are
at most 64 bytes. Supported striped placements satisfy point and aligned
16-value locality with 32-byte tile alignment and a stride divisible by 32.
For dense local 16-value groups with a 64-byte-aligned run origin, the bound
holds exactly at positive widths `w<=32+gcd(w,32)`, plus the empty payload case.
Other strides require their own group-locality argument. Head traffic is counted
separately from this payload guarantee. The
[locality derivation](../../workbench/spikes/ikea-composition/probes/ikea-integers/locality/README.md)
explains why a width-56 group can span three lines while width 64 satisfies two.

## Native composition contract

An executor defines working lane width, participating lanes, original-index map
and admitted byte accesses. Tile executors use `tile*T+Begin+i`; dense/grouped
executors use `origin+i`. The generic vocabulary permits other compatible maps.
Joins preserve domains, coordinates and each actual source dependency; equal
format types or lane counts do not establish common ownership or compatibility.

Only active lanes promise logical values. Other bits must not affect results,
addresses or writes; harmless lane work may be masked away, but undefined
operations remain invalid. Physical capacity, participating lanes and selected
logical positions are distinct. Masks grant no read permission or fault
suppression. Dense/grouped `validate_region` admits complete logical regions
for the supplied source; substituted children need independent admission.

Native values and masks are ordinary owned C++ values, with no promise that they
stay in registers across arbitrary calls. `selected_sum` and `materialize` run
synchronously, including for empty masks. `materialize` ignores its sink's return
value; the sink preserves original output coordinates and writes only selected
positions. Drivers own traversal, skipping, early exit, progress and suspension.
Deferred sum partials denote one modulo-`2^64` result, rather than a set of rows.
Generic `Ops` determines its result representation and associated lifetime.

## Targets and coverage

Scalar paths cover every valid description, with all-width native mechanisms
for the configured AVX2, AVX-512 and NEON families. Native capability is a build
choice: an explicitly requested unavailable target returns `unsupported`;
`automatic` does not perform runtime CPU probing or choose a measured optimum.
AVX-512 helpers have distinct feature requirements; the complete payload family
is guarded by BW+VBMI, with additional GFNI/VBMI2 paths where enabled. Native
headers declare their exact guards. An ARM construction recipe still has the
same wire interpretation on x86.

## Tests and evidence

The [extension guide](extending.md) maps implementation changes to useful checks
and comparisons. [Workload definitions](../../workbench/benchmarks/seriespack/measurements.md)
state what is timed; linked findings retain source identities and limits.
Correctness coverage does not establish competitiveness, and matching a direct
wrapper control does not establish that the shared physical kernel is efficient.
With identical static format and admission facts, the composition interface is
intended to add no mandatory dispatch, allocation or intermediate materialization
over direct native work; establish that with code and caller measurements rather
than assuming it from the API shape.
General erased/CPS handoff, suspension state, progressive filtering policy and
stable database metadata serialization remain separate design work.
