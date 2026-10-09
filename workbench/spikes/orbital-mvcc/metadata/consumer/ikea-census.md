# Ikea records on the frozen consumer path

Source census of `7f73c81`, read from
`build/orbital-mvcc/metadata-consumer/frozen/`. Paths below refer to that archived
cut, including its original `workbench/benchmarks/orbital/versioned/` directory.
The current relocated spike is not the source of these observations. This is a
field, lifetime and call-chain inventory; it contains no inferred object sizes,
ABI classifications, instruction counts or timing claims. Actual layout and
generated-call evidence belong to the accompanying probes.

The consumer uses TuplePack and SeriesPack. Bec256, erased mutation operations,
CPS carriers and aggregate-maintenance state are not invoked on these paths.
Logical `Row`/`Record` and SIMD input/output packets are payload, not ownership
or placement metadata.

## Shared prepared controls

| Record | Owned fields and borrowed dependencies | Use in this cut |
| --- | --- | --- |
| Consumer `Schema` | Owns one `tp::layout`, one `tp::constructor`, four `tp::reader<8>` and four `tp::writer<8>`, all const. Plans own their controls; they do not borrow the layout/map used by `make`. | Shared caller state, borrowed by Tuple controls/bodies and construction/update. No per-Part Schema copy is introduced. |
| `tp::layout` | Owns `array<code,128>` plus unit-byte and code counts. Each code is offset/shift/width. | Canonical 23-code physical law; also constructed as a temporary by cold descriptor decoding and compared with Schema. |
| `tp::reader<8,1>` | Owns `detail::gpr_read` and a selected `detail::gpr_reader<1>` function pointer. `gpr_read` owns eight scalar codes, `packet_layout<8>`, slots/unit bytes/read mask, and `word_transfer` mask/offset/shift/bytes. `packet_layout` owns offsets, row owners, row/slot counts and row-major/single-group flags. | Four field projections. The frozen native fixed-transfer read uses the controls directly through `native_reader`; it does not invoke the reader's selected raw-GPR function pointer. |
| `tp::writer<8,1>` | Owns `detail::gpr_write` and selected `gpr_writer<1>`. Controls include scalar read/order metadata, eight write-byte entries, invalid-byte masks, footprint runs, point-word controls, counts/write mask, word transfer, invalid word and native old-read mask. Each write-byte entry owns offset/mask/count and input/shift arrays. | Key writer is retained in Schema but this fixed-row update uses only next/value/tag. Native writer execution accesses controls directly. |
| `tp::constructor` | Owns **two** `writer<64>` values plus code count. Each writer owns `detail::write64`: scalar read/order metadata, store/invalid/footprint arrays, eight ISA-specific shuffle rounds, preserve bytes, round count and dense/needs-old flags. Shuffles retain only this build's ISA operands. | `constructor::make` prepares both halves even for this 23-code law. `accepts` and initialization use only the first half when count is at most 64. The second owned plan is not executed in this consumer. |

Definitions: `ikea/include/ikea/tuplepack/{description,packet_layout,plan,construction}.h`,
`tuplepack/detail/plan.h`, and `ikea/src/tuplepack/construction.cpp`.
Stored array capacities are source facts, not estimates of padding, aggregate
size, register passing or how much optimized code loads.

## Placement, read records and results

| Record | Fields / ownership | Lifetime and call role |
| --- | --- | --- |
| `ReadPart`, `WritePart` | IDs and borrowed immutable body/interpretation spans, or ID and borrowed mutable body span. No protection owner. | Local arrays in `consumer.cpp` cross into cold `data.cpp` functions by span. Returned cursors do not borrow these arrays or descriptions. |
| `TupleControl`, `ColumnControl` | Immutable Schema reference plus count, or immutable count only. | Trusted `open` requires the admitted same/equivalent law/count. Controls are separate caller records; the cursor cannot check their association. |
| `TupleCursor`, typed `ColumnCursor<K>`, private `ColumnCursors` | Raw payload pointer; the column group owns four typed pointer values and no count. | Created only after cold qualification. Copies borrow backing, not their former object address. Four-column grouping cannot be independently reassigned field by field. |
| `TupleBody`, `ColumnBody` | Schema pointer/count/Tuple cursor, or count/four column cursors, copied by value. | Header-visible homogeneous native operations. Bodies do not borrow a control/cursor/frame object, but bytes and Tuple Schema must remain live. |
| `ReadData` | Schema pointer, count and variant of Tuple cursor / private column group. | Generic history/conversion record returned across `data.cpp` and `consumer.cpp` boundaries. Public read definitions in `data.cpp` delegate to the same native bodies. |
| `tp::basic_view<Byte>` | Borrowed storage span, count, stride, offset, unit bytes. | A named local on each Tuple native call, or a local writable view in `data.cpp`. Here stride/unit are 23 and offset is zero; extent is derived from admitted count. |
| `tp::read_operation<8,const tp::byte>` | Pointers to a named prepared reader and named const view. | Local endpoints; the view and Schema plans outlive the call. `native_reader<8,const tp::byte>` copies those two references, not a reference to the operation object. `get` reuses its key endpoint through search and prepares payload endpoints only on a hit; row/scan prepare all four. |
| `sp::view<Column<K>,Byte>`, `sp::plane<Byte>` | View owns three plane records and count; each plane is borrowed span plus stride. | Cold reads attach all four real placements, then discard the generic views. Writes keep mutable views through mutation and effect resolution. Head planes are unused for this law; no ownership is implied by their spans. |
| `sp::dense_source<Column<K>>`, `sp::decoder<uint64_t>` | Dense proof is payload pointer/count. Decoder copies resource pointer, count and range/region/point function pointers. | Created locally for homogeneous column reads. Dense binding does not borrow the temporary dense-source wrapper. Header function pointers refer to dense template bodies; devirtualization/inlining is a probe question. |
| `sp::representation`, `descriptor_bytes` | Width, heads, geometry, logical count and three strides; portable serialized descriptor is a fixed array. | Cold decoded value compared against the selected fixed law and count. It is not a native plan or retained owner. Tuple serialization likewise carries codes, not prepared ISA controls. |
| `expected<T, tp::error>`, `expected<T, sp::error>`, `orbital::Result<T>` | Success value or error discriminator/value; success inherits T's ownership/borrows. | Cold plan/view factories return expected values, and consumer factories return generic/native results. `take` unwraps/moves success. Probe wrappers separately: payload size does not establish result layout or return ABI. |
| `optional<Record>`, `Result<Row>`, `Result<void>` | Logical payload/presence, logical row/error, or status. | Native get/row/field results and compiled generic/complete helper boundaries. They carry no Part protection or publication rights. |

Tuple's source-level read dependence is body Schema pointer → reader controls
and local view → storage/count/stride → payload. Fixed native transfer reads
need transfer mask/offset/shift and the payload address; scalar code/order and
the selected raw-reader function pointer serve other paths. Series point reads
use typed payload pointer and original row; the decoder also carries count and
range/region entries that point-only execution does not call. Whether any of
these intermediate records or loads survives optimization is unmeasured here.

## Construction, mutation and effect records

| Record | Fields / ownership | Active path and lifetime |
| --- | --- | --- |
| `Shape`, `PartShape` | Own parent interpretation vector and up to four Part shapes; each Part owns descriptor vector plus ID/body extent/alignment/padding. Format/count/Part count are also stored in Shape. | Constructed in `data.cpp`, returned to `consumer.cpp`; plans/spec borrow vectors until `Store::build` copies descriptions. Shape owns metadata, not body bytes or rights. |
| Local `WriteColumns`, `WriteViews` | Four mutable Series views, or variant of those / Tuple view. | Returned inside `data.cpp` from `write_views`; all targets qualified before writes. Every update row currently rebuilds this record and requalifies the unchanged Shape. |
| `tp::construction_input` | Rank-ordered `array<byte,128>`. | One reused consumer input, unused slots cleared once. Constructor internally copies the lower 64 bytes into `packet<64>`; the upper half is copied only for more than 64 codes. No whole-input Row vector is used. |
| `tp::construction_operation` | Borrowed constructor pointer and named mutable view pointer. | Bound once around the row loop; initializes one row at a time and emits one unit span before stores. The constructor and view remain stationary. |
| `tp::mutation_operation<8>`, `tp::native_writer<8>` | Mutation borrows writer and view pointers; native writer owns a copy of that borrowed operation. | Three local next/value/tag endpoints. Capacity/width preflight precedes the first store; checked `set` performs its own range/value/capacity checks. No maintenance state beyond `no_maintenance` is used. |
| `sp::composition::prepared_mutation<E>` | Owns expression E, count and a cached physical-source pointer. E from `describe(view)` has body/tail/head0/head1 leaf references to the same named view, including fields unused by headless/whole-byte types. | Active **concrete** binding from `sp::bind_mutation`. Whole-view admission reuses placement proof and avoids temporary alias vectors. The expression/view references and physical pointer duplicate source identity; they must not outlive the view. Its erased operation carrier is not used here. |
| `sp::row_selection`, `sp::no_summary` | Selection has kind, origin and optional borrowed mask-word span. No-summary has a compile-time no-old-value law. | Update capacity calls use `row_selection::all`; no selection-mask storage or aggregate state is retained. Point updates still preserve neighboring physical bits. |
| `ikea::byte_write`, `owner_write` | Plane/offset/size; owner record adds borrowed named-source pointer. | Issued-store evidence, including preserved neighbors. Source identity is the local attached view, not Part ID or data address. |
| `source_write_journal` | Borrowed owner-write span plus used count; opportunistically merges only the previous compatible record. | Row construction uses one record, column construction a fixed conservative array, update four records. `account` resolves each source identity against the current named view before it expires. Column updates reset used after resolving each field. |
| Internal count/extent/coverage walkers | Counts, valid flags, or borrowed view/coverage references. `physical_coverage` maps byte effects back to one actual view. | `prepared.initialize` computes construction capacity again after the consumer's explicit capacity call. `prepared.set` recomputes point footprint after the consumer's explicit batch capacity sum. Generic alias-analysis vector/diagnostic records exist, but the admitted whole-view fast path does not execute that allocation path. |
| `KernelCosts`, `Costs`, `ConstructionOptions` | Owning scalar counters, scratch high-water mark, copy/request counters, and failure-injection frontiers. | Passed by reference or small options value at compiled consumer/data boundaries; they are accounting, not runtime backing or rollback authority. |

Column conversion owns one `array<uint64_t,128>` reused per field/chunk plus its
journal array. Conversion toward rows owns four such field arrays, the one
construction input and its journal. These are bounded payload/effect buffers;
they must not be conflated with persistent cursor metadata or total compiler
stack use. `Writers` separately owns real Orbital WriteViews and holds borrowed
WritePart spans. All writers close before freeze; Ikea records themselves do
not retain the Builder or grant mutation/publication authority.

## Calls to probe rather than presume

- `consumer.cpp` → `data.cpp`: `shape` returning `Result<Shape>`; raw bind
  returning `Result<ReadData>`; generic lower-bound/row reads in update; construct/convert/update
  with Schema/Shape/ReadData references, Part spans, costs and options. Thin
  actual factories currently bind a temporary generic result, check count and
  extract a homogeneous result. These are ordinary source TU boundaries.
- Schema setup / cold Tuple validation → compiled Ikea definitions:
  `layout::{make,decode,encode}`, `constructor::make`, `prepare_read8`,
  `prepare_write8`, `prepare_write64`, selected GPR entry preparation and
  `packet_layout::make`. Constructor's `writer<64>::set_unchecked` reaches
  compiled `detail::write_buffered` with controls and a packet reference.
- Series cold placement → compiled `detail::admit_view`: count, tile rows,
  `array<size_t,3>` occupied widths, `array<stream_extent,3>` and striped flag;
  each stream extent holds pointer/bytes/stride. Overlap helpers take
  `occupied_run` records (base/stride/bytes/count). Descriptor encode/decode
  also has compiled value/result boundaries.
- Native bodies, view bind, local endpoints, concrete Series prepared
  mutation and journal hooks are header-visible. This does not prove that a
  caller inlines them, scalarizes their records or eliminates indirect calls.
  Include the real `versioned-performance/read.h` read/chain/scan dispatcher:
  its branch owns `array<Row,16>` scan scratch even though read/chain need no
  scan output. Measure whole caller assembly, not only isolated `get`.

Probe the records above plus `detail::{scalar_read<8>,scalar_write<8>,read64,
write64,shuffle,word_transfer}`, `tp::packet_layout<8>/<64>`, and result wrappers
for layout, constructor, reader/writer, view, generic/native consumer records,
descriptor values, capacity counts and status. Obtain the exact Series E and
prepared types with `decltype(composition::describe(named_mutable_view))` and
the `value_type` of `decltype(sp::bind_mutation(named_mutable_view))`, for
`Column<64>`, `Column<20>` and `Column<32>`. `decoder<uint64_t>` is common across
those formats but its selected template bodies differ. Distinguish structural
layout probes from actual read/construction/update call-site disassembly.

## Bounded alternatives worth testing

1. **Remove repeated consumer qualification inside a private batch.**
   `consumer::update` has already resolved every key and checked every value;
   Writers and target shapes stay fixed. Qualify WriteViews and bind their
   three field operations once per batch, then use those stationary views for
   each row and resolve effects before leaving their scope. Preserve whole-call
   preflight and private-failure behavior. This changes orchestration, not
   container copy grain or Ikea kernels.
2. **Open the source representation once around conversion.** Current generic
   `source.field` is invoked four times per row-target chunk and once per
   field/chunk toward columns. Branch to one homogeneous native body around
   the conversion loop so Schema/count/variant dispatch need not be reconstructed
   at each field call. Keep the same bounded buffers and existing constructors.
   Same-TU optimization may already remove some of this; probes decide.
3. **Avoid generic temporary results in homogeneous cold factories.** Share
   the exact descriptor/placement validator but construct TupleCursor or the
   private column group directly rather than `Result<ReadData>` followed by a
   second result extraction. Retain exact count/format checks and all plan
   admissions. No boxing, ownership change or per-Part extra state is needed.
4. **Separate fixed physical effects from compound-source machinery where
   justified.** This consumer resolves each journal immediately against one
   known Part/view. The existing physical Series `initialize(view,input,
   write_journal)` can use plane/offset/size evidence without per-entry source
   pointers or the prepared expression's conservative field walk; it invokes
   the same physical constructor used by the current fast path. Establish its
   intended public-use contract before switching. A more general direct
   coverage hook would require checked preflight equivalent to the existing
   APIs; do not silently weaken rejection guarantees or drop issued coverage.
5. **Keep large cold controls shared; examine unused capacity separately.**
   Schema is already shared rather than duplicated per Part. Constructor's
   unused second wide writer and GPR controls not consulted by fixed-transfer
   reads are concrete stored-field candidates for a future Ikea-owned
   hot/cold control split or bounded prepared specialization. This is a control
   representation question, not evidence for a kernel rewrite. Measure storage
   and real dependent loads first; do not trade small cursors for a hidden
   per-Part control allocation.

Count is intentionally repeated in logical metadata, parent interpretation,
Series descriptors, Shape and shared/native controls so cold qualification can
compare independent promises. Hot cursor methods do not reload persisted
descriptors. Replacing these equality checks with one unchecked count source
would remove a qualification boundary, not merely redundant metadata.
