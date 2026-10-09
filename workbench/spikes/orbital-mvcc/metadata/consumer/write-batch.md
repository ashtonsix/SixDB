# Admitted private write batch

The complete-call candidate binds three field writers once per update batch,
instead of calling the unchanged `update_into` once per row. In a 64-update
batch this changes the logical binding count from 192 to 3. The native and
sanitizer checks preserve the same issued bytes/spans and final wire image;
there is no timed performance result yet.

[write_batch.h](write_batch.h) declares `apply` and `generic_control` with the
same private-clone boundary. [write_batch.cpp](write_batch.cpp) uses existing
TuplePack and SeriesPack kernels, without copying their load/store algorithms.
These are experiments against rejected baseline `7f73c81`, not a production
write API or an 8-byte owning Object.

## Complete call and owner boundary

The caller supplies an initialized, exclusive private clone, live immutable
Schema/Shape/commands, and sorted fixed encoded keys. `apply` validates the
actual canonical parent and every actual Part description using the existing
`ReadData::bind`, checks declared geometry against the supplied IDs/extents,
and rejects mutable body overlap with other Parts, metadata, commands or
accounting outputs. Ordinary readable memory and write authority remain
caller facts. No benchmark Corpus constant substitutes for physical admission.

Every command's width and key existence are checked in offered order before
the first store. This preserves value/key error precedence for mixed failures.
The batch is nonempty and at most 64 commands, matching the old consumer.
Duplicate keys apply in offered order, with the last record winning. Updating
an identical record still issues preserving stores and effects like the control.
Returned refusals leave clone bytes, immutable inputs and seeded counters intact.

Named Tuple or Series views remain stationary. Their three native mutation
endpoints bind once, and complete-batch value/range/effect-capacity admission
precedes execution. TuplePack calls `native_writer::set_unchecked`, the trusted
continuation of its checked setter. SeriesPack calls
`composition::replace_point_unchecked`, the exact continuation of
`prepared_mutation::set`; it keeps the point kernel and effect walk rather than
substituting a range operation. Journal source identities are resolved against
the named views before either views or prepared endpoints end. Post-admission
invariants assert; they do not return a partial-write refusal.

`generic_control` uses the same outer admission and complete batch preflight,
then calls the existing compiled `update_into` per resolved row. Its last key
existence check materializes Row24; the candidate checks only the encoded key.
The shared compiled resolver retains both branches and a Row24 stack slot, but
`apply` selects the key-only branch. The control's per-row validation/binding
and checked stores remain intact. Exact admitted whole views allocate no
temporary alias-analysis storage on either path in this baseline.

This endpoint does not allocate/copy the clone, freeze or publish it, retain
history, or establish MVCC visibility. Its enclosing owner must keep backing
protected, end mutation before freeze/publication, and account for those calls.

## State and emitted code

Both paths still require the full preparation Schema: **10,176 bytes on
AArch64 and 15,296 on x86-64**, including all four retained writer plans and
construction/read plans. A reference does not remove that charge. The reused
three Tuple writer plans occupy 1,008 bytes within Schema; they are not an
additional per-call copy. This loop reduces repeated admission/binding work,
not retained write-law size.

The [size probe](write_batch_sizes.cpp) includes the actual implementation to
observe private records instead of mirroring them. Both targets report:

| Actual record | Bytes | Residence |
| --- | ---: | --- |
| Resolved row indexes, 64 entries | 512 | Complete-call local |
| Four-entry owner-effect journal | 128 | Reused local |
| Tuple view / three native endpoints | 48 / 48 total | Named borrowed locals |
| Four Series views / three prepared mutations | 320 / 144 total | Named borrowed locals |
| Protected-range array / four ReadParts | 176 / 192 | Admission locals |
| ReadData / checked result | 56 / 64 | Cold admission/result |
| KernelCosts / ProbeCosts | 72 / 32 | Caller-owned accounting, copied locally |
| Shape / one WritePart | 304 / 32 | Caller-owned description/container packets |
| Result<void> | 8 | Ordinary register return |

Shape also owns serialized interpretation vectors; the size above excludes
their bytes/capacity. Descriptions, backing protection, caller state and any
owner closure remain additional costs. `scratch_bound_bytes` retains the old
journal-only instrumentation. It excludes indexes, views, admission records,
spills and call areas; it is not a complete-call scratch/frame bound.

The final cut2 assembly gives these individual frame reservations:

| Function | AArch64 bytes | x86 CFA bytes |
| --- | ---: | ---: |
| `apply` | 1,440 | 1,536 |
| `generic_control` | 800 | 784 |
| Shared cold `admit` | 544 | 560 |
| Shared `resolve` | 144 | 128 |
| Series20 prepared bind | 144 | 192 |
| Series20 assignment effect walk | 0 | 24 |

AArch64 includes saved registers/link register. x86 uses the CFA offset and
includes the entry return-address word. These frames coexist with called
functions; they are not total application peaks. The unchanged control still
enters `update_into → write_views → qualify_shape`, whose earlier emitted
frames are recorded in [the control audit](codegen-write-control.md).
The candidate's larger outer frame is not evidence of worse total stack or
better performance. Source `sizeof` totals do not predict optimized frames.

The combined candidate/control object emits **20,344 / 16,723 bytes of text**
and **1,734 / 1,778 bytes of rodata** on AArch64/x86. It contains both endpoints,
their shared helpers and emitted template bodies, including assertion strings;
these totals exclude external baseline/Ikea bodies, unwind sections, linked
deduplication and the enclosing caller. They are not the incremental cost of
one selected application path. There is no extra global Schema constructor.

Both exported endpoints use ordinary C++ `i64` Result<void> returns, with no
special calling convention. AArch64 carries each span as a two-word aggregate;
x86 splits each span into pointer/count. Its two final accounting references
arrive on the stack in this crowded signature. Compiler-private helper
conventions are separate from that public ABI.

## Independent qualification and provenance

[write_batch_check.cpp](write_batch_check.cpp) builds real encoded sources and
real private clones. Independent linear lookup selects expected rows; an
independent per-bit map produces the expected entire final image. It compares
the candidate, complete generic control and a third unchanged per-row control.
Nonzero Tuple spare bits and Series ghost-row/slack bytes expose preserving
store mistakes. Real Holds retain old bytes after original Allocation release.

Final cut3 AArch64 native and ASan/UBSan/LSan/always-use-after-return checks pass
with assertions enabled. x86 evidence here is compilation/ABI/sizes, not an
executed write qualification:

- 18 row/column fixtures at 0, 1, 7, 8, 9, 63, 64, 65 and 129 records.
- Three further metadata-overlap fixtures: actual placement Schema/Shape inside
  2,048-row clones in both formats, and one-row Tuple parent/Part descriptor
  storage as the supplied target. These qualify overlap refusal, not large-count
  update coverage. `SIZE_MAX` mismatches likewise prove refusal, not a valid
  huge-count operation.
- 64 successful batches, 2,112 updates and 1,360,260 independently checked wire
  bits, including initial encoding of the metadata-only fixtures.
- 574 exact-error refusals, including 206 malformed description/placement/alias
  calls, with all bytes and seeded counters preserved.
- 36 duplicate-key batches, 16 same-record batches and 656 retained-old checks.
- Identical 34,848 issued bytes and 5,099 spans; 192 candidate bindings versus
  6,336 control bindings across the successful calls.
- Candidate final checks read keys only; the control materializes 2,112 Rows.
- Physical close drains to zero Arena charge and Pool metadata.

The final immutable source archive is
`build/orbital-mvcc/metadata-consumer/write-qualified-cut-3`, with source-map
SHA256 `6e473aaae05535dd2b20d1aefda3e8a9352eb0deee7290602416ef5841598a94`.
Receipts are in `write-cut-3-native/receipt.json`,
`write-cut-3-sanitize/receipt.json` and `write-cut-2-codegen/receipt.json` under
the same base directory. The implementation/header are byte-identical in cuts
2 and 3; cut3 adds only test cases, so the captured cut2 code audit remains
applicable without relabeling its outputs. Cut1 passed its checks but was
superseded by the offered-order error correction and stronger error oracle.

Qualification reuses exact hashed baseline data/consumer/Object objects from
qualified cut3, with 118 matching lower-library source hashes and recorded
archive hashes. New write implementation/check TUs are freshly compiled for
each configuration. Receipts retain source maps, commands, compiler, sanitizer
environment, reused/new object hashes, binary hashes and outputs. This proves
the captured candidate against the captured comparator; it does not freeze
the allocation layer or qualify a later integration automatically.

[write_batch_probe.py](write_batch_probe.py) records both-target sizes, actual
LLVM endpoint signatures, assembly and object sections with pinned Clang
21.1.8 and libstdc++15. AArch64 is Armv8 SIMD tuned for Neoverse V2; x86 is
x86-64-v3 tuned for Zen5. QEMU executes the x86 size-only binary, never timing.
All local compilation/checks use CPU2 without a performance campaign.

```sh
orb -m ubuntu python3 workbench/spikes/orbital-mvcc/metadata/consumer/write_batch_probe.py \
  --candidate-root build/orbital-mvcc/metadata-consumer/write-qualified-cut-2/metadata/consumer \
  --baseline-root build/orbital-mvcc/metadata-consumer/write-qualified-cut-2 \
  --output build/orbital-mvcc/metadata-consumer/write-cut-2-codegen --cpu 2
```

The next discriminating evidence is paired whole-owner writes/publication and
old-reader release with these exact adapters. Clone cost, Schema retention,
generated code, allocation metadata and history/reclamation must stay visible.
The collection-wide metadata target and any production decision remain open.
Assertions diagnose journal source identity/range invariants in these builds;
those diagnostics disappear under `NDEBUG`. The wire oracle and aggregate
effect comparisons are unconditional and remain active independently.
