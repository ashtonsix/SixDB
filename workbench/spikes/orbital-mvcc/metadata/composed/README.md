# Composed Shape adapter

Disposable performance-driver scaffolding joining the actual compact Shape,
catalog, Allocation and native consumer. It contains no Engine or container
policy. `Inline` uses the selected `orbital::ObjectStore<ShapePhysical>`; `Directory` uses
`CompactShapeStore16`. Both use distinct-cut retention and real independent
Allocation owners, with no shared-pointer backing wrapper.

`backend.h` owns the catalog composition. `material.cpp` owns construction,
private clones, conversion and the explicit native-read seam. Immutable proven
Shapes are shared across Objects. The scalar Inline trial uses
`InlineReadFusion::consume`: one ordinary TU keeps the directory cell through
coherent Scope selection and consumes the selected Tuple through the canonical
Ikea kernel. Its friend access reaches the actual catalog, physical Shape and
Material; it owns no copied directory, layout or cache. Mapped LMDB uses the
same canonical kernel. Batch and escaped Body reads retain the qualified
`shape_read::read_proven` bridge. Column reads and scans use existing generic
consumer binding. [Measured fusion feedback](../../performance/FEEDBACK.md#coherent-read-fusion)
records the benefits, code-size cost and remaining negative cases.
Writes use the shared [value/tag point law](../consumer/point-write.md). It owns
its writer plans, admits every fresh private clone and resolves keys once in
that clone. Key/next bytes remain as cloned; only value/tag are written. All
commands are checked before stores. Cloning before key resolution permits
capacity failure to precede absence and can consume copy work on refusal;
neither installed state nor caller Outcomes change. The generic full-record
writer remains a separate control. Conversion calls `convert_into`,
changing one Tuple Part to four
SeriesPack Parts or back, then adopts at the catalog-issued exact version.

The fixture has fixed membership/count and one common Schema. Default reads
select the primary. `alternative=true` selects a currently adopted alternative,
falling back to primary when absent. That selection is captured by the Scope;
an already selected body remains usable when a later adoption replaces it.
Updates clone the currently selected alternative when present. No navigation or
representation-choice policy is inferred from this test convention.

## Driver integration

Link the compiled `orbital_mvcc_composed` CMake target and include
`metadata/composed/backend.h`. It compiles `material.cpp` and `read_fusion.cpp`
once and reuses
`orbital_versioned_performance_runtime`, `orbital_mvcc_point_write`,
`orbital_mvcc_shape_read` and `orbital_mvcc_directory`. Each source remains an
ordinary independent translation unit.

`Bodies<1>::consume_into` selects the fused primary read; alternative selection
uses the existing captured Body path. The separately qualified
`InlineReadFusion::consume_current` borrows only for an uninterrupted call and
is not used by the coherent Scope benchmark.

```cpp
using Backend = orbital_mvcc::composed::Inline; // or Directory
Backend owner(corpus, byte_limit, data::Format::rows, metadata_partition);
auto acquire = [&] { return owner.bodies<64>(); }; // <1> for batch1
// Pass acquire to the existing opaque_read_trial inside its complete interval.
auto held = owner.read();                         // stationary, nonmovable Scope
auto body = owner.body(held, object, true);        // existing read_operation
need(owner.update(input, output, objects));       // <=64 offered requests
need(owner.convert(object, data::Format::columns));
```

`prepare_conversion(scope, object, format)` returns a movable owned pending
conversion; `adopt(pending)` is a separate fallible operation for delayed work.
Destroying pending work abandons it without publication. Its target retains
source bytes but does not retain obsolete history. Bodies, Scopes and pending
conversions must all end before their Backend. The writer's own basis closes
before install/maintenance. `drain()` is full catalog maintenance and accepts no
work budget; the driver must call it explicitly with those semantics.

`costs` counts actual construction/clone/conversion requests and native work;
`write_costs` counts the qualified writer's admission/key work. Failed private
attempts can consume these counters. Output Outcomes are changed only after
successful publication. `accounting()` exposes complete PMR live/peak/calls,
shared proof bytes, native write controls, public Pool counters, actual live
Allocation capacity/held extent, and Arena charge. Catalog statistics remain
available through `statistics()`.

`held_extent_bytes` is optional: baseline accounting reads the Storage extent;
Storage16 accounting reads the wide Window extent for Windows. Treatments that
remove extent tracking report absence (`null` in the check's JSON), not zero.
This compile-time diagnostic adaptation does not change native read/write paths.

The PMR partition limits catalog/Shape/realization allocations. Additional
resident metadata is reported explicitly: the Backend record, borrowed Schema
and Tuple Shape, dynamic writer descriptors, Arena/Pool setup and inventory,
and Pool metadata. Schema is a fixed inline control with no hidden heap.
The writer's live law is included in its optional slots, which are included in
the Backend record; these are overlapping views of the same storage. Empty
Tuple-law slots also occupy space in a Series-only Backend. Schema remains
resident for construction, conversion and generic reads.
Native Allocation bytes and Pool idle/mapped bytes are distinct. OS/libc thread
state, the reclaimer stack and allocator chunk rounding are not PMR bytes or
Pool metadata; process accounting is still required for those. Corpus payload,
trace and independent oracle storage are shared driver inputs, not backend
metadata. No 4–8 KiB total-metadata or below-LMDB target is claimed.

`check.cpp` reports explicit caller records/arrays; these sizes are not compiled
stack-frame or peak-stack measurements. Read caches are bounded by offered work,
never by catalog size. The fixed 64-entry write arrays remain a performance lead.

## Qualification

After configuring the opt-in build as described in the
[metadata guide](../README.md#evidence-and-executable-probes), run the normal target:

```sh
orb -m ubuntu cmake --build build/orbital-mvcc/metadata-integration \
  --target orbital_mvcc_composed_check -j2
orb -m ubuntu ctest --test-dir build/orbital-mvcc/metadata-integration \
  --output-on-failure -R '^orbital_mvcc_composed_check$'
```

It is also included in `orbital_mvcc_metadata_validate`. For a captured source
cut with a qualification receipt, the private runner remains available:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-mvcc/metadata/composed/check.py \
  --output build/orbital-mvcc/composed/NEW-native
orb -m ubuntu python3 workbench/spikes/orbital-mvcc/metadata/composed/check.py \
  --sanitize --output build/orbital-mvcc/composed/NEW-sanitize
```

The script captures sources and preserves private incremental native/sanitized
builds. No benchmark clocks run. Independent maps check point/miss/dependent/
scan results, sparse duplicate and dense 64-Object publications, late old-cut
discovery, real bidirectional conversion, replaced-alternative lifetime,
partial private failure, metadata refusal, and adoption after history expiry.
Teardown checks zero PMR and actual Runtime checks zero Arena charge.

The selected catalog now lives in `orbital/include/orbital/object.h` and
`orbital/src/object/`. `catalog.cpp` explicitly instantiates its cold operations
once for this fixture's `ShapePhysical`; `read_fusion.cpp` owns the private
`Access<ShapePhysical>` specialization. Counting/refusal `Memory`, physical Shape,
row interpretation and all comparison controls remain in this study. Resource
totals are read from that same Memory exactly once; private structural counters
are projected into the existing driver observation packet.
