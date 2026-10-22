# StreamPack reference

StreamPack stores variable-length byte values in caller-owned storage. It combines
[Intervals](../intervals/reference.md) search and subdivision kernels with value lengths, payload
access and coordinated value edits. A block has up to 256 assigned ranks. StreamPack
borrows its storage; the collection owns allocation, retained versions and publication.

This reference starts with the value model, then explains the module boundary, schema,
storage and operations. The [usage guide](usage.md) gives a worked block and consumer
patterns.

## Values, reservations and ranks

A **rank** is a stable zero-based slot number. A block with `n` assigned slots has
ranks 0 through `n-1`. Each slot has a contiguous byte **reservation** of size **R**,
containing a value of **used length U**, with `0 <= U <= R <= 65535`, subject to the
schema's smaller reservation maximum. The value is the reservation's first U bytes;
the remaining `R-U` bytes are slack. Payload bytes
contain no per-value headers.

Reservations concatenate from offset zero. A slot's start is the sum of preceding
reservation sizes; its exclusive end is `start+R`. The final end is the block's
**extent**. For example:

| Rank | Value | R | U | Reservation |
| --- | --- | --- | --- | --- |
| 0 | `hi` | 4 | 2 | `[0,4)` |
| 1 | empty | 0 | 0 | `[4,4)` — owns no bytes |
| 2 | `cat` | 5 | 3 | `[4,9)` |

The extent is 9, although only 5 bytes are used by values. Offset 8 belongs to rank 2's
slack; offset 9 has no owner. A live empty value still has a rank. Empty values can
also have nonzero reservations.

A **tombstone D** marks a retired rank. Retirement leaves its reservation in place,
so later starts and the extent remain unchanged. Retirement and a zero-size reservation
are independent. Packing can reclaim retired reservations; it does not remove their
ranks.

## What Intervals, StreamPack and the owner do

| Layer | Responsibility |
| --- | --- |
| **Intervals** | Borrowed ordered search and interval geometry, native kernels, sparse sources, carried ranges and external operation filters. It owns no compact header, bitmap or retirement policy. |
| **StreamPack** | Its compact geometry image and admission/decoding, tombstones and retirement, optional U storage, fused value reads, native packets and coordinated payload/geometry edits and transfer. |
| **Collection / owner** | Storage and schema identity, coherent retained versions, read/write authority, allocation growth, logical holders and remaps, sorting/deduplication policy and transaction publication. |

A StreamPack layout prepares the geometry and value rules together. One named view
binds the payload and metadata; callers do not attach a second geometry view.
The implementation reuses geometry bodies inside complete value operations. Sharing
those bodies does not require another allocation or per-value indirect call.

Logical ownership also does not dictate physical separation. Geometry fields and U
can share a grouped lengths window even though different modules define their meaning.
StreamPack reports its complete writes, including the geometry/control stores it
performs, under the StreamPack source identity.

If a consumer stores U in its own record or tuple plane, it can compose Intervals,
that plane and a payload itself. The stock StreamPack recipe instead either manages U
or derives `U=R`. Omitting U does not select an external length provider.

## Prepare a schema

Prepare one immutable `layout` from a `description` and share it among blocks with
that schema:

| Schema fact | Meaning |
| --- | --- |
| `maximum`, M | Maximum reservation size, 0–65535 bytes. |
| `byte_limit`, B | Maximum total extent, at most `256*M`; independent of the currently allocated payload span. |
| `family` | Geometry encoding: Sizes by default, Absolute Ends or Relative Ends. |
| `fixed_reservation` | Derive every R from this size; store no per-rank geometry. |
| `full` | Derive `U=R` and omit U. Every value write must exactly fill its reservation. |
| `immortal` | Omit tombstones; retirement is unavailable. |
| `lengths` | Grouped geometry/U by default; `placement::separated` selects planar storage where both are present. |

A schema omission is a promise, not a temporary compression decision. A `full` block
cannot accept a shorter value while silently forgetting its length. Fixed nonzero
reservations cannot be packed into variable sizes. Zero-size geometry needs neither
stored boundaries nor U. These omissions leave assigned count and any tombstones.

### Choose reservation geometry

All three encodings describe the same starts and reservation sizes:

| Encoding | Stored geometry | Main cost |
| --- | --- | --- |
| **Absolute Ends** | Absolute exclusive end per rank. | Cheap point access; the full endpoint directory is reserved up front. |
| **Sizes** | R per rank and one group-origin checkpoint per 16 ranks. | Narrow fields and local size changes; random starts need a local prefix sum. |
| **Relative Ends** | Group-local cumulative end per rank and the same checkpoints. | Direct local lookup; local ends can need wider fields than R. |

![The same reservation boundaries stored as absolute ends, sizes and relative ends. All three locate rank 18 at start 37 with size 5.](../intervals/images/encodings.svg)

There are `ceil(n/16)` meaningful checkpoints, including the first origin, and no
terminal checkpoint. The final extent is stored separately for variable checkpointed
geometry. [The Intervals guide](../intervals/usage.md) describes the borrowed source contract;
StreamPack owns the compact byte law here.

Preparation chooses the smallest u8/u16/u32 geometry field that can hold its bound.
Global ends/checkpoints use B, Sizes uses `min(M,B)`, and Relative Ends uses
`min(16M,B)`. U is chosen independently from `min(M,B)`, further tightened by fixed R
when present; it needs at most u16. A u32 relative end therefore does not force U to u32.
These field widths remain fixed as allocation or occupancy changes.

### Place geometry and used lengths

For checkpointed geometry with U, grouped placement stores 16 geometry fields followed
by 16 U fields for each group. Planar placement stores a full-capacity geometry prefix
followed by U groups. Geometry is always first; there is no second plane-order option.

Grouped is the default for complete value access. Planar can help U-only scans but pays
for all 256 geometry fields even at partial occupancy. With Absolute Ends or fixed
geometry, only U uses the lengths window. Without a geometry/U pair, new schemas
normalize placement to grouped. Legacy layouts retain their original placement laws.

## Bind storage and admit contents

A view borrows four disjoint byte windows, which may be spans of one owner allocation.
The **effect plane** number names each window in reported writes:

| Window / effect plane | Stored fields |
| --- | --- |
| Payload /0 | Concatenated initialized reservations. |
| Directory /1 | Optional 32 B tombstone bitmap, then 256 absolute ends or 16 origin checkpoints. Fixed/zero geometry omits the geometry array. |
| Lengths /2 | Grouped: 16 geometry fields then 16 U fields per group. Planar: all geometry capacity then assigned U groups. Absolute Ends/fixed use U only; omitted fields occupy no bytes. |
| Control /3 | u16 assigned count; variable checkpointed geometry adds u32 extent at byte 2. |

The directory reserves its full capacity. Lengths grow in initialized 16-rank groups;
unassigned lanes in the last group are zero and readable, but are not ranks. Mixed
geometry/U field widths can form 48 B or 96 B groups as well as 32 B or 64 B groups.
Lengths alignment follows the widest local field, not the group byte size. Use
`directory_bytes()`, `lengths_bytes(assigned)`, `control_bytes()` and
`lengths_alignment()` rather than copying layout formulas into an owner.

All multibyte fields are little endian. The schema has no padding or
tombstone/checkpoint-interleaving option. Payload extent includes live reservations,
retired reservations and slack. A sum of live used lengths cannot replace it.

The setup sequence separates placement from contents:

1. Prepare the immutable layout.
2. `view::bind` checks directory/control minimum sizes, lengths alignment, pointer
   ranges and window disjointness, including nonoverlap with the layout object.
   It does not inspect encoded values.
3. `initialize` constructs an empty block, resetting directory and control. It does
   not initialize unassigned payload or lengths capacity.
4. For external contents, use `view::validate()` instead. It checks geometry, count,
   extent, initialized metadata tails and `U <= R`, then checks extent against payload
   span size. Validation checks metadata/bounds, not the contents of value or slack bytes.

Successful checked mutations preserve content admission. The owner establishes that
every reservation is readable/initialized and that the chosen contents belong to its
selected version. B and the supplied payload span are independent bounds; writes must
fit both. A small span cannot narrow the schema's stored fields.

## Read values and physical ownership

| Operation | Result |
| --- | --- |
| `value(source,rank)` | Borrowed span of U bytes. Invalid or retired rank is an error; a live empty value succeeds. |
| `length(source,rank)` | U with the same rank/liveness checks. |
| `reverse(source,offset)` | Optional `byte_owner{rank,tombstone}` below extent, including slack and retired storage; otherwise no result. Zero-size slots own no bytes. |
| `overlap(source,low,high,only_live)` | Physical reservations overlapping `[low,high)`; retired storage participates unless `only_live=true`. |
| `range(source,first,count,sink)` | Live values in original rank order, carrying geometry across the requested rank interval. |
| `select(source,ranks,sink)` | Live values in requested order, preserving duplicate ranks. |
| `select(source,ranks,scratch,sink)` | The same result, paying once to decode all starts into caller scratch for that operation. |

`overlap` returns `intervals::rank_window{first,limit}`, including `first` and excluding
`limit`. The limit is one past the last candidate, and an empty answer is `{n,n}`.
Internal zero-size slots can remain; the live-only form can also contain internal
retired holes. This operation concerns reservations, not comparisons against stored
strings or keys.

`range` and `select` validate all requested coordinates before the first callback.
They skip retired ranks, whereas transfer below refuses a retired selection. Scratch
selection requires `ceil(n/16)*16` writable u32 starts, disjoint from encoded inputs,
rank selection, borrowed controls and callback state. No decoded state or callback is
retained after return. Value spans remain subject to the source's lifetime and mutation
rules.

## Mutate values and pack reservations

Checked mutations take an effects sink for issued writes. Before entry, the owner
supplies exclusive authority, admitted contents and enough effect capacity. Every
checked refusal preserves supplied bytes and effect state.

| Command | Success semantics |
| --- | --- |
| `append(source,input,R,effects)` | Assign the next rank, copy input, zero new slack, write U/geometry, clear D and update count/extent. Refuse incompatible size or insufficient ranks, lengths, payload or schema extent. |
| `replace(source,rank,input,effects)` | Keep R, rank and start. Write the new U-byte value and optional U field. Full schemas require exact fill. Remaining slack is not automatically cleared. |
| `retire(source,rank,effects)` | Require tombstones and idempotently issue the containing bitmap byte. Leave R, payload and extent unchanged. |
| `soft_pack(source,shrink,effects)` | Preserve ranks and tombstones, move values together, and set retired R/U to zero. With `shrink=true`, reduce each live R to U; otherwise keep its reservation. |
| `transfer(source,ranks,destination,shrink,effects)` | Preflight a disjoint destination and every selected live rank, then construct the destination in the requested order, retaining duplicates. Optionally shrink R to U. |

Fixed geometry refuses packing that would change its reservation sizes. Soft packing
the example above after retiring rank 0 leaves three assigned ranks: rank 2 moves to
offset zero, with extent 5 when keeping live slack, or 3 when shrinking it. Rank 0 remains
retired and rank 1 remains a live empty value.

A reservation-changing logical replacement appends elsewhere; the owner retargets its
holder and retires the old rank as appropriate. StreamPack does not expose the historical compact geometry
boundary/resize commands as uncoordinated value edits. It must coordinate geometry with
payload movement and U.

Transfer supplies the mechanics for hard rewrites, format conversion and reordering.
Destination ranks are positions in the requested sequence. The owner chooses sorting,
deduplication and repartitioning, builds the remap, repairs logical holders and publishes
the replacement. Soft packing and transfer do not free allocation pages or erase all
bytes beyond the new extent.

## Effects, aliases and retained readers

Effects report actual issued byte stores, including preserved neighboring lanes and
count/extent writes. They are not read protection, byte differences or a logical redo
log. Records borrow the stable named destination view; an enclosing owner resolves
their source identity before it expires.

Conservative journal capacities are:

| Operation | Entries |
| --- | --- |
| Initialize | At most 2 |
| Append | `layout::append_effect_capacity()` |
| Replace / retire | At most 2 / 1 |
| Soft pack | `layout::pack_effect_capacity(count)` |
| Transfer | `2 + selected_count * destination.format().append_effect_capacity()` |

Exact append/replace admission can accept a smaller grant. Never assume optional
coalescing makes an insufficient grant safe. Hooks cannot allocate, fail, suspend or
invalidate admitted inputs between preflight and stores.

Mutable outputs must be disjoint from views/layout objects, selection, command/control
inputs and effect storage. Append/replace payload input may alias current payload;
both use memmove. Input overlap with metadata is rejected. Resolve such payload aliases
before relocating allocation growth. Transfer requires disjoint source/destination
windows and rejects stock journal overlap with source or rank selection before
initialization. Writable scratch also obeys its disjointness contract.

Keep the immutable layout and named view at stable addresses while accesses, phases or
effects borrow them. Read callbacks cannot mutate or rebind the source. Packing
invalidates decoded starts and physical descriptors; transfer creates new rank identity;
relocation invalidates pointers. The owner retains the old payload and all interpretation
metadata together for old readers. Owning backing alone does not authorize changing it.

## Compose complete consumers

`author::dispatch` resolves the schema once around a useful operation. Typed
`author::access` assumes admitted contents and ranks, except `get` checks rank/liveness.
`rebind` requires the exact same shared layout object. Trusted mutations additionally
require command, alias and effects-capacity proofs before entry.

`native::read16` returns sixteen offsets and used lengths in native registers plus
an original-rank live mask. It reuses geometry decoding and adds U where present.
Keep it with the consuming predicate/reduction to avoid an obligatory intermediate array.
`visit_value_metadata` reports the point value's geometry, U and liveness dependencies;
it does not cover every reverse/search read, protect backing or issue prefetches.

The temporary `author::value_encoder` carries geometry through a completely preflighted
append run and writes count/extent once at `finish`. It owns no memory or partial-failure
recovery, cannot copy/move/escape, and its in-progress source must not be observed.
Ordinary append and transfer share this body. Transfer uses bounded 2 KiB coordinate
scratch so source/destination dispatch stays additive across codecs. In-place packing
instead captures original geometry before rewriting it.

Explicit native execution is available for little-endian NEON, AVX2 and AVX-512.
The stored byte law is ISA-independent. SVE2 and arbitrary-bit field codecs are not
selected production recipes. CPS stages stay bounded; the owner decides any Loom wait.

## Portable descriptor and further reading

The portable eight-byte **version 3** descriptor uses byte 0 bits 6–7=`3`, bits
0–1=encoding 0/1/2, bit 2=planar, bit 3=full, bit 4=immortal and bit 5=fixed-present.
Bytes 1–2 hold M as u16, bytes 3–5 B as u24 and bytes 6–7 fixed R as u16, zero when
absent. New noncanonical flags are rejected. Version 2 still decodes with its original
carriers, placement, alignment and exact descriptor re-encoding. This descriptor is
distinct from the historical standalone Boundlist descriptor retained only for
compatibility controls. Convert by constructing/transferring
into a newly prepared layout; retagging bytes does not convert them.

[Owner integration](../integration.md) explains Loom/Orbital visibility, retention and
publication. The [current portfolio qualification](../../../workbench/spikes/boundlist/PORTFOLIO.md)
records codecs, memory and complete-consumer timings; the [original implementation
study](../../../workbench/spikes/streampack/IMPLEMENTATION.md) retains earlier controls.
