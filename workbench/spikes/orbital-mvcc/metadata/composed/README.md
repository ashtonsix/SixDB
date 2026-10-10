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
same canonical kernel. Batch and borrowed Body reads use the qualified
`shape_read::read_proven` bridge, which specializes the same canonical transfers
proved by Tuple admission while retaining the selected address and count.
Column reads and scans use existing generic
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

Point updates keep membership/count fixed and use one common resident Schema.
The disposable one-Object Tuple append caller can publish a larger count.
Default reads
select the primary. `alternative=true` selects a currently adopted alternative,
falling back to primary when absent. That selection is captured by the Scope;
an already selected body remains usable when a later adoption replaces it.
Updates clone the currently selected alternative when present. No navigation or
representation-choice policy is inferred from this test convention.

## Driver integration

Link the compiled `orbital_mvcc_composed` CMake target and include
`metadata/composed/backend.h`. It compiles `material.cpp` and `read_fusion.cpp`
once. The shared `orbital_mvcc_catalog` library compiles `catalog.cpp` once for
this Backend and the Loom callers. The composition also reuses
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
need(owner.append(tail_rows));                   // one Object, primary Tuple
```

`prepare_conversion(scope, object, format)` returns a movable owned pending
conversion; `adopt(pending)` is a separate fallible operation for delayed work.
Destroying pending work abandons it without publication. Its target retains
source bytes but does not retain obsolete history. Bodies, Scopes and pending
conversions must all end before their Backend. The writer's own basis closes
before install/maintenance. `drain()` is full catalog maintenance and accepts no
work budget; the driver must call it explicitly with those semantics.

`append(tail_rows, options)` copies the protected primary Tuple prefix into a
fresh private allocation and initializes the exact suffix through the existing
`construct_into` adapter. It captures the complete new-count Part and uses real
Prepared publication. Input order is caller-owned; binary-search reads require
sorted keys. A refused append may copy a private prefix or initialize part of
the tail. A partially initialized target is never captured; a complete target
may be captured before publication refusal, but no refused append is published. Injection row/Part limits
and KernelCosts describe the tail initializer, while `copied_bytes/spans` account
for the separately covered prefix copy. Suffix-local effects are resolved by the
codec before its views expire. This caller imposes no split or navigation policy.

Source descriptors and private target geometry use the actual selected count.
The initial-count descriptors remain reusable; another count uses transient
count-specific fixture Shapes, including ordinary heap descriptor storage.
Every target Shape stays stationary through codec execution and capture; the
returned `PrivateParts` borrows it only within that phase. Shared captured Shape
proofs keep format/geometry independently of each Representation's count. Old
Scopes and selected alternatives retain their original bytes and count, even
when two Series counts occupy the same rounded tile extent. Conversion preserves
count; append performs a new logical publication. No constructor plan is rebuilt.

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

## Disposable assumptions

The fixture deliberately fixes choices that Engine and real containers must own:

- Four fields with a 23-byte Tuple encoding or four particular Local Series
  columns; sorted fixed records, no variable-length values, schema evolution,
  index/summary maintenance, insertion/split or membership policy.
- Updates clone every Part, including unchanged columns. Default reads prefer
  primary; updates prefer an adopted alternative. Conversion compatibility checks
  count and interpretation, while the converter owes actual content equivalence.
- At most 64 offered update commands, with repeated scans for grouping. Duplicate
  commands apply in offered order and same-value commands still store. Each
  successful Outcome has `changes = 1` per offered command, not per distinct or
  actually changed row. This is a benchmark witness, not SQL affected-row semantics.
- One initial shared Schema, explicitly reused Shapes without interning, and a
  Backend that borrows Corpus/Schema. Four-Part arrays and empty writer-law slots
  are charged scaffolding, not production capacity requirements.
- `Body` borrows both its issuing Scope and Material. It is not an owning Part
  export. Append supports one primary Tuple Object; the separate shared-prefix
  control adds an explicit bounded reservation, without rollover policy.

The [handover](../../OUTCOMES.md#lessons-for-ikea-engine-and-schema-work) explains
what these choices taught us and what the next real caller should reconsider.

## Initial collection inventory

[Current initial counters](current-metadata.json) at `f836ca9` cover 64 Objects,
three records each, independent Allocations, the selected trim bitmaps, exact heap
control backing and extracted Object/Representation module. These are local
AArch64 with 4 KiB pages; x86 Schema storage differs. Persistent requested and
inline bytes, before any update:

| Component | Tuple, 1 Part | Series, 4 Parts |
| --- | ---: | ---: |
| Catalog, Representations and shared proof (one PMR total) | 6,563 | 9,592 |
| Allocation Group | 4,040 | 4,752 |
| Arena/Pool setup and owning vectors | 688 | 688 |
| Backend, including Lane and writer-law slots | 2,048 | 2,048 |
| Shared Schema | 10,176 | 10,176 |
| Borrowed write-Shape record and writer descriptions | 587 | 587 |
| **Total** | **24,102** | **27,843** |

Payload, trace/oracle, libc rounding and OS/thread overhead are excluded.
Initial history, alternative and debt allocations are zero. The writer-law
slots and shared proof are already included above; do not add them again.
Transient caller records and actual peak stack remain separate. This inventory
does not meet the earlier 4–8 KiB aspiration. The larger resident Schema remains
selected because the measured transient replacement penalizes tiny construction;
further reductions need a performance or ownership benefit.

The existing CLI's one-update oracle checks passed in both formats; only its
pre-operation counters are retained here, with incidental timings discarded.
The [earlier census](initial-metadata.json) at `e5a9492` remains unchanged at
26,070/29,163 B. The difference in this same-sized collection is the
[heap-control selection](../orbital/allocation/heap-control/README.md); these
requested-byte totals are not RSS or a whole-process peak.

## Qualification

After configuring the opt-in build as described in the
[metadata guide](../README.md#evidence-and-executable-probes), run the normal target:

```sh
orb -m ubuntu cmake --build build/orbital-mvcc/metadata-integration \
  --target orbital_mvcc_composed_check orbital_mvcc_composed_append_check -j2
orb -m ubuntu ctest --test-dir build/orbital-mvcc/metadata-integration \
  --output-on-failure -R '^orbital_mvcc_composed_(check|append_check)$'
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

`append_check.cpp` uses independent wire and logical oracles on the actual Inline
and Directory callers. It checks 9→16 and 7→9 growth, early and late old-cut
discovery, replaced alternatives, count-preserving conversion, equal-extent
wrong-count adoption refusal, the fused appended-tail read, new-count Tuple
and Series point updates and reverse conversion, private tail and metadata
refusal, and physical settlement after retained Scopes close.

The selected catalog now lives in `orbital/include/orbital/object.h` and
`orbital/src/object/`. `catalog.cpp` explicitly instantiates its cold operations
once for this fixture's `ShapePhysical`; `read_fusion.cpp` owns the private
`Access<ShapePhysical>` specialization. Counting/refusal `Memory`, physical Shape,
row interpretation and all comparison controls remain in this study. Resource
totals are read from that same Memory exactly once; private structural counters
are projected into the existing driver observation packet.
