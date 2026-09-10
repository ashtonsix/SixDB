# SeriesPack specification

Proposed first implementation contract, 2026-09-10. SeriesPack is a family of
**headless arrays of unsigned packed integers with one width `k=1..64` per
array**. It owns packing, access and local mutation primitives. It is intended
for direct use by other Ikea parts and Workbench spikes. This document selects
an initial scope and provisional preset tables; it does not claim the full
family is implemented or measured. See the [implementation sketch](implementation.md).

## Contract and intended uses

Headless means that the bytes contain no width, count, format tag, pointer or
ownership header. The parent supplies that information. It does not prohibit
separating leading value bytes into filtering planes; those are called heads
below. Width zero, automatic width discovery, NULL sentinels, signedness and
record membership are outside this primitive. Constant/empty-domain containers
and semantic transforms can compose around it.

For each index `i` in the supplied logical extent, a value is in `[0,2^k)`.
At `k=64`, every u64 bit pattern is valid. There must be no shift by 64 or
overflowing `1 << 64` in mask/range construction. The public scalar type can be
the smallest standard unsigned type containing `k` bits; a u64 materializing
adapter is available without making u64 the universal native carrier.

The array may be empty or exceed `2^16` values. A logical view does not imply
ownership, sorting, uniqueness, arithmetic meaning or a contiguous allocation.
Packing preserves index order and exact values, with no compression decisions
depending on the values themselves. A different width/layout requires explicit
re-encoding and a new actual representation description.

| Consumer | SeriesPack supplies | Enclosing structure supplies |
| --- | --- | --- |
| Hash bucket keys/fingerprints and slot metadata | Small arrays, equality masks, point updates, strided embedding | Hashing, membership, collision resolution, bucket capacity and stability |
| Metadata for other blocks | Headless point/bulk reads and updates | Offsets/checkpoints, child identity, dependency reconstruction and bounds |
| Column/row-oriented records and aggregates | Packed fields and native consumers | Field semantics, validity, record layout, summary laws and visibility |
| Delta-coded posting lists | Exact packed deltas and native handoff | Base/checkpoint, prefix reconstruction, sortedness and dependencies outside a selection |
| Strings and future TuplePack/StreamPack compositions | Byte-width cases and packed lengths/tags/offsets | String semantics, heterogeneous shape and variable-length framing |

[Iceberg hashing, section 3](https://arxiv.org/html/2109.04548v3#S3), describes
small fingerprint and slot-index arrays used for routing within a bin. This
motivates a customer shape, not an obligation to reproduce its exact packed-word
algorithm. SeriesPack must not infer that a fingerprint match is full-key equality
or claim the hash table's performance guarantees for a particular preset.

## Description, placement and execution are separate

A resolved description identifies the SeriesPack wire version, `k`, head count
in bits `h`, and one of the geometries below. The geometry determines values
per tile `T` and packed payload bytes per tile `B`. A placement supplies payload
base/extent, byte stride `S`, and any head-plane bases/extents/strides. Length
and the enclosing coordinate/owner association are supplied at attachment.

An execution choice identifies the operation, target features, point-reader
strategy and useful native region. It is not serialized as part of the bytes.
The preset is a recipe used to obtain a resolved description and initial
execution preference. Persist the resolved description, not an instruction to
“pick best again on this CPU.” A changed preset table cannot reinterpret an
existing array. No stable numeric wire tags are assigned by this draft.

All targets decode the same resolved bytes. ARM prioritization can influence
the choice made when constructing an array, but a subsequent x86 reader must
still honor its actual geometry. The encoder must not change the wire merely
because a different ISA is available.

## Initial presets

Keep three layout-selection policies and a few recipes. They all support
`k=1..64`; absent/ineligible stripe cases resolve to the compact local geometry.
Here `w=k-h` is the remaining payload width, not necessarily the logical width.

| Recipe | Layout policy | Heads | Tile stride | Initial execution preference |
| --- | --- | --- | --- | --- |
| `compact` | Uniform LocalPack residuals; local eight-value packets | 0 | Dense `S=B` | Small/point-oriented arrays; native bulk still available |
| `bulk` | Mixed table below | 0 | Dense | Bulk native regions, with point access on the same bytes |
| `bulk_arm` | ARM-oriented mixed table | 0 | Dense | ARM bulk; ordinary readers remain available on every supported target |
| `filter` | Mixed table | Explicit 8 or 16 | Payload dense; each head plane independently placed | Head-only work followed by selected reconstruction |
| `filter_arm` | ARM-oriented mixed table | Explicit 8 or 16 | As above | Same staged use, prioritizing ARM bulk construction/consumption |

Strided embedding is a placement modifier on these recipes, not another codec.
For example, `compact.at_stride(S)` leaves room for sibling data after each
eight-value packet. A head modifier may also be applied explicitly to `compact`.
Head legality requires `h ∈ {0,8,16}` and `h<=k`; no head choice is inferred from
the data. Users can override point-reader choice at binding without migration.

The initial mixed tables are deliberately small:

| Remaining width `w` | Uniform policy | Mixed policy | ARM mixed policy |
| --- | --- | --- | --- |
| 0 | Empty payload; tile of 8 logical values | Same | Same |
| 1..7 | Local eight-value transpose | ScanPack stripes | ScanPack stripes |
| 10 | Local packet | Local packet | Interleaved body/Scan2 |
| 12 | Local packet | Body64/Scan4 | Body64/Scan4 |
| 14 | Local packet | Local packet | Interleaved body/Scan6 |
| 15 | Local packet | Local packet | Interleaved body/Scan7 |
| 20 | Local packet | Body64/Scan4/body64 | Body64/Scan4/body64 |
| All other 8..64 widths | Local packet | Local packet | Local packet |

“Mixed” is the provisional pick-by-width policy among a curated set. It is not
a measured claim that each entry is fastest. Narrow ARM bulk provides strong
evidence for stripe tails; complete wider comparisons remain to be made during
implementation. The 10/14/15 entries are intentionally confined to the ARM
recipe initially, and are [borderline inclusions](#carry-forward-and-borderline-decisions).
For tiny arrays, their larger physical periods can dominate storage costs;
the compact recipe is the natural starting choice there.

This avoids exposing the full product of arbitrary tail wires, chunk
permutations, tile sizes and target tricks. An explicit description may select
one of the listed geometries for its supported widths, allowing controlled
comparisons and composition. Adding another geometry is an implementation and
wire change, not an unchecked integer template parameter.

## Physical layouts

The following bit maps define the proposed bytes independently of the kernels.
Bits within a byte are numbered from zero at the least significant bit.
Whole-byte bodies are little-endian. There is no per-tile header or implicit
readable/writable suffix. Every full tile uses exactly `T*w/8` payload bytes.

### Leading heads

For `h=8`, head plane 0 holds `(x >> (k-8)) & 255` for each index. For `h=16`,
plane 0 holds that same most-significant byte and plane 1 holds
`(x >> (k-16)) & 255`. Each plane is an array of bytes in logical index order.
The payload stores the low `w=k-h` bits. Reconstruct by concatenating the heads
in that order and then the payload; zero/full-width cases bypass invalid shifts.

For tile `t`, head plane `j` begins at `head_base[j] + t*head_stride[j]`
and contains `T` bytes. Dense head stride is `T`. The payload begins at
`payload_base + t*S`. Heads may be in separate allocations or different ranges
of one owner. Their ownership and correspondence to the payload are admitted
facts. A head-only read needs no payload access.

### Local eight-value packets

Write `w=8*q+r`, `0<=r<8`. A tile contains eight values and occupies `B=w`
bytes. Its first `8*q` bytes hold `q` whole-body bytes per value in AoS order:
for local index `i` and body byte `j`, byte `i*q+j` holds bits
`r+8*j .. r+8*j+7` of the value. Its final `r` bytes are the LocalPack transpose:
tail bit `b` of value `i` occupies bit `i` of byte `8*q+b`.

For `w=1..7`, this is exactly the existing LocalPack wire. For `r=0`, it is a
plain little-endian array of `q`-byte words. Thus `k=8/16/32/64`, head zero,
degenerates to ordinary fixed-width values without extra metadata or transforms.
When `w=0`, `B=0` and there is no payload pointer to access; heads carry the value.

### ScanPack residual stripes

A residual width `r=1..7` has `G=8/gcd(r,8)` groups of 32 values and
`C=r/gcd(r,8)` stripes of 32 bytes. One period has `T=32*G` values and `32*C`
bytes. Within each group `g`, local lane `l` uses byte lane `l` of its stripes.

For `r=3` and `r=6`, the following arrays map each value bit to a bit position
across the successive stripe bytes **at the same lane**:

```text
r=3, groups 0..7, bits 0..2:
  [0,1,2] [3,4,5] [6,7,14] [8,9,10]
  [11,12,13] [22,23,15] [16,17,18] [19,20,21]
r=6, groups 0..3, bits 0..5:
  [0,1,2,3,4,5] [8,9,10,11,6,7]
  [12,13,14,15,22,23] [16,17,18,19,20,21]
```

Map position `p` to stripe `p/8`, bit `p%8`. For the other widths, let
`s=g*r`, `a=s/8`, `u=s%8`, and `room=8-u`. A value fitting in that byte places
bit `b` at stripe `a`, bit `u+b`. A crossing value places its low `r-room` bits
in stripe `a+1`, starting at bit zero, and its remaining high bits in stripe
`a`, starting at bit `u`. This is the current continuous high-fragment-first
5/7-bit wire, not the historical permuted wire.

For pure residuals the stripe order is natural: stripe `c` begins at `32*c`.
The periods are:

| `w=r` | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `T` | 256 | 128 | 256 | 64 | 256 | 128 | 256 |
| `B` | 32 | 32 | 96 | 32 | 160 | 96 | 224 |

### Selected body/stripe placements

`Xi` denotes the AoS whole-byte bodies for logical values `32*i..32*i+31`.
For `q=1` it occupies 32 bytes. `Yj` denotes stripe `j` from the residual map
above, also 32 bytes. These placements change parent addresses, not the bits
inside a tail. All chunks are contiguous in the stated order, without padding.

| `w` | `q,r` | `T` | `B` | Physical order |
| ---: | --- | ---: | ---: | --- |
| 10 | 1,2 | 128 | 160 | `X0 X1 Y0 X2 X3` |
| 12 | 1,4 | 64 | 96 | `X0 X1 Y0` |
| 14 | 1,6 | 128 | 224 | `X0 Y0 X1 Y1 X2 Y2 X3` |
| 15 | 1,7 | 256 | 480 | `X0 Y0 X1 Y1 ... X6 Y6 X7` |
| 20 | 2,4 | 64 | 160 | `X0 Y0 X1`, with each `Xi` occupying 64 bytes |

For width 20, the body values inside each 64-byte `Xi` remain two-byte AoS.
Body bits are above the low `r` bits and reconstruct as `(body << r) | tail`.
In widths 10/14/15 the tail is physically distributed between body chunks;
an introspected tail child therefore needs its placement map, not a fictitious
pointer to a contiguous child allocation.

## Length, capacity and exact extents

Support arbitrary logical length `n` through complete storage tiles. Set
`m=ceil(n/T)` and physical value capacity `c=m*T`, with checked arithmetic.
The final tile's unused values are encoded as zero. There is no alternate
partial-tile wire and no length prefix. At full encode, read only the `n`
supplied input values, initialize the remaining lanes without overreading them,
and produce complete tiles. Point/range APIs still reject indices outside `n`.
Capacity here describes the tiles backing this view. Storage reserved beyond
`c` is a parent's byte reservation, not automatically initialized array values;
new tiles must be initialized before a larger view permits read/modify/write.

This is exact packing for complete tiles, **not** a promise of `ceil(n*k/8)`
bytes for arbitrary `n`. In particular a one-value striped array can occupy a
whole stripe period. Report this slack separately from payload bits and stride
gaps. A parent can reserve more complete tiles for growth; logical membership
and changes to the visible length remain its responsibility.

For `m>0`, the minimum payload address envelope is `(m-1)*S+B`; payload bytes
actually belonging to tiles total `m*B`. Each present head plane has envelope
`(m-1)*head_stride+T`, and owns `m*T` bytes. For `m=0`, all extents are zero;
for `w=0`, the payload extent is zero regardless of `m`. Dense total stored bytes
including heads and final unused values are `c*k/8`. Allocation alignment,
gaps and owner headers are additional costs.

Require nonoverlapping tile placements: `S>=B` when payload exists, and
`head_stride>=T`. Gaps may contain other components; SeriesPack must not read,
clear or report them as its own writes. Admission checks all products/sums,
buffer extents and overlap requirements before computing pointers. No hidden
64-byte suffix or allocator rounding may be used as access permission.
Actual payload and head tile ranges must also be mutually disjoint. Their
address envelopes may overlap when one stream occupies another's stride gaps;
admission must reason about occupied ranges rather than rejecting that valid
embedding merely because the envelopes intersect.

Appending within reserved capacity writes selected previously unused positions;
changing `n` is an enclosing action. Inserting/deleting by shifting values,
choosing a new width or growing storage is an outer composition. SeriesPack
provides the reads/writes for it without claiming stable positions or atomic
publication of the resulting array.

## Locality and native access contracts

The standard dense placement starts at a 64-byte boundary. Each point payload
read must access at most two **adjacent** 64-byte lines. Aligned sixteen-value
payload reads retain that bound where the selected dense layout can provide it.
Heads are the deliberate exception: their traffic counts in reconstruction,
but is outside the payload locality promise. Calling an ordinary scattered
body plane a head does not earn that exception.

- A local packet is at most 64 bytes, so points satisfy the bound at every base
  residue. A dense aligned group of sixteen spans two packets. With the admitted
  64-byte run origin it meets the group bound exactly for
  `w=0..34,36,40,48,64`, equivalently `w<=32+gcd(w,32)` for positive `w`.
- Every listed striped placement satisfies point and aligned-sixteen locality
  at tile residues 0 and 32. Require a 32-byte-aligned tile origin and a stride
  divisible by 32 for that guarantee; the dense placements all satisfy it.
- Other local strides remain usable for points. Group guarantees must be
  derived from that placement; they do not survive arbitrary gaps or run phases.
  A parent requiring a particular group guarantee must establish it at binding.

The [geometry audit](../workbench/spikes/ikea-composition/probes/ikea-integers/locality/README.md)
proves the dense-width limit under independent, exactly packed group data.
For example `w=56` gives 112-byte groups that sometimes span three lines;
`w=64` gives aligned 128-byte groups and passes. A separated head can change
which remaining width passes, in either direction.

Required data bits, issued access widths, unique accessed bytes and line
footprints are different facts. Native fragments can borrow a complete tile
and read neighboring encoded bytes within it if their declared access contract
and locality permit this. They must not overread the admitted tile/owner or
strided gaps. A materializing point API must not obtain its scalar result by
decoding a whole tile and touching unrelated payload lines.

Exact overlapping loads/stores are allowed when their union is legal. The
[width-56 body](../workbench/spikes/ikea-composition/probes/ikea-integers/wide56/README.md#data-and-native-work)
is useful prior art. Write locality is not automatically identical to read
locality; each writer exposes its actual writable footprint. Unaligned input
arrays and final partial input groups need bounded adapters, not implicit
overread permissions.

## Operations and authoring surface

The names below describe required operations; exact C++ spelling is provisional.

| Operation | Contract |
| --- | --- |
| `describe/requirements` | Resolve a preset or explicit geometry; derive extents, placement obligations, child maps and target requirements without touching values |
| `attach/bind` | Validate the runtime description/resources and select an operation once; retain owners in the enclosing binding when required |
| `get(i)` | One unsigned logical value, with no allocation or materialized tile |
| `read_native(range, selection, consumer)` | Supply ordered native fragments directly to a compatible consumer; empty selected regions may be skipped |
| `decode(range, out)` | Explicit materialization into a chosen sufficient unsigned element type; exact input/output bounds |
| `encode(values, out)` | Full construction of tiles including canonical zero final lanes; inputs must fit the width |
| `set(i, value)` / selected range write | Preserve every unselected value and all foreign bytes; expose complete physical write coverage |
| `read_head(j, ...)` / `read_payload(...)` | Independently expose the selected parts, with their meanings, coordinates and dependencies intact |
| Equality / unsigned comparison masks | Small ordinary compositions over reads/native comparisons, optionally fused; preserve the incoming selection's original indices |

Checked entry points return a small error before mutation for invalid shape,
range, capacity, input width or unsupported capability. Trusted kernel entry
points assume those facts; they contain no error tag or repeated validation.
Bulk input-value validation, when requested, is a real pass or fused checked
operation and must be charged. Do not silently truncate out-of-width values.
Initial encode/decode materializing endpoints require disjoint source and
destination ranges; in-place transcoding needs an explicit outer composition.
Borrowed read views require stable bytes for the invocation, and writable views
require the caller's synchronization/ownership for the declared footprint.

The first native API should follow actual useful register widths, not require
`get16 -> array<u64,16>` before a consumer can run. A 56-bit body currently has
useful u64 grains of 8/4/2 on AVX-512/AVX2/NEON; a narrow body can use many more
lanes. Storage tile size, native fragment size, requested logical range and
compilation region are independent. Arbitrary range ends may need a smaller
native/scalar boundary path. A consumer requiring a larger retained group pays
for that adaptation explicitly.

```cpp
// Pseudocode: a statically resolved SeriesPack wrapper, no dynamic Shape walk.
using P = SeriesPack<12, preset::bulk, head_bits<0>>;
auto source = P::view(payload, n, dense_stride);  // borrowed; facts already admitted

auto selected_sum(Ops& ops, auto source, auto rows, auto active, auto cutoff) {
    auto values = ops.read(source, rows, active);
    auto keep = ops.unsigned_less(values, cutoff, active);
    return ops.sum(values, keep, explicit_sum_contract);
}
// The shared value need not be stored; a selected region reads, filters and sums.
```

This preserves a recordable expansion for composition without making a general
graph implementation part of the first SeriesPack deliverable. Blocks/heads,
their placement and read/join operations remain inspectable. A specialized
read/filter region must implement the same dependencies and meaning.

Selections name original array positions and cannot compact implicitly. Loading
a shared packed byte containing an inactive value may still be necessary; an
inactive value need not be decoded or consumed. Final unused lanes never become
selected logical rows. A head-derived candidate mask is not an exact equality
mask. For unsigned range predicates, a known head defines an interval of possible
values and can sometimes certify or reject; semantic adapters must justify the
same reasoning for signed/float encodings. A full progressive-evidence protocol
remains outside this first primitive.

### Zero-overhead meaning

For static `k`, layout, head choice and the same placement knowledge, SeriesPack
must lower to the same native work as direct use of its internal mechanisms.
For widths 1..7, `compact` is exactly LocalPack and `bulk` exactly ScanPack,
with no extra bytes or compulsory runtime object. Useful tests compare the
wrapper and direct body in the same target TU with identical operands and
consumer contracts.

No extra width/layout switch, child descriptor loads, allocation, reference
count operations, indirect call or intermediate stores may be introduced merely
by that static wrapper. This is an implementation obligation, not an already
measured claim. Runtime erasure can select an independently compiled endpoint
at a deliberate boundary; its dispatch is priced separately. A native value
need not cross an erased boundary just because its metadata did.

## Mutation and integration

Point writes update body bytes and read/modify/write only the packed tail bits
that belong to the value, plus any heads. Different logical values can share
physical bytes. Native bulk writers may use a larger admitted footprint, but
must preserve unselected values and may not write gaps/neighboring owners even
when they would restore the same bytes. Concurrent disjoint-index writes are
therefore not automatically race-free or atomic.

Derive fixed footprints in an adapter, and return/append compact coverage only
where it is dynamic. Do not instrument every scalar store with a virtual MVCC
callback. The write capability must authorize the complete read/modify/write
footprint, and effect capacity must be available before changing bytes.
SeriesPack does not allocate, suspend, publish a transaction or update ancestor
summaries inside a native body. An enclosing composition supplies those duties
and can consume semantic old/new values without decoding them a second time.

The [semantic/integration proposal](../workbench/spikes/ikea-composition/semantics-and-integration.md)
retains the wider questions. First implementation needs exact local footprints,
borrowed lifetime and nonsuspending calls; it does not require deciding the
database's MVCC format, schema system or Loom suspension representation.

## Carry-forward and borderline decisions

These are initial inclusion decisions, separate from historical probe acceptance.
Reconsider them when a real consumer supplies stronger evidence; there is no
requirement to preserve every experimental switch in the production module.

| Mechanism | Initial decision | Reason and evidence limit |
| --- | --- | --- |
| Local8 body/tail packets; narrow LocalPack and current ScanPack wire | **Include** | Complete baseline geometry and direct primitive equivalence; measured narrow kernels and width-56 body provide implementation examples |
| Separate 8/16-bit heads and explicit tile stride | **Include** | Needed for filtering and embedding without making placement/meaning implicit; full operation costs still include heads and gaps |
| Body64/Scan4 at `w=12` | **Include** | Exercised in native composition; only one of the two equivalent-width parent placements is selected here |
| Body64/Scan4/body64 at `w=20` | **Include, borderline** | Reuses the same Scan4 mechanism and a small chunk map; proved geometry, but no wider native timing. Adds a useful second whole-byte body width without a new tail algorithm |
| Interleaved Scan2/6/7 at `w=10/14/15` | **Include in ARM recipe, borderline** | Positive locality witnesses and strong narrow ARM stripe economics justify a limited attempt. Complete body/join performance is unmeasured; large periods and mapping code are costs |
| Arithmetic and constant-offset Scan5/7 point/group readers | **Include both** | Material small-versus-large access tradeoff on unchanged bytes; approximately doubled reader text is justified here. Bind explicitly, no cache classifier |
| Balanced packing and ARM shift-insert reconstruction from the bit map | **Include** | Shared algebra and substantial narrow ARM improvements; do not recreate a generic ISA abstraction around the bodies |
| Compact AoS byte expansion, including irregular 3/5/6/7-byte bodies | **Include** | Needed for full-width coverage and locality; width 56 is measured, other native widths still require implementation/checks |
| Optional SVE2 bit-permutation helpers | **Defer, borderline** | Keep NEON as the first complete ARM path. Existing V2 timings do not isolate the optional instruction choice's contribution; add a small same-wire specialization if it earns its maintenance cost |
| 32-value width-56 AVX-512 encoder region | **Exclude initially, borderline** | Measured 6–9% encode improvement, but more code and four index tables instead of one; packet encoder already supplies the function. Keep the evidence for an encode-heavy caller |
| Register-mask Scan5/7 encoder control | **Exclude initially, borderline** | Partial width-7 improvement with extra instructions/code; did not solve the wider Zen gaps |
| Paired 64-byte AVX-512 stripe execution | **Exclude** | Mixed small gains and regressions; retain 32-byte storage stripes and ordinary useful native regions |
| Historical permuted Scan5/7 wire; GFNI Scan decoder experiment | **Exclude** | Additional wire/implementation variants without a compelling overall case. This does not exclude GFNI use for LocalPack transposes |
| Arbitrary chunk permutations, optimal padded supertiles, global body planes | **Exclude from presets** | Extra format/placement space without enough benefit for this scope; padding and separated ordinary body planes also change locality/storage contracts |
| General graph matcher, universal native carrier, per-node CPS switches | **Exclude** | Probe scaffolding is not the production authoring model; shared functions and chosen regions are sufficient to start |

The [narrow measurements](../workbench/spikes/ikea-composition/probes/ikea-integers/measurements.md)
support these distinctions. Current continuous Scan5/7 has strong ARM results,
near-parity GNR comparisons and remaining Zen gaps; historical permuted-wire
parity must not be used to claim those gaps are gone. The optional
[width-56 encoder](../workbench/spikes/ikea-composition/probes/ikea-integers/wide56/README.md#a-larger-encoding-region-over-unchanged-packets)
and [locality placements](../workbench/spikes/ikea-composition/probes/ikea-integers/locality/README.md#repairs-for-intact-32-byte-interleaving)
retain the evidence for borderline decisions. No new hardware measurements
were made for this specification.

## Implementation checks that address this contract

Use an independent bit oracle for all selected layouts and widths. Check
round-trip values and canonical bytes, all basis bits, head reconstruction,
empty/partial arrays, and `k=64` extrema. Enumerate reachable placement phases
and aligned groups; guard native accesses at exact extents, including strided
gaps. Check selected writes preserve other values and that coverage contains
every issued store. Include small logical lengths where tile slack is large.

Verify static wrapper equivalence against direct LocalPack/ScanPack-equivalent
bodies and native reuse without a materialized array. Runtime-bound operations
need separate checks for description/extent/capability errors and unchanged
bytes on rejected writes. Measure point, bulk encode/decode and an immediate
consumer under matched contracts; full-width and ARM preset speed claims await
those implementations. Compile time and text/data size count when deciding
whether a borderline implementation stays. These checks accompany development;
they are not a requirement for another general-purpose spike before starting.
