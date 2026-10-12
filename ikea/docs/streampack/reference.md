# StreamPack reference

This reference describes the initial native Direct/Width implementation. Execution
is available for little-endian NEON, AVX2 and AVX-512 targets; the physical byte law
is independent of those ISAs. SVE2 and arbitrary-bit field codecs are not selected
production recipes.

## Schema and representation

`description` contains family, length placement, rank capacity, maximum reservation,
block byte limit, optional fixed reservation, full-reservation flag and immortal
flag. Capacity is 128/256/512. The byte limit is at most capacity × maximum and can
be zero. When omitted in aggregate construction, it defaults to the selected
capacity × maximum. A fixed reservation cannot exceed maximum. Values and reservations can
both be zero; a retired zero-width slot still has a rank and tombstone.

All multibyte fields are little endian. Let N be rank capacity, G = ceil(N/16),
W be one byte when the reservation upper bound is ≤255, otherwise two, and O be
one/two/four bytes for a block byte limit ≤255/≤65535/larger. Fixed reservations
supply the tighter bound for W. D occupies N/8 bytes when tombstones are present.
Within D, rank r is bit r%8 of byte r/8; one means retired.

| Window / effect plane | Stored fields |
| --- | --- |
| Payload / 0 | Concatenated, initialized reservations; only the occupied extent is logically assigned. |
| Directory / 1 | D first, followed by N exclusive endpoints (Direct) or G group-start checkpoints (Width), each O bytes. No leading or terminal sentinel. Fixed/zero geometry omits these fields. |
| Lengths / 2 | Direct: U groups only. Width grouped: 16 R then optional 16 U, repeated. Width separated: full-capacity R plane then a U plane. Fixed geometry: U groups only. Omitted U has no storage. |
| Control / 3 | Assigned count as u16. Variable Width adds u32 occupied extent immediately at byte 2; Direct and fixed geometry derive extent. |

The directory is reserved at full capacity. `lengths_bytes(assigned)` includes
whole initialized groups, so prefix/native reads never overread a partial final
group. `lengths_bytes()` gives full capacity. Grouped Width requires its group-size
alignment (16/32/64 bytes); other recipes require W-byte alignment. Directory and
control use bounded byte loads and need no additional alignment. The API does not
require windows to be separately allocated or separately published Parts.

Unassigned tail R/U fields in the last assigned group are zero. Unassigned
Direct endpoints and future checkpoints are initialized by construction but have
no semantic value. A checkpoint is the sum of reservations before its group.
Positive reservations are disjoint. Used lengths never exceed reservations.
Occupied extent is the sum of all assigned reservations, retired or live.

`layout::encode()` produces exactly 16 bytes: version 1, family (Direct=0/Width=1),
placement (grouped=0/separated=1), flags (full bit 0, immortal bit 1), capacity u16,
maximum u16, byte limit u32, fixed reservation u32 (`0xffffffff` means variable).
Offsets are respectively 0,1,2,3,4,6,8,12. Unknown enums, flag bits, versions and
invalid bounds are rejected. Native layout objects and pointers are never the wire
format. The owner persists block schema identity and the four windows coherently.

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
alias the current payload for append/replace; these use memmove before clearing
new reservation slack. Metadata/input overlap is rejected. Such aliases must be
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
