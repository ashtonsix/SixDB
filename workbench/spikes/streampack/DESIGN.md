# StreamPack: variable-sized values in packed blocks

StreamPack is a proposed Ikea primitive for storing short byte sequences densely.
It can hold strings, blobs, optional values, or fields with different meanings.
A block has room for 128, 256 or 512 slots. Each slot can reserve 0–65,535 bytes;
a schema may choose a smaller maximum of `2^k - 1`, including zero.

The payload is headless: it contains only the reserved bytes. The caller supplies
separate metadata, called **sidecars**, to locate values and describe their state.
StreamPack borrows those spans. It does not allocate memory, own records, choose
transaction visibility, or interpret strings. A schema may keep string prefixes
in SeriesPack and suffixes in StreamPack.

This document explains the model and intended interface. The executable code is
research code; the production surface and presets remain open. [Results](RESULTS.md)
records measured behavior. [Investigation](PLAN.md) identifies comparisons, prior
art and remaining questions without making them requirements of every caller.
The [implementation recommendation](RECOMMENDATION.md) selects the initial scope
and explains what should remain with the owner or in Workbench.

## Slots, ranks and bytes

A **rank** is a slot's index within one physical block. A **holder** belongs to
the enclosing collection and names a block and rank. Multiple holders may name
the same slot. A logical row can therefore survive movement or deduplication by
having its holder updated. Ranks are local to a representation, not durable IDs
that other representations can retain independently.

For each assigned rank, distinguish three facts:

| Fact | Meaning |
| --- | --- |
| Reservation `R` | Initialized bytes assigned to the slot, including unused slack. |
| Used length `U` | Bytes constituting the value; `0 <= U <= R`. |
| Tombstone `D` | Whether the slot has been retired. |

An assigned, live, empty slot differs from an unassigned slot or a tombstone.
Logical null and absence are schema facts. When every reservation is full, used
lengths can be derived from reservations. When slots cannot be retired, the
liveness sidecar can be omitted. Operations requiring a missing fact must refuse
or use an explicit caller-owned representation transition.

Consider reservations `[3, 0, 5]`, used lengths `[3, 0, 2]`, all live. The slots
start at offsets `[0, 3, 3]`; the reserved body ends at offset 8. Rank 1 is a
valid empty value. Offset 3 belongs to rank 2, as do its three slack bytes.
Offset 8 and allocator capacity beyond it have no owner.

The current geometries keep positive reservations in disjoint, rank-ordered,
half-open intervals. Reverse lookup maps a physical byte offset to
`(rank, tombstone)`. It returns no owner for zero-width slots, the end sentinel,
or allocation tail. Returning an owner does not establish a logical search hit:
the entire match must fit within that owner's live used span. This rejects
cross-slot matches and matches wholly or partly in slack or retired bytes.

## Sidecars

Geometry answers where a reservation starts and ends. Used lengths, liveness and
assigned count can be composed with it independently.

| Geometry | Stored information | Principal trade-off |
| --- | --- | --- |
| Direct | Each slot's exclusive reservation end; rank zero starts at zero | A preceding endpoint locates a value. Two endpoints give its reservation. Endpoints are sized from the block byte bound. |
| Width | Each slot's reservation width; the start offset of each group of 16 slots | A checkpoint plus a masked local prefix locates a value. Widths are sized from the slot bound. |
| Group-relative endpoints | Within-group ends plus group bases | Trades larger local entries for direct local addressing. An exploratory read control. |
| Fixed/presence geometry | A common width and, if needed, a bitmap of occupied reservations | Useful when a schema can derive geometry; arbitrary shrinking may require a representation change. An exploratory read control. |

The proposed public geometry names are **Direct** and **Width**. Native 8/16/32-bit
fields, packed codes and ISA execution are choices behind the resolved layout,
not separate semantic families. Native widths remain useful optimized cases.

Given capacity `N`, inclusive maximum reservation `M` and a separately enforced
body limit `B`, derive reservation/used bits from `bit_width(M)` and endpoint bits
from `bit_width(min(B, N*M))`. The optional used-length field follows the slot
bound for **both** geometries. A tighter independent used bound could narrow it
further. A zero maximum needs no length codes. Do not silently round a requested
maximum down: 4,095 needs 12 bits, while inclusive 4,096 needs 13.

Without a tighter body limit, at capacity 256:

| Inclusive slot maximum | Reservation / used bits | Direct endpoint bits | Width checkpoint bits |
| --- | ---: | ---: | ---: |
| 255 | 8 | 16 | 16 |
| 4,095 | 12 | 20 | 20 |
| 4,096 | 13 | 21 | 20 |

The last checkpoint precedes 16 slots, so its bound is `(N-16)*M` for these
capacities. An endpoint can reach `N*M`. These are format bounds, not observations
that happen to hold in one population. Rank and byte limits can independently
seal a block; choosing 16-bit endpoints with wider slots needs an enforced byte
limit or earlier sealing.

Width needs **`ceil(assigned_count/16)` group starts**, with capacity for
`ceil(N/16)` entries: 8, 16 or 32 at the three candidate capacities. The first
entry is zero. There is no terminal checkpoint in the new layout comparison.
The owner supplies an authoritative occupied byte extent along with assigned
count; allocation capacity cannot substitute for that extent. This also makes
empty blocks unambiguous. Reverse lookup uses the last checkpoint not greater
than the offset, including repeated starts from zero-width groups. The older
mutable control still stores a terminal checkpoint and a leading Direct zero;
its timings describe that control, not a requirement of the proposed interface.
Direct can derive occupied extent from the final assigned endpoint; a coherent
bound extent does not require an additional persistent word in that format.

Byte-width checkpoint groups have 16 initialized entries, including padding in
a partial final group. SIMD can load that group within its admitted metadata
span. Point decoding uses a masked widening sum; reverse lookup finds a checkpoint
group and computes inclusive local prefixes. Sequential scans keep a running
offset. Architecture-specific execution changes neither stored bytes nor ranks.

A caller may also maintain direct location/length descriptors or inline tiny
values. These are compositions outside the headless payload. Their storage,
relocation repair and lifetime costs belong to the complete comparison.

## Physical placement and reuse of Ikea primitives

Geometry, optional state and their placement are separate decisions. Interleaving
can bring fields needed together into fewer cache lines. Separate planes avoid
bringing geometry into a lengths-only scan. Padding may reduce line crossings
but increases the resident metadata. Those trade-offs need complete consumers.

`layout.h` compares these concrete recipes with capacity 256, used lengths and
tombstones present. Offsets below are physical, not another ownership layer:

```text
Separate native planes:
  [all endpoints OR all widths] [all used lengths] [tombstone bitmap]
  [alignment gap] [all Width group starts]

Native group of 16 slots:
  [group start:u32] [16 endpoints OR widths] [16 used lengths] [16 dead bits]
  [optional stride gap]
  ... next group ...

SeriesPack:
  [Local geometry plane] [aligned Local used plane] [tombstone bitmap]
  [aligned Local Width-checkpoint plane]

TuplePack row records:
  [geometry bits | used bits | dead bit | byte-rounding spare bits] per slot
  [aligned Width group-start plane:u32]

Packed TuplePack Width group, maximum 4,095:
  [start:u32] [width low bytes:16B] [width high nibbles:8B]
  [used low bytes:16B] [used high nibbles:8B] [dead bits:2B]
  [optional 10B stride gap]
```

The grouped Direct recipe duplicates a group start to keep a group's first rank
from touching the previous group. It therefore changes boundary storage as well
as placement. Native fields are byte-sized to the schema; the `planes-used16`
control alone forces 16-bit used lengths for maximum 255.

| Native group, 16 slots | Maximum 255 | Maximum 4,095 or 4,096 |
| --- | ---: | ---: |
| Direct: header + endpoints + used + dead | 4 + 32 + 16 + 2 = **54 B** | 4 + 64 + 32 + 2 = **102 B** |
| Width: header + widths + used + dead | 4 + 16 + 16 + 2 = **38 B** | 4 + 32 + 32 + 2 = **70 B** |

The experiment tests tight packing and strides 64, 96 and 128 where they cover
the owned group. A 96-byte stride alternates its origin between offsets 0 and 32
within a 64-byte line; it does not align every group. Wide Direct cannot fit a
96-byte stride, and neither wide native geometry fits 64. Stride gaps are not
owned fields and mutations preserve them. Measurements report requested span,
owned metadata, actual Orbital capacity and shared block control separately.

SeriesPack uses its real checked placement and native decoders, including packed
checkpoints. Local tiles contain eight rows and their payload plane origins need
64-byte alignment. A single ordinary view cannot describe two tightly adjacent
eight-row tiles followed by an unrelated per-16-slot header/state region using
one constant tile stride. That is a composition constraint to resolve explicitly,
not permission to bypass attachment checks.

TuplePack's row control contains byte-contained codes assembled into each schema
field. Its unit may occupy at most 64 bytes; stride may exceed the unit. A
16-row native projection obtains Width prefix inputs without sixteen erased
calls. Row rounding costs space. The follow-up in `layout_tuple_group.h` instead
uses one 54-byte unit for sixteen maximum-4,095 slots: 84 byte-contained codes,
including the native 32-bit checkpoint. Its full-group projection produces
sixteen natural 16-bit width lanes. Tight and 64-byte strides compare the same
owned bytes, with per-rank maps prepared once for the schema. Boundary checks also
exercise maxima 255 and inclusive 4,096; the timing follow-up is bounded to 4,095.
Generic prepared controls and map selection have execution costs, so neither
adapter establishes the best possible TuplePack composition by itself.

The Direct batch control uses actual SeriesPack or TuplePack decoding to obtain
sixteen endpoints. The source spells a SIMD shift inserting the previous endpoint
to produce sixteen starts; it needs no width prefix sum. In some ordered native
consumers, that shift folds into scalar loads feeding the variable-length copies.
The shuffled/packed consumers retain native batch work; linked review records
the actual lowering. TuplePack's row description
can split a used length across physical bytes: a 20-bit endpoint followed by a
12-bit used length yields 4+8-bit pieces for that length; the 21/13-bit case
yields 3+8+2. Its prepared group projection emits one byte-code plane per piece,
then explicit SIMD reconstructs native lanes. Liveness is projected as one
sixteen-row packet. Those preparation maps are shared schema state; the encoded
rows contain only their endpoint, used bits and tombstone.

These controls use one population-wide placement of each plane and full capacity
metadata. Per-block allocation alignment, partial-block utilization, append and
full MVCC costs must be charged before selecting the corresponding owner recipe.

## Separating lengths from the block directory

A hybrid Width recipe keeps reserved and used lengths together in groups of
sixteen, while the block's tombstone bitmap and all checkpoints share a compact
directory. For 256 slots:

```text
Lengths:  [R0..15][U0..15] [R16..31][U16..31] ...
Directory with 16-bit checkpoints: [tombstones:32B][16 starts:32B] = 64B
Directory with 32-bit checkpoints: [tombstones:32B][16 starts:64B] = 96B
```

Both lengths use the schema's slot bound. Checkpoints use the full block bound,
not its current occupancy: maximum 255 permits 16-bit checkpoints at N=128/256,
but needs 32 bits at N=512. The directory then occupies 32/64/192 bytes for those
three capacities. With 32-bit checkpoints throughout it occupies 48/96/192 bytes.
Direct geometry still has one endpoint per slot, so the same directory economics
do not automatically transfer to Direct.

The field experiment separates three changes: existing native planes to a
32-bit hybrid with still-separated R/U; then grouping R/U; then narrowing
checkpoints where the schema permits it. A fourth control changes only the
32-bit directory stride from 96 to 128 bytes. Tombstones remain first in both:
the 64-byte checkpoint region starts at offset 32. At stride 96 its start alternates
between line phases 32 and 0; at stride 128 it always starts at phase 32. This
compares those exact placements, not a claim that 128 aligns every checkpoint
load. A spatial-prefetch explanation requires evidence beyond a stride timing.

This organization also permits reserving the full directory when a block is
created, while growing only its length groups. Reserving a directory and keeping
its address stable are separate choices:

- A fixed directory prefix and growable length suffix can share one Orbital
  Allocation. Offsets within it stay fixed; relocating growth still invalidates
  borrowed pointers to both regions.
- A separate directory Allocation keeps its address independent of private
  length growth, at the cost of another allocation and its capacity rounding.
- If occupancy is known, allocating the entire metadata block at once avoids
  growth. A sparse long-lived block then pays for unused length groups.

The current `layout_growth.cpp` aligns the length origin to 64 bytes so that
16 pairs of native u16 lengths occupy one line, as in the read experiment. A
96-byte directory prefix therefore has a 32-byte alignment gap before lengths;
that gap is charged in the allocation. A separate directory needs no such gap
in its length allocation. The earlier unpadded growth control is retained as
allocation evidence, not as a read-layout equivalence claim.

Consequently, the 96B-versus-128B population-directory comparison does not
choose the size of this block-local prefix: a 96B directory plus its alignment
gap already occupies 128B. Packing directories at a 96B stride across many
blocks and allocating a single block with aligned length groups have different
space arithmetic. Keep that owner choice explicit when applying the timings.

The growth experiment tests these owner choices against compact controls that grow
and require internal repacking. All keep the same 16R+16U organization and double
the admitted number of groups. Full directory reservation is a caller policy;
StreamPack still allocates nothing. Initialization of inactive directory entries
does not authorize reading missing length groups. A partial group must have its
admitted SIMD-read footprint initialized, while assigned count and occupied
extent remain the only logical bounds.

The private growth checks replay exact physical afterimages after each append
and growth. They do not publish an ObjectStore version. In an MVCC owner, old
readers must retain a coherent old directory, lengths, payload and count/extent;
a fixed directory is not permission to update shared version state in place.

Two block-local read controls separate independently addressable blocks from
population-wide transposed planes. At N=256 and maximum 4,095:

```text
Block planes: [R:512B][U:512B][dead:32B][starts:64B][gap:32B]
Block hybrid: [dead:32B][starts:64B][gap:32B][16R:32B,16U:32B] x16
```

Both own 1,120 bytes and occupy a 1,152-byte span. Length origins are line-aligned
and checkpoint origins have the same line phase. The comparison changes both
R/U grouping and directory position. Their read fixture still packs blocks into
one allocation; the private growth experiment separately charges actual block
allocation. Neither substitutes for an MVCC owner using the new physical recipe.
Growing `[all R][all U]` also needs an explicit placement policy: keeping R and U
adjacent while expanding R moves U, reserving all R fixes its offset but spends
capacity upfront, and separate allocations add ownership/capacity costs. The
private growth experiment holds 16R+16U grouping constant across its four owner
policies; it does not measure those alternative split-length growth policies.

Locality depends on the requested operation. With u16 lengths, a grouped line
contains sixteen R and sixteen U values; a U-only plane line contains thirty-two
U values. That density difference matters to a U-only stream. A complete Width
lookup instead reads its sixteen-width prefix and selected U: grouping can put
both in one line, while separated planes generally need two. A sequential scan
consuming both reads the same useful R/U bytes in either organization. These are
address/byte facts, not measured cache-miss counts or an explanation of a timing
gap. Choose the layout from the complete operation's dependent and overlapping
execution, not by extrapolating a single-field probe.

## Organizing the interface

Keep three layers explicit, with only the first two ordinarily visible to a
caller:

1. A schema states slot capacity, inclusive slot and body bounds, and whether
   used lengths or liveness can be derived. A curated recipe resolves a physical
   description, required extents/alignment and execution preparation.
2. A named borrowed binding supplies payload, metadata windows and authoritative
   assigned count/occupied extent. It hides child SeriesPack/TuplePack bindings.
   A writable binding additionally establishes command admission and effects.
3. Typed bodies implement the resolved geometry and field projections. Native
   loads, packed decoders, fusion and CPS reuse these bodies; they do not add
   semantic variants or allocate per call.

Ask for the facts a consumer uses: length/liveness, value span, reservation,
reverse owner or an ordered range. A full slot-description return is convenient
for diagnostics, but should not force point value reads to decode reservations
or tombstones that the admitted consumer does not use. A sequential Direct range
carries the previous endpoint; it does not re-derive that start each iteration.
Width ranges similarly carry their running offset.

Keep dynamic recipe selection outside rank loops and expose native masks/packets
to composition where useful. The ordinary checked surface and author bodies
should share laws and kernels. CPS belongs around meaningful memory-dependent
operations, not around every field or prefix sum. Owner allocation, sharing,
visibility and remap publication stay outside all three layers.
Structural binding and validation of encoded contents are separate operations.
Checked mutation preserves admitted contents; refreshing an appended read window
should not repeat a full geometry scan. Recovery or untrusted input still needs
content validation before trusted execution.

The resolved storage description must remain identifiable for recovery and
damage translation, even when users normally select only a curated recipe.
Hiding a bit width from a method name does not make byte layout implicit. Persist
semantic/physical descriptions, not native ISA controls. Prepare reusable maps
once per schema/operation; per-block metadata does not contain those plans.
Prefer a simple storage view and separately stable prepared execution over a
view that owns objects to which it also holds pointers. Such self-borrowing needs
explicit rebinding on move; the research adapter instead disallows copying and
moving. It is not a proposed production ownership shape.

Removing a terminal checkpoint transfers an authoritative fact to caller control;
it does not remove the need to publish changes to occupied extent. Append and
compaction must keep payload, metadata, count and extent coherent. The owner
must include any control stores it issues in its publication damage.

## Ordinary operations

The desired public surface is small. It follows Ikea's [authoring conventions](../../../ikea/docs/source.md)
and [owner integration](../../../ikea/docs/integration.md); exact names below are
interface direction, not an implemented production API.

| Surface | Operations and result |
| --- | --- |
| Attach/bind | Validate placement, sidecar shape, maxima and readable bounds once; borrow a named view. No allocation or version acquisition. |
| Construct/transfer | Initialize supplied destination storage or copy selected source values in caller-chosen order. Allocation, deduplication decisions, remaps and holder repair remain with the owner. |
| Point read | Used length, live value span, and reverse byte owner. Checked logical value reads reject invalid or retired ranks; reverse lookup can report a retired owner. Trusted bodies remain available to admitted callers. |
| Range/selection read | Visit live values in a range or caller-supplied rank order, preserving rank identity, duplicates and requested order. Physical byte scans instead deliver physical coordinates or native byte masks in their documented order. |
| Mutation | Append, replace within a reservation, retire, and rank-preserving packing. Report issued writes through the caller's effects sink. |
| Author composition | Inline the same typed bodies into larger operations, or wrap a useful bounded operation in the existing native-chain/CPS machinery. |

A point call must not build a decoded directory. A range must not be forced
through repeated generic point calls. Temporary decoded starts can be useful for
dense arbitrary-order reuse; their preparation and bounded scratch space must be
paid by that operation. They confer no authority to outlive a changed geometry.
The caller maps physical occurrences to logical holders, including shared-holder
fanout, and chooses whether it needs occurrences, matching slots or matching rows.

The spike's `Direct`, `Grouped`, `MutableDirect` and `MutableWidths` are aggregate
borrowed views, not yet this checked public interface. Read accessors trust an
admitted rank and coherent spans; `admit()` validates geometry but does not prove
allocation ownership or version identity. Mutators retain command checks after
shape admission. Their ordinary-looking names must not be mistaken for a finished
checked/trusted API split.

Here is a complete small use of the current direct view. Storage remains owned
by the surrounding scope; real owners provide their own spans and damage sink.

```cpp
std::array<std::byte, 1024> body{};
std::array<std::uint16_t, 129> ends{};
std::array<std::uint16_t, 128> used{};
std::array<std::uint64_t, 2> tombs{};
std::uint16_t count = 0;
streampack_next::MutableDirect<std::uint16_t> slots{
    ends, body, used, tombs, count, 255};
ikea::no_coverage private_construction;
assert(slots.initialize(private_construction) == streampack_next::Error::none);
const std::array value{std::byte{'a'}, std::byte{'b'}};
assert(slots.append(value, 4, private_construction) == streampack_next::Error::none);
auto values = slots.read();
assert(values.value(0).size() == 2);
assert(values.reverse(3)->rank == 0); // reservation slack still has an owner
```

`no_coverage` is suitable here because this is unpublished private construction.
A published mutation needs the owner's actual effects and publication procedure.
The proposed bound surface should keep comparable ordinary use short without
requiring callers to assemble transaction machinery for a byte read.

## Mutation, compaction and invalidation

An append reserves a new rank, initializes its entire reservation and records
its used length. An edit that fits the existing reservation can preserve geometry;
changing reservation size appends elsewhere and retargets the logical holder.
Editing one of several shared holders detaches that value even when it fits.
The old slot is retired only when its last holder leaves. Allocation uniqueness
alone does not prove unique logical ownership.

Soft compaction preserves every rank and sets retired reservations to zero.
Shrinking live reservations to used lengths is an explicit option. It changes
physical offsets, so external byte descriptors need repair even though rank
holders remain valid. Reducing format extent does not necessarily refund backing.

Hard compaction creates a replacement representation. It may remove ranks,
reorder, deduplicate, or repartition across blocks. It publishes the complete
partial/many-to-one remap and repairs every actual holder. An inverse index may
reduce discovery work only if the caller maintains and pays for it; otherwise
the full holder scan is part of the operation. Sorting suffix bytes does not
establish full string order when prefixes are stored elsewhere.

| Change | What the caller must reconsider |
| --- | --- |
| Same-reservation edit | Used-length/liveness observations, summaries and logical sharing; geometry is unchanged. |
| Retirement | Liveness and summaries change. Reservations and decoded starts remain valid until packing. |
| Append | Assigned count and initialized metadata/body bounds. Existing read views capture their count/window; refresh them. |
| Soft packing | Every moved byte span, direct byte descriptor and prepared geometry cache. Rank identity remains. |
| Allocation movement/growth | All affected pointers, extents and bindings; initialize newly exposed bytes before use. |
| Hard rewrite/adoption | Representation identity, block/rank holders, descriptors, summaries and derived caches. |

Before any relocating growth, resolve input that aliases the old allocation by
preserving source coordinates, staging it, or rejecting the case. Calling
`memmove` on a pointer invalidated by growth cannot make it safe. An old version
must retain its payload and all interpretation metadata together.

## Allocation and MVCC ownership

The owner uses Orbital `Allocation`, `allocate` and `grow` directly. Known rewrites
can size destinations and allocate once; online appends need an explicit growth
or rollover policy. An all-zero body needs no zero-byte allocation. A `Hold` pins
backing, not immutable contents or a logical version.

Before executing a mutation, the owner establishes exclusive authority over the
writable interpretation and reserves infallible effects capacity. A refused
single checked command changes neither supplied spans nor its effect output.
Typed planes retain their natural alignment. Distinct caller storage windows,
count/extent control and effect storage obey the binding's admitted alias rules.
Within one packed metadata window, logically distinct fields can share bytes;
sibling-preserving stores require authority over those actual physical bytes and
report them as damage. These are binding requirements, not guarantees conferred
by having spans of the right lengths or disjoint field bits.
A larger private builder can have a different, explicitly documented disposal
rule. Hooks cannot suspend between preflight and stores.

Issued damage identifies a **stable named source view**, a plane and a byte span.
It records stores, including preserving stores, rather than changed bits or a
semantic redo log. The owner resolves it to actual Orbital Parts while bindings
live. Payload, geometry, used length, tombstones, count, occupied extent, checkpoints and holder
writes all participate. Empty payload damage can still mean a logical change.
Logical destinations, read dependencies, summary maintenance/removal and remaps
are separate owner facts; byte damage alone does not describe them.
Preserving a neighboring geometry bit while storing its byte does not by itself
invalidate decoded starts. Conversely, changing the occupied extent can change
reverse-lookup validity while every checkpoint remains unchanged.

The executable material groups up to two blocks and their holder array into one
coherent ObjectStore representation. It freezes owned afterimages against the
exact predecessor, reconstructs and geometrically admits the candidate, then
prepares and installs it. Complete issued coverage and summary maintenance remain
trusted producer obligations: geometric admission cannot detect a missing
same-extent payload patch or a stale summary. Independent history oracles test
those obligations. Old readers continue to see their prior interpretation. Hard rewrites
use full replacement and actual holder repair. This is a concrete integration
fixture, not transaction machinery to embed in StreamPack.

The caller currently copies predecessor Parts. Orbital lacks a native way to use
an unchanged captured allocated Part as a new capture source, and adoption retains
the current primary. Those limitations have been reported to Orbital. Their costs
remain visible in the experiments; a StreamPack allocator layer would not solve
them. The ObjectStore catalog's existing PMR interface is unrelated to payload
and sidecar allocation.

## Fusion and scheduled memory access

Ready point, prefix and scan bodies should inline into the consumer. A span or
mask should reach its sink without an obligatory intermediate array or erased
per-slot callback. Cold recipe/ISA selection belongs outside rank loops. The
ordinary path and CPS path must share those bodies and their access contracts.

A dependent lookup first reads geometry, then accesses payload. When many lookups
are independent, a Loom Ring can overlap those waits: hint geometry, decode it,
hint the actual payload address, yield, consume payload, and continue. Arithmetic
prefix reconstruction does not need its own scheduled stage. A single unresolved
chain has no independent successor address to prefetch early.

The Ring caller retains stable reader state and backing through drain. Native
callbacks carry scalar/pointer state and use Loom's existing `preserve_none` /
`musttail` ABI; they never retain callback-local views. Per-plane demand classes
are bound against qualified windows. Hints neither establish readiness nor grant
permission to read slack or beyond a live value. Ring storage and admission/drain
costs are part of the caller's throughput economics.

## Choosing a composition

Choose from the consumer's actual work: point versus sequential versus selected
reads, independent lookup width, optional metadata, mutation batch size, body-byte
limit, sharing, and retained readers. The experiments cover NEON, AVX2 and AVX-512
execution; SVE2 remains optional unexplored work. There is no universal block size,
geometry or compaction interval implied by the model.

The complete publication comparison favors widths for update cost in the tested
caller, while read cost depends on selection order and the work used to prepare
geometry. Neither journal fusion nor Ring scheduling is a universal improvement.
Keep these execution choices separate from the stored format.

Begin production implementation by turning the proposed checked/trusted binding
boundary into a small exercised interface, preserving cheap point and range
paths. Changing hot sets, descriptor maintenance and maintenance tail latency
remain useful discriminators as those callers are implemented; they are not an
open-ended prerequisite to starting. Additional formats need a demonstrated
consumer advantage sufficient to pay their maintenance cost.

The layout controls do not yet implement the new checked mutation handoff.
Carry the winning representation into a small real owner sequence before fixing
its stored recipe: admit one coherent count/extent observation, append into a
partial group (including a live zero-byte slot), edit shared metadata bytes,
publish damaged afterimages, retain the prior reader, then grow/rebind and release.
Include refusal with unchanged control/effects and a changed final extent with
unchanged count/checkpoints. This exercises the specific seam introduced by
removing the terminal sentinel; it does not require a new Orbital abstraction.

Graduate the small binding, selected layouts, typed operations and their laws
into Ikea. Keep comparative adapters, workload generators and native evidence in
Workbench. A production implementation need not carry every experimental
placement or expose the matrix as a collection of public knobs.
