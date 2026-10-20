# StreamPack reference

StreamPack composes [Boundlist geometry](../boundlist/reference.md) with byte
values and an optional used-length companion. It owns value admission, native
value packets, mutation, packing and selected transfer. Geometry does not imply
used length; one StreamPack view binds the complete coherent interpretation.

This reference describes the native Direct/Width implementation. Execution
is available for little-endian NEON, AVX2 and AVX-512 targets; the physical byte law
is independent of those ISAs. SVE2 and arbitrary-bit field codecs are not selected
production recipes.

## Schema and representation

`geometry` and `slot_capacity` use Boundlist's geometry families and 256-rank
capacity. StreamPack owns `description`, `layout` and `placement`.
`description` contains family, R/U placement, maximum,
block byte limit, optional fixed reservation, full and immortal facts.

R is reservation, U is used length. `full` omits U and requires value length == R;
it does not mean an arbitrary shorter value is accepted without recording its
length. `immortal` omits D, fixed reservations derive geometry, and zero geometry
needs no R/U. W is one byte for upper bound <=255, otherwise two; fixed R provides
a tighter bound. O is one/two/four bytes for total limit <=255/<=65535/larger.
All multibyte fields are little endian.

| Window / effect plane | Stored fields |
| --- | --- |
| Payload / 0 | Initialized concatenated reservations. |
| Directory / 1 | Optional 32 B tombstones first, then Direct's 256 exclusive ends or Width's sixteen origin checkpoints, O bytes each. Fixed/zero geometry omits boundaries. |
| Lengths / 2 | Width grouped: 16 R then optional 16 U per group; separated: full-capacity R then U. Direct/fixed: U only. Absent U has no bytes. |
| Control / 3 | Count u16; variable Width adds u32 extent at byte 2. Direct/fixed derive extent. |

The directory remains full capacity. Lengths grow by initialized complete G16
groups; inactive R/U tails are zero. Grouped Width requires group-size alignment
(16/32/64 B), other recipes W-byte alignment. The placement choice has no physical
R/U interleaving effect when U is absent, and is ignored by Direct/fixed geometry.
There is no tombstone/checkpoint interleaving or padding switch.

`layout::encode()` preserves the existing eight-byte **version-2** value schema:
byte 0 has version 2 in bits 5–7, Width bit 0, separated bit 1, full bit 2,
immortal bit 3 and fixed-present bit 4. Bytes 1–2 hold maximum u16, 3–5 total
limit u24, 6–7 fixed R u16 (zero when absent). Omission/maximum/limit laws are
checked at preparation/decode. StreamPack bytes are unchanged by the ownership
split; Boundlist's standalone geometry-only schema is version 3.

All four supplied windows are disjoint spans, even when they share one allocation.
Metadata placement follows this value recipe. The schema's `byte_limit` and the
payload span are independent bounds: writes must fit both. A smaller allocation
cannot implicitly narrow a shared schema's endpoint/checkpoint fields. Occupied
extent includes live reservations, retired reservations and slack. The owner
persists schema identity and all four windows as one coherent interpretation.

## Admission, lifetime and commands

`bind` checks physical extents, alignment, overflow and window disjointness without
examining encoded values. Keep its immutable layout and named view alive at stable
addresses. `initialize` resets directory and control for an empty block; it does
not initialize unassigned payload/length capacity. `validate` checks complete
assigned contents, checkpoint/end laws, used bounds, tail padding and coherent
count/extent against the bound windows. Validating content does not grant ownership
or select a version. Checked mutation preserves content admission.

The owner keeps mutable outputs disjoint from views, layout objects, selection,
command/control inputs and effect storage. It holds exclusive authority over every
issued byte, including bytes whose sibling bits are preserved. Payload input may
alias the current payload for append/replace. Both use memmove; append clears new
reservation slack after copying. Metadata/input overlap is rejected. Such aliases must be
resolved before any relocating owner growth. Stock journal overlap with transfer
source/selection is rejected before initialization.

Read callbacks may consume values but cannot mutate/rebind the source during a
traversal. Scratch is writable, sufficiently large, and disjoint from source,
selection, borrowed controls and other callback state. Typed `author::access`
requires admitted ranks/contents except its `get` wrapper; `rebind` requires the
exact same shared layout object. Trusted author mutations additionally require
pre-admitted command/alias/effect capacity. No hook can invalidate these proofs,
fail or suspend between preflight and stores.

| Command | Success and refusal semantics |
| --- | --- |
| value / length | Invalid rank or retired value is an error; a live empty value succeeds. |
| reverse | An offset below extent maps to one reservation owner, including slack and retired storage. Otherwise no result. |
| overlap | Physical reservations overlapping `[low,high)`, including retired storage unless `only_live` is requested; internal zero/dead holes can remain. |
| range / select | Validate coordinates before any callback; deliver only live values with original ranks. Selection preserves order and duplicates. |
| append | Assign the next rank, initialize reservation including slack, record U and geometry, clear D, publish new count/extent through effects. Refuse insufficient rank/length/body capacity or incompatible value/reservation. |
| replace | Live rank and unchanged reservation; full schemas require exact fill. Stores input and optional U; no automatic slack clearing. |
| retire | Requires tombstones. Idempotently issues the containing bitmap byte. |
| soft_pack | Rank preserving; clears retired reservation/used widths; optionally shrinks live slack. Fixed geometry refuses a change it cannot express. |
| transfer | Preflight all selected live values and disjoint destination; initialize and append in requested order. Owner handles remapping/publication. |

All checked mutation refusals leave supplied bytes and effect state unchanged.
Effects describe issued writes, not byte differences or a logical redo log.
`initialize` needs at most two entries, append at most `append_effect_capacity()`,
replace at most two and retire one. `pack_effect_capacity(count)` and
`2 + selected_count * append_effect_capacity()` conservatively bound packing and
transfer. Exact append/replace admission can accept smaller capacity. Coalescing
is optional and never assumed to make an insufficient grant safe.

Appending refreshes coherent count/extent in caller storage. Retiring leaves
geometry unchanged. Packing invalidates decoded starts and physical descriptors;
hard transfer changes representation/rank identity. Growth invalidates pointers
if storage moves. The owner retains the old coherent interpretation for old readers
and publishes logical holder changes and summaries separately from physical damage.
