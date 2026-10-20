# StreamPack reference

StreamPack composes [Boundlist geometry](../boundlist/reference.md) with byte values and
an optional used-length companion. It owns value admission, native value packets,
mutation, packing and selected transfer. Geometry does not imply used length; one
StreamPack view binds the complete coherent interpretation.

Execution is available for little-endian NEON, AVX2 and AVX-512 targets; the physical
byte law is independent of those ISAs. SVE2 and arbitrary-bit field codecs are not
selected production recipes.

## Schema and representation

A block assigns up to 256 ranks. StreamPack's `description` adds `placement`,
`byte_limit`, optional `fixed_reservation`, `full` and `immortal` to a chosen Boundlist
geometry family. The canonical families are **Absolute Ends**, **Sizes** and **Relative
Ends**; the [geometry tour](../boundlist/reference.md#three-geometry-encodings) explains
their costs and arithmetic. The default remains Sizes with grouped U.

R is reservation size and U is used length. `full` omits U and requires value length==R;
it does not accept shorter values without recording their lengths. `immortal` omits
retirement D; fixed reservations derive geometry. Zero geometry needs neither stored
geometry nor U. Schema maximum M and total byte limit B are independent of allocation
size. All multibyte fields are little endian.

Geometry carriers follow Boundlist: global coordinates use carrier(B), sizes use
carrier(min(M,B)), and relative ends use carrier(min(16M,B)). U independently uses
carrier(min(M,B)), tightened by fixed R when present. Each carrier is the smallest
u8/u16/u32 proving its bound; U needs at most u16. A Relative Ends u32 field therefore
does **not** force U to u32. These widths remain fixed as the block grows.

| Window / effect plane | Stored fields |
| --- | --- |
| Payload /0 | Initialized concatenated reservations. |
| Directory /1 | Optional 32 B D first, then 256 absolute ends or 16 origin checkpoints. Fixed/zero geometry omits the geometry array. |
| Lengths /2 | Grouped: 16 geometry fields followed by 16 U fields per group. Planar: full-capacity geometry prefix followed by assigned U groups. Absolute Ends/fixed use U only; absent U has no bytes. |
| Control /3 | u16 count; variable checkpointed geometry adds u32 extent at byte 2. |

The directory reserves its full capacity. Lengths grow in initialized G16 groups;
inactive fields in the last assigned group are zero. With U present, grouped mixed
carriers naturally form 48 B / 96 B groups as well as 32 B / 64 B groups. Alignment
follows the widest actual local carrier, not the group's byte size. There is no padding
or tombstone/checkpoint-interleaving schema switch.

`placement::separated` chooses planar U for U-only scans. Planar storage is
**geometry-first**, with no second ordering flag. All geometry starts at byte zero of
lengths, keeping the common addressing path direct. The full-capacity prefix costs space
at partial occupancy, especially for wider Relative Ends; choose planar for a
demonstrated U-only workload. Placement is normalized to grouped when there is no
geometry/U pair to interleave. Use `layout`'s extent/alignment methods rather than
duplicating formulas in an owner. Legacy descriptors retain their original carrier and
group-alignment laws.

The portable eight-byte **version 3** value descriptor uses byte 0 bits 6–7=`3`, bits
0–1=encoding 0/1/2, bit 2=planar, bit 3=full, bit 4=immortal and bit 5=fixed-present.
Bytes 1–2 hold maximum u16, bytes 3–5 total limit u24 and bytes 6–7 fixed R u16 (zero
when absent). Decode rejects noncanonical new flags. Version 2 still decodes, preserving
its original carriers, placement, alignment and exact descriptor re-encoding.
Boundlist's standalone descriptor is distinct. Construct/transfer into a newly prepared
layout to convert; do not retag bytes.

All four supplied windows are disjoint spans, even when they share one allocation.
Metadata placement follows this value recipe. The schema's `byte_limit` and the payload
span are independent bounds: writes must fit both. A smaller allocation cannot
implicitly narrow a shared schema's endpoint/checkpoint fields. Occupied extent includes
live reservations, retired reservations and slack. The owner persists schema identity
and all four windows as one coherent interpretation.

## Admission, lifetime and commands

`bind` checks physical extents, alignment, overflow and window disjointness without
examining encoded values. Keep its immutable layout and named view alive at stable
addresses. `initialize` resets directory and control for an empty block; it does not
initialize unassigned payload/length capacity. `validate` checks complete assigned
contents, checkpoint/end laws, used bounds, tail padding and coherent count/extent
against the bound windows. Validating content does not grant ownership or select a
version. Checked mutation preserves content admission.

The owner keeps mutable outputs disjoint from views, layout objects, selection,
command/control inputs and effect storage. It holds exclusive authority over every
issued byte, including bytes whose sibling bits are preserved. Payload input may alias
the current payload for append/replace. Both use memmove; append clears new reservation
slack after copying. Metadata/input overlap is rejected. Such aliases must be resolved
before any relocating owner growth. Stock journal overlap with transfer source/selection
is rejected before initialization.

Read callbacks may consume values but cannot mutate/rebind the source during a
traversal. Scratch is writable, sufficiently large, and disjoint from source, selection,
borrowed controls and other callback state. Typed `author::access` requires admitted
ranks/contents except its `get` wrapper; `rebind` requires the exact same shared layout
object. Trusted author mutations additionally require pre-admitted command/alias/effect
capacity. No hook can invalidate these proofs, fail or suspend between preflight and
stores.

| Command | Success and refusal semantics |
| --- | --- |
| value / length | Invalid rank or retired value is an error; a live empty value succeeds. |
| reverse | An offset below extent maps to one reservation owner, including slack and retired storage. Otherwise no result. |
| overlap | Physical reservations overlapping `[low,high)`, including retired storage unless `only_live` is requested; internal zero/dead holes can remain. |
| range / select | Validate coordinates before any callback; deliver only live values with original ranks. Selection preserves order and duplicates. |
| append | Assign the next rank, initialize reservation including slack, record U and geometry, clear D, publish new count/extent in caller storage through effects. Refuse insufficient rank/length/body capacity or incompatible value/reservation. |
| replace | Live rank and unchanged reservation; full schemas require exact fill. Stores input and optional U; no automatic slack clearing. |
| retire | Requires tombstones. Idempotently issues the containing bitmap byte. |
| soft_pack | Rank preserving; clears retired reservation/used widths; optionally shrinks live slack. Fixed geometry refuses a change it cannot express. |
| transfer | Preflight all selected live values and disjoint destination; initialize and encode in requested order, carrying position/local ends and publishing controls once. Owner handles remapping/publication. |

All checked mutation refusals leave supplied bytes and effect state unchanged. Effects
describe issued writes, not byte differences or a logical redo log. `initialize` needs
at most two entries, append at most `append_effect_capacity()`, replace at most two and
retire one. `pack_effect_capacity(count)` and `2 + selected_count *
append_effect_capacity()` conservatively bound packing and transfer. Exact
append/replace admission can accept smaller capacity. Coalescing is optional and never
assumed to make an insufficient grant safe.

Appending refreshes coherent count/extent in caller storage. Retiring leaves geometry
unchanged. Packing invalidates decoded starts and physical descriptors; selected
transfer changes representation/rank identity. Growth invalidates pointers if storage
moves. The owner retains the old coherent interpretation for old readers and publishes
logical holder changes and summaries separately from physical damage.


The admitted `author::value_encoder` is a temporary cursor for a complete preflighted
append run. It carries geometry across slots and finishes count/extent once; it neither
owns memory nor supplies partial-failure recovery. It must not escape, copy or move, and
its in-progress source must not be observed before `finish`. Ordinary append and
selected transfer share this body. Transfer retains bounded 2 KiB coordinate scratch so
source/destination dispatch remains additive rather than instantiating every pair of
codecs. Packing instead composes the original-geometry walker and sequential geometry
encoder in place, capturing original coordinates before overwriting them.
