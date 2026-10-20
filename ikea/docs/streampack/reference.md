# StreamPack reference

[Boundlist](../boundlist/reference.md) owns the reservation metadata types and
kernels below StreamPack. StreamPack aliases those schema types and adds the
payload plane, its admission and coordinated operations. The stored representation
is shared. Use Boundlist directly for payload-free metadata or ordered-key routing;
use StreamPack for byte values.

This reference describes the native Direct/Width implementation. Execution
is available for little-endian NEON, AVX2 and AVX-512 targets; the physical byte law
is independent of those ISAs. SVE2 and arbitrary-bit field codecs are not selected
production recipes.

## Schema and representation

`description`, `layout`, `geometry`, `placement` and `slot_capacity` alias
[Boundlist's reservation recipe](../boundlist/reference.md#stored-representation).
That specification owns Direct/Width, the 256-rank capacity, G16 widths, optional
U/D, alignment, initialized padding and the eight-byte version-2 schema. There is
one encoding; StreamPack adds a payload window to those same metadata bytes.

| Window / effect plane | Meaning |
| --- | --- |
| Payload / 0 | Concatenated, initialized reservations; only the occupied extent is logically assigned. |
| Directory / 1 | Boundlist directory (standalone plane 0): tombstones and geometry. |
| Lengths / 2 | Boundlist lengths (standalone plane 1): optional R/U groups. |
| Control / 3 | Boundlist control (standalone plane 2): count and any stored extent. |

All four supplied windows are disjoint spans, even when they share one allocation.
Metadata placement follows the linked recipe. The schema's `byte_limit` and the
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
