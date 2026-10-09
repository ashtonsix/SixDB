# Actual Shape write control

[shape_write.h](shape_write.h) / [shape_write.cpp](shape_write.cpp) is an isolated
control for Schema-free fixed-record writes. The released
[read adapter](shape-read.md) and [checked batch control](write-batch.md) stay as
comparators. This changes consumer preparation and admission; it uses the same
Ikea TuplePack and SeriesPack point mutation kernels and issued-effect walk.

## Owned and borrowed state

The sealed Tuple proof is **8 bytes**, borrowing one stationary `TupleLaw`.
That law owns the existing public layout (392 B) and three writer controls (336 B each),
plus an 8-byte association with the actual admitted Shape: **1408 bytes**.
`optional<TupleLaw>` is 1416 bytes. The owner cannot move or copy; preparation
validates before emplacement into empty final storage and returns status (8 B).
Schema may end after preparation. No Schema, map or former endpoint address
is retained. The Shape remains separately protected and charged: the current
Tuple Shape requests 227 bytes, including geometry, ReadLaw (64 B) and descriptor (75 B).

The Series proof is 8 bytes borrowing the actual four-Part Shape, with no
runtime writer plans. Fixed typed geometry 64/64/20/32 follows cold semantic
Program and ordered Part-ID checks. The current Series Shape requests 184
bytes. Neither proof protects backing or supplies private mutation authority.
Realization/count, address slots, physical owners, allocator controls and
history remain with Orbital; these bytes have not disappeared.

Cold Tuple admission checks the actual read Shape and canonical layout, then
compares every observable supplied writer control against independently
prepared canonical maps 8–15, 16–18 and 19–22. Unit size 23 alone is insufficient: a key
writer substituted for next has the same unit and width but writes wrong bytes.
Memberwise comparisons include word selection, transfer, acceptance, read and
issued envelopes, ordering and footprints; padding is never compared.

## Complete calls

Protected-selection lookup checks absence and exact Shape identity, then reads
keys inline through the Shape's compact law or the typed Series decoder. Only
a hit publishes caller-owned row and Record (16 B) outputs. Outputs must be disjoint
from backing, live metadata and each other. Mapped Tuple lookup also admits the
new exact count/extent/null/address arithmetic; its enclosing transaction owns
encoding association and physical protection. No unchecked position enters
the mutation interface. Source and private-clone key searches both remain.

Apply admits the caller's actual count and newly supplied ordered IDs, exact
extents, null/address overflow, alignment, cross-Part exclusion and aliases
against the entire Shape allocation, whole law, input, Part array and counters.
The owner supplies initialized exclusive private storage, its exact basis
count and uninterrupted lifetime. Two Series counts within one final tile
can have the same extent; bytes alone cannot authenticate that association.
The adaptation checks private clone count against its protected Selection. Shared immutable proof replaces parent/Part descriptor parsing and
reader/map/plan re-admission; it does not replace fresh placement checks.

All commands' values and keys are admitted in offered order, followed by writer
acceptance and effect capacity, before any store. Refusal preserves bytes and
counters. The 20-bit value still needs an explicit width check: the three-byte writer's
invalid mask cannot detect bits beyond its packet map. Duplicates apply in
offered order; same-record commands still issue preserving stores. Named views
and endpoints stay stationary until journal sources are resolved.

[shape-write-integration.patch](shape-write-integration.patch) is an authored
Material/backend/mapped-LMDB adaptation for the owning Architecture task. It
accounts for live law, optional slots and containing records separately; these
are overlapping accounting views, not additive allocations. Existing fixture
Schema, write Shapes, construction/conversion and generic read paths remain
charged. Integration and matched whole-owner timing belong to their owners.

## Qualification

Frozen cut2 passes native AArch64 and ASan/UBSan/LSan with always-on stack
use-after-return checking. Its 22 real Allocation-backed captures share Shapes
across counts 0/1/2/7/8/9/63/64/65/129. After Schema and preparation descriptors
die: 72 batches / 2376 updates, 275 exact-error refusals (51 malformed), 13
cold-law refusals, 626760 independently checked wire bits, 818 selected and
393 mapped lookups, and 368 unchanged-old-backing checks pass. Tests include
spare/residual preservation, wrong key writer for next, whole law/Shape overlap
and bit24-only invalid values. Physical close and metadata cleanup end at zero
charge. Issued counters match the unchanged per-row control; they are not
hardware measurements or an independent effect oracle.

Ignored evidence: `build/orbital-mvcc/metadata-consumer/shape-write-cut-2`,
map SHA256 `884c1b1cea86624e12e83d74031840bcf89de7059f3a12a2525795f7f2f66eba`;
`shape-write-2-{native,sanitize}/receipt.json` record exact compiler commands,
sources, reused lower-library provenance, fresh objects, executable and outputs.
Construction/reference use transient Schemas; resource peaks exclude their
stack state and the final-place law, which remain separately charged. These
checks exercise Allocation-backed protected captures and in-memory mapped
spans, not catalog publication or LMDB transactions.

`shape-write-1-codegen/receipt.json` records ordinary ABI and actual type sizes
with pinned Clang 21.1.8, assertions, independent TUs and no LTO on ARM and x86.
Cut1 and cut2 have identical header/implementation; cut2 adds only the bit24
check. The six public status results are 8 bytes in registers; Series proof
Result is 16 bytes in registers. No owning law return, global Schema or
per-comparison external ReadData call remains. Final-place preparation copies
layout and plans directly to the destination before engaging it.

| Emitted function | ARM SP frame | x86 maximum CFA |
| --- | ---: | ---: |
| Tuple preparation (cold) | 1296 | 1296 |
| Tuple apply | 928 | 1008 |
| Series apply | 1296 | 1296 |
| Shared clone admission helper | 240 | 224 |
| Selected Tuple lookup | 0 | 56 + 8 red-zone bytes |
| Selected Series lookup | 0 | 40 |
| Mapped Tuple lookup | 0 | 48 |

These are individual emitted frames, not concurrent call peaks. ARM includes
SP subtraction plus preindexed saves; x86 CFA includes the return address and
Tuple apply's temporary 32-byte outgoing argument area. The emitted Series20
mutation binder adds 144 ARM /192 x86 CFA while its caller is live. External
library/runtime frames are excluded. The one unlinked control object contains
22580 text /1653 rodata bytes on ARM, 18931 /1769 on x86, including cold
admission/comparator code and emitted local/inline helpers; external bodies,
link deduplication, unwind and owner code are excluded. No timed gain or
x86-executed mutation qualification is claimed.
