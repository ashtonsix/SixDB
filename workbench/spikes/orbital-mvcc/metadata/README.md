# Metadata and call boundaries

The question is how much state each operation actually needs, where that state
lives, and what crosses its calls. Smaller handles alone do not answer it.
The baseline is the withdrawn Stage 2 cut `7f73c81`; probes retain their compiler,
target, input hashes and generated code. Sizes below are observed records or
requested allocations, not RSS or hardware traffic.

## Findings that change the design

| Baseline observation | Consequence for the next comparison |
| --- | --- |
| Version is 88 B; its repeated ownership header is 32 B | Catalog-owned history can avoid independent reference-counted allocation for every version. Compare 8/16/32 B nodes with their actual directories, chunk slack and retirement. |
| Info and Part are 200 B each; Representation is 160 B before arrays | Removing one field from Version cannot address the closure. Consolidate ownership and directories; test shared interpretation instead of copying the same law into every realization. |
| A one-Part captured closure adds 600 B requested metadata; four Parts add 1,272 B | These are rejected baseline costs. Charge the entire replacement, including exports and shared records. A reduced but still bulky closure is a control, not a satisfactory endpoint. |
| A coherent context is an 8 B owner backed by a 96 B heap enrollment | Compare final-place caller registration with allocation and lifetime work inside the read interval. Keep future discovery and alternative capture correct. |
| Selection is 16 B, but its checked result is 24 B and returns indirectly | An 8 B descriptor borrow permits a 16 B checked result under ordinary ABI. Rich inspection need not travel with every selected access. |
| RetainedView is 8 B but still returns indirectly | Nontrivial ownership matters independently of size. Keep stable owning state in frames and use trivial borrows across frequent calls; explicit escape remains an owning operation. |
| The shared consumer Schema is 10,176 B on AArch64 and 15,296 B for the probed x86 configuration | Separate cold construction plans from hot controls where useful. Do not claim this storage disappeared when a cursor became 8 B. |
| A 256-identity Allocation Group charges roughly 30–34 KiB metadata | Revisit its trim inventory, control mappings and hint/ownership records. The one-byte binding is not the allocation's total metadata; charge full capacity and sparse occupancy. |

The closure measurements use eight copied interpretation bytes per Part and
eight bytes each of logical and parent interpretation. They exclude body bytes,
external owners/control blocks and malloc overhead. The baseline's real codec
descriptions have their own costs. Compare like fixtures when quoting reductions.

Ashton's working target is **4–8 KiB total requested metadata for 64 Objects**
with one or four Parts each: roughly 64–128 bytes per Object after amortizing
collection state. The first compact controls still charge about 36–37 KiB for
the four-Part case, so they are far from that target. A candidate must report
shared shape/law storage, directory and allocator capacity, caller records and
external control blocks separately; moving costs outside its resource does not
establish a reduction. Retained history and escaped access are additional
workloads, not silently included in that initial-collection target.

Allocation metadata is part of this same pass; the existing layer is a
comparison point, not a frozen base. Show a new Arena/Pool's complete setup and
retained capacity alongside the marginal cost in an existing Pool. A small
Object-layer number cannot establish a small complete collection. Changes to
either layer need composed read/write feedback and the physical lifetime,
foreign-return, preparation and refusal checks appropriate to the change.

## Three different kinds of state

**Ownership and authority** stay with the catalog, reader registration, private
operation and any escaped access. Their stable residence matters more than a
small return value. Catalog history must preserve absence and later discovery;
an independently replaced physical alternative needs its own protection.

**Immutable interpretation** connects a physical representation to its Parts,
geometry and native decoder. Test what can be shared between realizations and
what belongs to an Object or logical version. A shared record must remove
duplication, not hide an unchanged per-version allocation. Independent Part
retention must keep its interpretation without retaining unrelated bodies.

**Call packets** carry the qualified access needed now. Prefer ordinary 8/16 B
value returns where the meaning fits; inspect error wrappers and the enclosing
caller. Cold construction records passed by reference and resident SIMD plans
are different from frequent returned packets. A 32 B history record can be a
useful resident layout while being unsuitable as a by-value return.

These are candidate placements, not a frozen architecture. In particular, the
first consolidated descriptor still has a substantial header and Part records.
It must compete with shared-shape/interpretation alternatives under total memory
cost and complete-operation measurements.

History retention is another ownership cost, independent of node width. The
compact Store currently admits new scopes at its current cut. Comparing an
oldest-reader floor with the exact live cuts can reveal how much intermediate
history is retained unnecessarily under that admission policy. Later discovery,
prepared changes, conversion targets, selected alternatives and escaped Parts
still need their actual owners. This is an embedded retention experiment;
[Journal/recovery and audit obligations](../../../../orbital/spec/EXECUTION.md)
are independent retention roots, not released by the absence of a local reader.

## Evidence and executable probes

The selected composed path now uses an inline current version, 16-byte older
nodes with separately charged 8-byte physical-pointer slots, coherent
caller-resident Scopes and a stationary 48-byte `Prepared`.
Preparation returns an 8-byte status. The catalog selects and retains physical
representations without depending on their codec or Shape; full maintenance
remains the default after bounded maintenance increased retained memory and
total work. Scalar coherent reads use the measured friend fusion, and point
writes use the narrow value/tag law. Other layouts remain comparison controls.
The [publication feedback](orbital/candidates/prepared-feedback.json) retains
the negative tiny-write signal as well as the smaller ABI.

Production Allocation now uses 4-byte finite ownership records, a shared
active-Group lifetime and Group-level advice. Growth preserves readable bytes;
the caller owns initialization and content permissions. These changes remove
the former Hold high-water state. They do not establish the whole-collection
budget: cold allocator capacity, initial catalog/physical records and resident
consumer construction plans still matter. The measured cuts and their limits
remain with the [complete-operation feedback](../performance/FEEDBACK.md).

- [Orbital records and closures](orbital/FINDINGS.md), [Allocation and Worker](orbital/allocation-worker.md), and [probe runner](orbital/run.py).
- [Consumer and Ikea field/call census](consumer/ikea-census.md), [actual type probes](consumer/census.cpp), [probe runner](consumer/probe.py), and [qualification/native candidates](consumer/candidates.h).
- [Admitted private write batches](consumer/write-batch.md) compare one batch admission with repeated field-writer binding, including actual effects, private backing and unchanged-error controls.
- [Shared Shape writer laws](consumer/shape-write.md) remove repeated descriptor admission. The composed driver now uses the narrower [value/tag command](consumer/point-write.md), which also removes a source lookup and preserves untouched fields; the full-record writer remains a comparison control.
- [Shared-Shape native reads](consumer/shape-read.md) compare public checked access with a composed private-layout path, using the same stored law and kernels for selected and mapped bodies.
- [Composed Shape adapter](composed/README.md) joins shared Shapes, actual Allocation owners, retained histories, native reads/writes and one-Part/four-Part conversion. [Actual compact adapter checks](performance_adapter/README.md) independently exercise the descriptor-based performance controls.
- [Loom findings and selected evidence](loom/README.md), [actual frame/type probes](loom/facts.cpp), [ordinary call producers](loom/boundaries.cpp), [consumers](loom/callers.cpp), and [code-generation runner](loom/codegen.py).

The [performance study](../performance/README.md) owns the measured comparisons.
Apply shared codec improvements to the mapped-LMDB arm too. Compare the old
catalog with the new native call path before attributing a gain to new metadata.
Include sparse and dense publication, retained-cut first discovery, alternative
replacement, escaped Parts, release/drain and many small Objects. Keep the native
row-key LMDB control visible: matching encoded blobs isolates foundational
overhead but cannot establish a general database performance win.

Run the integrated metadata controls independently of the withdrawn baseline
suite (descriptor, shared shape, Object directory, compact histories, retention,
native consumers and driver adapters):

```sh
orb -m ubuntu cmake -S . -B build/orbital-mvcc/metadata-integration -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=orbital-mvcc
orb -m ubuntu cmake --build build/orbital-mvcc/metadata-integration \
  --target orbital_mvcc_metadata_validate -j2
```

The target also compiles descriptor, shared-shape and inline-current ABI probes.
Tests use the separate `orbital-mvcc-metadata` CTest label and keep assertions
enabled. To build and run only the two adapter checks:

```sh
orb -m ubuntu cmake --build build/orbital-mvcc/metadata-integration \
  --target orbital_mvcc_composed_check orbital_mvcc_actual_adapter_check -j2
orb -m ubuntu ctest --test-dir build/orbital-mvcc/metadata-integration \
  --output-on-failure -R '^orbital_mvcc_(composed|actual_adapter)_check$'
```

The compiled `orbital_mvcc_composed` library reuses the performance runtime,
Shape-writer and Shape-read libraries. These checks qualify repository integration;
the performance study owns measured comparisons.
