# Representation conversion over real Ikea Parts

This temporary Stage 2 fixture converts column records backed by SeriesPack
(one Part per column) to row records backed by TuplePack (one Part for all
records), and back. It exercises physical interpretation, changing Part count,
construction and actual Orbital allocation. It supplies no Engine/container
architecture or production Object/Views implementation.

`fixture.h` is the small integration seam. `Completed` owns an immutable
Representation. `describe()` borrows its logical Object/version labels, schema,
record count and Parts. Retain the completed owner through every access or wait.
Each Part carries readonly body bytes, initialized readable padding and owned
interpretation. `read_parts()` accepts the description with qualified body
spans supplied by the enclosing selected-access exercise; qualification and
read bindings occur inside one uninterrupted visit. No bound operation refers
to an expired local view or moved plan.

The converter returns a target only after all construction and immutable
capture succeed. Mutable Allocations are private during construction; completed
backing is retained with Holds and all Allocation handles are released. Fixture
encapsulation supplies immutability; a Hold itself supplies only physical
lifetime. The Pool and its Lane must survive all completed descriptors and
physical settlement. Failures discard the private target, leaving the source
available. This is not transaction-wide rollback of earlier kernel calls.

## Physical interpretation

The deliberately small unsigned schema supports 1–8 fields with widths
1, 7, 8, 9, 12, 20, 31, 32, 63 or 64. Unused `Record` slots have no meaning.
Conversions preserve every active field, record order and logical identity.
Part IDs are local to a descriptor; column and row Part IDs differ.

Each Series column uses Local geometry and eight-row tiles, with 16 head bits
for widths at least 20, eight for 9/12-bit fields, and none for narrower fields.
Payload and head planes share one Part but have separate aligned offsets and
strides with gaps. Its 88-byte interpretation is the existing 40-byte SeriesPack
descriptor followed by three little-endian `(offset, accessible extent)` u64
pairs. Partial final tiles include the initialized physical slack required by
the actual constructor. Prefix/gaps retain a diagnostic sentinel.

Tuple units concatenate fields' least-significant-first bits in schema order.
Fixture-owned code fragments split wide fields at physical byte boundaries;
the existing TuplePack layout/constructor/readers encode/decode them. The Part
interpretation is the ordinary versioned Tuple layout serialization; the owned
schema supplies field-to-fragment meaning. Final unit spare bits are zero.
Eight 64-bit fields reach the 64-byte unit limit; eight 63-bit fields exercise
more than 64 codes and the upper construction/read packet.

Empty records keep the same Part counts and complete interpretation, with empty
bodies and no zero-byte allocation. Ragged final tiles are supported. Unequal
column counts and incomplete/mismatched interpretation are rejected rather than
silently truncating data. Logical equality is compared field by field; bytes
from different encodings are never used as an equality test.

## Run locally

Use pinned Clang 21.1.8 on Linux, with separate ignored builds. From macOS:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-representation-conversion/run.py --mode all
orb -m ubuntu python3 workbench/spikes/orbital-representation-conversion/run.py --mode native --diagnostic
```

The opt-in CMake selection is `-DSIXDB_SPIKES=orbital-representation-conversion`.
The runner uses CPUs 2/3 for builds and CPU 2 for execution. It records commands,
source hashes, binary identity and JSON output under
`build/ikea-representation-conversion/{native,sanitize}/receipt/`. Sanitizer mode
instruments every project TU reached by the check with ASan/UBSan, leak checks
and always-on use-after-return instrumentation. It refuses a changed input
snapshot. It runs no remote jobs and modifies no production interface.

`check.cpp` owns the independent record/bit oracles and boundary/failure checks.
`bench.cpp` is a bounded repeated-buffer local diagnostic; its output states
clock scope and physical settlement separately. Selected findings belong in
`FINDINGS.md`; full receipts remain in ignored storage.

## Costs and limits

`Costs` counts logical values decoded/constructed, qualification bindings and
constructor calls, actual issued byte coverage/spans, actual allocation body
requests and initialized padding, plus completed columns or row units. These
are logical work counters, not observed memory-bus traffic. Columns include
owned stride gaps/prefixes in body size. Diagnostic sentinel initialization is
counted separately and is included in local conversion timing.

Conversion currently stages fixed eight-u64 Records, then a column input buffer
or one Tuple construction packet. `scratch_bytes` estimates requested staging
and effect storage at the largest phase, including the retained Record vector
during target construction. It excludes native plan/stack frames and allocator
overhead. `descriptor_bytes` accounts retained descriptor storage and vector
capacities; it excludes shared_ptr control blocks, allocator records and malloc
overhead. Arena charge/Pool statistics and final zero-charge drain are separate.
Check output sums costs across calls, including each call's scratch estimate;
that sum is not a simultaneous memory peak.

The ownership and Part adapter are fixture-specific. Logical selection,
version publication, readiness, cancellation of asynchronous transfers, cache
replacement, durability and distributed semantics belong to the separate
Orbital work. Process-lifetime decoding code is linked directly. No timing here
qualifies a hardware/cache tier, an optimized converter, or database headroom.
