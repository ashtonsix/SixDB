# Actual shared-Shape complete reads

[shape_read.h](shape_read.h) connects a protected actual Orbital `Selection`
to the existing compact Tuple consumer. The read uses the count and Part0
address admitted by actual capture, and the canonical transfers proved by its
immutable shared Shape. Construction Schema and generated descriptors can
end before any read. This path supports the fixed one-Part Tuple law;
Series and distinct Shape identities refuse.

`TupleProof` is an **8-byte borrowed pointer**. Its cold factory checks the
actual Program, requested Part ID, one-Part geometry, canonical descriptor
and stored transfer controls. The factory result is sealed; the public header
offers no unchecked constructor. The selected `Selection` is also 8 bytes;
its captured count lives in the protected Realization. A standalone body
cursor plus explicit count still occupies 16 bytes. These are borrowing
packets, not complete owning Objects.

## Complete call and ownership

`read_proven` refuses absence or a different actual Shape before touching the
caller Outcome. It then uses capture-proven count/placement and specializes the
admitted canonical transfers. [shape_read_bridge.cpp](shape_read_bridge.cpp) is an
ordinary separate translation unit using the caller's authoritative
[private Shape header](../orbital/shape/shape_private.h). It does not mirror
layout offsets or include another implementation file. The private
[cursor factory](shape_read_private.h) skips descriptor and transfer checks
already established by Shape admission; payload loads and chain execution
remain the existing Ikea fixed-transfer kernels and compact operations.

`read_checked` is the control: it uses public outlined Shape/Selection
accessors and the existing `qualify_cursor` on each call. It interprets the stored
law; the proven endpoints specialize canonical transfers whose equality to that
law was established at admission. Both preserve the same complete chain semantics.
The control reconstructs the body extent from capture's admitted geometry/count; it does not obtain an
independent fresh backing measurement.

A live owner or catalog Scope must protect the Selection, its address slot,
Shape and immutable backing throughout the call. `TupleProof` independently
requires a live immutable Shape; retaining mapped payload alone cannot protect
the proof pointer against destruction/reuse. Neither packet owns that lifetime
or carries an Object/version label. Keys must be sorted, and Outcome must be
disjoint from encoded bodies and live metadata.

The mapped pair accepts explicit count and body, so the same consumer
improvement can be applied to LMDB. Both mapped paths check the new body's
exact extent, null/overflow before writing Outcome. An enclosing transaction
must associate that mapping/count with the admitted encoding and protect it;
payload bytes cannot authenticate format or Object/version identity. The
qualification below uses borrowed real bodies and shifted mappings, not an
LMDB benchmark.

## Original captured ABI and emitted code

The sizes and emitted-code observations in this section describe the original
`shape-read-cut-1` capture, before canonical specialization and later physical
ownership changes. The selected [canonical-read comparison](../../performance/FEEDBACK.md#canonical-selected-read-specialization)
owns the subsequent performance and code-generation evidence; this older table
does not describe the current proven endpoints.

Shared state remains charged. In this original cut, one Tuple Shape requested
227 bytes (`Shape56 + Geometry32 + ReadLaw64 + descriptor75`). The Realization
header was 24 bytes, followed by actual address/owner storage; independent shared
ownership added 24 bytes per Part. Allocator rounding/control blocks,
dependencies, history, catalog Scope and owner closure remained additional.
No read created another retained Schema, law, descriptor or payload scratch.

[shape_read_sizes.cpp](shape_read_sizes.cpp) measures the actual records on
both targets: proof/Selection/View/Cursor8, checked proof/View result16,
read controls/Part16, ReadLaw64, Outcome24 and status result8. The transient
Schema remains 10,176 bytes on ARM and 15,296 on x86; it is absent from reads.
The factory returns its ordinary 16-byte result in registers, and all four
read endpoints return ordinary register status8. The mapped signature's last
Outcome reference arrives on the x86 stack because its span is split into
pointer/count. No custom calling convention or extra global Schema exists.

In that capture, separate-TU LLVM and assembly confirmed that the proven endpoints
called the same emitted `read_chain_into` as the checked control, using the stored runtime
transfers. They make no outlined Selection/Shape/law calls, repeat no
descriptor/transfer qualification, and allocate no owner or preparation
state. Part0 address selection folds without an owner-profile branch. The
selected endpoint keeps its absence/exact-Shape checks; the mapped endpoint
keeps count/extent/null/address-overflow admission. The law's location still
uses the actual stored Part count; the compiler does not replace it with a
canonical static law.

| Function | ARM reserved bytes | x86 CFA bytes |
| --- | ---: | ---: |
| `admit_tuple_shape` | 80 | 64 |
| `read_checked` | 80 | 96 |
| `read_mapped_checked` | 80 | 80 |
| `read_proven` / `read_mapped_proven` | 16 each | 16 each |
| Emitted shared `read_chain_into` | 0 | 56 |
| Emitted `qualify_cursor` | 48 | 8 |
| Emitted ARM `qualify_part` | 0 | Inlined into qualifier |

ARM reservations include saved frame/link registers. x86 CFA includes the
entry return-address word: both proven wrappers reserve only eight additional
bytes. The x86 chain helper also uses four red-zone bytes below its saved
registers. These are individual frames, not complete concurrent stack peaks;
the ARM checked path calls `read_mapped_checked` with both 80-byte frames
live, whereas x86 inlines that wrapper. The checked path additionally calls
external Shape/Selection accessors.
Cold exception termination helpers remain in both objects.

The two unlinked adapter objects sum to **2,720/2,787 text bytes** on ARM/x86,
and **75 rodata bytes** on both. This includes both paths, cold factory,
emitted qualifiers/chain/termination helpers, with duplicate COMDAT bodies
still counted. External Shape/Ikea/runtime bodies, unwind sections and
link-time deduplication are excluded. The totals do not establish a timed
gain or the incremental cost of one selected path.

## Original captured independent qualification

[shape_read_check.cpp](shape_read_check.cpp) builds and publishes through
actual `ShapeStore16`, with actual charged payload and shared-owner control
blocks. All construction Schemas and temporary descriptor buffers end before
reads. One admitted Tuple Shape is reused across 16 captures with counts
0, 1, 7, 8, 9, 64, 65 and 129, both finite and cyclic chains. Independent
per-bit decoding checks logical fields and nonzero Tuple spare bits; linear
logical lookup and an explicit digest recurrence supply complete Outcomes.

The check also converts a real published four-Part column source through
`convert_into`, independently compares its logical fields with the Tuple
replacement, and adopts it through a real ConversionTarget. It preserves the
original and adopted alternative under an old Scope after later publication
and drain. Program/count equivalence alone is not used as proof of content.

AArch64 native and ASan/UBSan/LSan/always-use-after-return runs pass with
assertions enabled:

- 19 captures, 18 publications, four Shape factories and 20 transient Schemas.
- 32,936 selected and 32,936 mapped complete calls, including empty, point,
  zero-step, long finite and cyclic chains; 16 unaligned mapping calls.
- 126 exact-error read refusals with seeded Outcome preserved, plus four
  factory refusals: absence, wrong identity/format/Program/Part, bad mapping
  lengths, null and count/address overflow.
- 3,812 logical fields and 171,540 wire bits independently checked; nine
  old-Scope checks and 7,704 adopted-alternative selected calls.
- Charged metadata, payload and owner/control-block storage drains to zero.

These are correctness and composition results, with no timing claim or full
allocation/MVCC candidate qualification. Runtime owner coverage here is
independent shared capture. Whole-container storage and paired
native/LMDB timings belong to the enclosing composition experiment.

The original behavior capture remains local at
`build/orbital-mvcc/metadata-consumer/shape-read-cut-1`, source-map SHA256
`99fc684bba10a7b3960fef700bb8dc14db5c83097762b7af436f13d29bc147c4`.
`shape-read-1-native/receipt.json` and `shape-read-1-sanitize/receipt.json`
record fresh data/Shape/directory/adapter/check compilation, exact commands,
source/object/binary/output hashes and sanitizer settings. Lower libraries
reuse qualified cut3 objects only after all 118 source hashes and linked
library hashes match. x86 evidence is limited to compiler ABI/code and size
reporting; it is not executed read qualification.

These local paths are not a shared recovery archive. The separately retained
[early cut1/cut3 codegen receipts](evidence/early-codegen-20261009/README.md)
identify their own older consumer experiments; they do not recover this Shape
capture, its native/sanitizer executions or its codegen outputs. Exact replay of
this section still requires the original local capture and dependencies.

[shape_read_probe.py](shape_read_probe.py) records ordinary signatures,
actual compiler dependencies, separate LLVM/assembly/objects, section sizes,
copied probe inputs and source/output hashes in
`shape-read-1-codegen/receipt.json`. Only sizeof executables run for this
report; the x86 one runs under qemu, never for performance.
