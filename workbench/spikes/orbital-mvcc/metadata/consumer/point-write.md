# Value/tag point command

[point_write.h](point_write.h) / [point_write.cpp](point_write.cpp) is a separate
spike control for this fixed-row driver's update semantics: change value/tag,
preserve key/next already present in the initialized exact-basis clone. The
generic full-record `Update`, per-row mutation and [Shape write control](shape-write.md)
retain their full next/value/tag behavior. The two existing Ikea point mutation
kernels and effect walk are reused.

## Composition and state

The command contains key/value/tag, measured as 16 B on ARM and x86;
64 commands occupy 1024 B, versus 1536 B for full Updates. The final-place Tuple
law owns layout (392 B), two value/tag writer controls (336 B each) and an 8 B
borrowed actual Shape association: measured 1072 B, optional slot 1080 B. Its
borrowed proof remains 8 B. Series reuses the existing admitted four-Part proof
(8 B), with no runtime writer plans. The Shape, read law, count, backing owners,
construction/conversion Schemas and allocator/catalog records remain separately
accounted for. These sizes do not describe concurrent stack peaks or whole owners.

Cold Tuple preparation proves the actual semantic Shape and full canonical
layout, then compares every observable supplied public writer control with
canonical value/tag maps 16–18 and 19–22. The law is noncopyable/nonmovable,
constructed into empty final storage only after validation. Schema and former
preparation descriptors may end; stationary law and actual Shape must stay live.

Apply admits fresh exact clone count/ordered IDs/extents/null/address arithmetic,
alignment, cross-Part exclusion and all metadata/counter/input aliases. Every
Part is checked, including untouched key/next: counter publication must not
corrupt one of those bodies. The owner supplies initialized exclusive storage,
exact basis association and uninterrupted lifetime; Series extent alone cannot
authenticate counts within a final tile.

One key search per command on the clone resolves internal positions. Offered
value-before-key error order, explicit 20-bit width checks, whole-batch writer
acceptance and effect capacity all precede stores. Duplicates apply in offered
order and same-value commands still issue stores. Refusal preserves every clone
byte and both primitive counters. No unchecked-position interface is exposed.
Effects resolve against named stationary value/tag views before they close.

The authored [integration patch](point-write-integration.patch) removes the
source-record lookup from native and mapped LMDB treatments. Global object/kind/
value validation remains before selection or cloning, and object groups retain
their existing order. Clone-first key admission intentionally accepts capacity
before absence and wasted allocation/copy work on refused keys. Failed updates
publish no state or Outcomes; earlier private object work is discarded. Existing
full clone/capture/publication and old-reader ownership remain with their owners.

Mapped source admission checks exact count × 23 extent, null storage and address
overflow before reservation/memcpy. The transaction supplies encoding association
and protects the readonly basis through publication. Destination write admission
does not establish source bounds. This changes no Engine/container/shared-Part
policy. Architecture owns integration and Tooling owns matched timing.

## Qualification

Frozen cut1 passes pinned Clang 21.1.8 native AArch64 and ASan/UBSan/LSan with
always-on stack-use-after-return checking. After transient Schema/descriptors
die: 20 real Allocation-backed captures sharing two Shapes across counts
0/1/2/7/8/9/63/64/65/129; 72 batches / 2376 commands; 277 exact-error refusals
(53 malformed); 13 cold-law refusals; 626400 independent wire bits / 13920
fields; 359 unchanged-old-backing checks. Cleanup closes physical allocations
and metadata at zero charge. The diagnostic's PMR peak (723 B) excludes stack
Schemas and the final-place law; it is not whole-owner usage.

All stored next values are distinct and nonzero. Independent complete wire
images change only value/tag, preserving Tuple spare nibbles, Series inactive
lanes and complete key/next Parts. Duplicate different commands establish
last-wins behavior; same-value batches still issue stores. Both offered mixed
error orders, late missing keys, overwidth and bit24-only values refuse before
stores/counter commit. Concrete cost/probe objects inside the untouched next
Part, full law/Shape aliases and malformed placement are refused. Mapped
admission has 67 calls, including 47 exact bounds refusals and opaque exact spans.
It admits geometry without reading payload; transaction encoding association
remains a caller fact.

Issued effects are independently asserted: **7 B / 1 span per Tuple command**
(value bytes 16–18 plus tag 19–22); **10 B per Series command** (value body2 +
preserving tail4, tag4), with 3 spans ordinarily or 2 at lane7, where the value
body and tail coalesce. Two bindings occur per clone/batch and two encoded
values per command. Named effect journals occupy 64 B for Tuple /96 B for Series.
These are logical issued envelopes, not hardware traffic or changed-byte counts.
The unchanged full-update reference supplies a secondary byte comparison only;
its three-field effects are not the expected narrow effects.

Ignored evidence lives under `build/orbital-mvcc/metadata-consumer`:
`point-write-cut-1` (1152 files, map SHA256
`552085781a69997db9d9f2582e55d0da7a4700aadbe8e3c365dfe9cadad1ad14`),
`point-write-1-{native,sanitize}/receipt.json` and
`point-write-1-codegen/receipt.json`. Native/SAN receipts record exact commands,
source/library/object/executable/output hashes and 118 matched lower-library
source hashes; fresh data/Shape/directory/read/full-write/point-write/check TUs
compile separately. Codegen's earlier source capture has identical point header/
implementation bytes. Independent source and emitted-frame audits and their
hashes are collected in `point-write-1-evidence.json`.

Ordinary ARM/x86 separate-TU codegen measures Command16, law1072, optional1080,
proofs8 and status8. All four public status endpoints return in registers;
there is no new public owning return, eager global Schema or per-comparison
external ReadData call. Cold preparation places layout392 and two writer336
controls into final storage before engaging it. Internal expected-plan owning
returns/copies remain. Reused Series Shape admission lives in the full-write
control; it is not included in this point object's code total.

| Emitted body | ARM actual SP allocation | x86 maximum CFA |
| --- | ---: | ---: |
| Cold Tuple preparation | 1328 | 1312 |
| Tuple apply | 832 | 928 |
| Series apply | 1168 | 1168 |
| Private clone admission | 240 | 224 |
| Mapped source admission | 0 | 8 (entry return address only) |
| Series20 binder | 144 | 192 |
| Series20 effect walk | 0 | 24 |

Frames include ARM saves/SP subtraction and x86 outgoing argument adjustments
(Tuple base896 +32 outgoing). No red-zone accesses were emitted. These are
individual frames, not concurrent call peaks. The one unlinked point object
contains 16432 text /1338 rodata bytes on ARM, 14432 /1454 on x86. External Shape/
read/Series admission/native/runtime bodies, owner code, unwind and final link
deduplication are excluded. The full-write comparator contains additional lookup/
factory endpoints, so object totals have different surfaces.

These checks qualify actual capture/Allocation-backed clones and in-memory
mapped spans. Store/Scope publication, real LMDB transaction behavior and
matched whole-owner performance remain integration obligations. Only sizeof
runs under x86 qemu; no x86 mutation or timed gain is claimed here. Resident
fixture Schema is still 10176 B ARM /15296 B x86, separately charged for generic
construction, conversion and reads. This control removes no clone Parts.
