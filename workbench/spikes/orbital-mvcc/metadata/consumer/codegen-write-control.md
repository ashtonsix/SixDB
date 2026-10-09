# Frozen write/conversion call evidence

The strongest surviving orchestration opportunity is qualifying and binding a
private update batch once. The frozen implementation calls `update_into` for
every row; both emitted targets retain `update_into → write_views →
qualify_shape`. Tuple qualification still calls the layout decoder, copies its
392-byte success value and compares the decoded layout with Schema. This is
observed code, not a timing estimate or an Ikea kernel problem.

This audit reads the existing `probe/{aarch64,x86_64}/locals_data.{ll,s}` and
`locals_consumer.{ll,s}`, plus `packets.json` and `receipt.json`, under
`build/orbital-mvcc/metadata-consumer/`. Those probes include actual frozen
`7f73c81` `data.cpp`/`consumer.cpp`, using Clang 21.1.8, libstdc++15 and `-O3`,
without LTO. AArch64 is baseline Armv8 SIMD tuned for Neoverse V2; x86 is
x86-64-v3 tuned for Zen5. No build, execution or timing was performed for this
audit. [The source census](ikea-census.md) owns the field/lifetime inventory.

The frozen cut has `construct`, `convert`, `update` and private `build`, not
`build_update` or `apply` entry points. Newer paths bearing those names are
outside these emitted files. Their behavior cannot be inferred from this cut.

## Actual carriers and ordinary calls

Sizes below come from the existing probes on both targets unless separated.
An identity ABI probe reports carrier classification; the actual call site's
arguments and optimized private convention are a separate observation.

| Carrier / call | Emitted evidence |
| --- | --- |
| `Shape` / `Result<Shape>` | 304 / 312 bytes; actual `shape` returns through `sret`. It allocates interpretation vectors and invokes Tuple encode or Series descriptor encode. Consumer callers keep a result slot and moved Shape slot. The move transfers vector ownership, not their serialized contents. |
| `WriteColumns` / `WriteViews` | 320 / 328 bytes; ordinary identity returns are indirect. Actual private `write_views` uses compiler-private `fastcc` with a caller-provided output address. It constructs the variant there; do **not** count the identity probe's 328-byte copy as an extra actual return copy. |
| `Writers` | 192 bytes, containing four real WriteViews plus four WriteParts. Its ordinary identity return is indirect, but actual construction is directly into the consumer's local slot. Writer ownership is not copied as a 192-byte packet at each row. |
| `ReadData` / result | 56 / 64 bytes. Convert/update call compiled `bind` and materialize a local successful value: IR copies 56 bytes on AArch64, 52 on x86 (padding excluded). These fixed copies are separate from update's later body-sized payload copy. |
| `RepresentationSpec`, four PartPlans | 104 bytes, and `4 × 72` bytes. Private `build` fills the local plan array and calls actual external `Store::build`. Descriptor spans borrow Shape until that call captures them. |
| Builder / Representation and results | Owners are 8 bytes; each result is 16 bytes and actual return uses `sret` on both targets. Small owning handles are not automatically register-returned: their nontrivial lifetime matters. |
| `construct_into`, `convert_into`, `update_into` | Actual `Result<void>` returns as `i64`, not a hidden result allocation. Schema/Shape/source/counters are references; Part and payload spans carry pointer/count. AArch64 represents spans/options/Record as two-word IR aggregates. x86 splits spans and passes ConstructionOptions on the stack; `update_into`'s 16-byte Record is `byval` in this crowded signature. That does not contradict Record's register-return identity probe. |

The metadata containers own descriptions, not body protection. `Store::build`
and `Builder::freeze` remain compiled external calls in these TUs. Their source
captures parent/Part interpretation into real owned metadata and allocates real
Parts. Their frames, allocation internals and retirement work are not emitted
here and are not included in the following table.

## Frames are not retained metadata or whole-path peaks

Fixed frame reservations below are read from assembly prologues, including
saved registers. The x86 numbers use the compiler's CFA offset and therefore
include the entry return-address word; AArch64 stores its link register inside
the listed reservation. They are not logical live-object totals.

| Emitted function | AArch64 bytes | x86 CFA bytes |
| --- | ---: | ---: |
| `shape` | 576 | 512 |
| `qualify_shape` | 1,040 | 992 |
| `write_views` | 320 | 352 |
| `construct_into` | 2,848 | 2,864 |
| `convert_into` | 5,984 | 6,160 |
| `update_into` | 720 | 720 |
| Outlined Local64 construction chunk | 2,416 | 2,464 |
| Outlined Local64 conversion chunk | 2,384 | 2,448 |
| Consumer `construct` | 1,040 | 1,024 |
| Consumer `convert` | 1,152 | 1,152 |
| Consumer `update` | 1,744 | 1,664 |
| Private `build` / Writers constructor | 352 / 144 | 368 / 144 |

The largest emitted function frame is cold `Schema::Schema`, not conversion:
AArch64 reserves 64 saved-register bytes plus 10,176 bytes, then rounds SP down
to 64-byte alignment; x86 pushes four registers, rounds SP down to 64-byte
alignment and reserves 14,400 bytes. Alignment padding depends on entry SP.
Do not turn either reservation into a fixed exact total. Schema itself is
10,176 / 15,296 bytes in caller-owned storage; it is not part of the constructor
callee frame. Constructor and expected-constructor controls are 8,000 / 8,064
bytes on AArch64 and 13,120 / 13,184 on x86. External preparation frames are
additional and absent from these files.

Among emitted steady construction/conversion/update functions, `convert_into`
has the largest individual fixed frame. It is still not the concurrent path
maximum: consumer `convert` stays resident while `convert_into` calls its
outlined Local64 chunk, whose checked initializer has another frame. Likewise
consumer `update`, `update_into`, `write_views` and `qualify_shape` coexist during
qualification. External decoders/builders/kernel bodies and caller frames add
further storage. No total path peak is claimed by this audit.

Source payload scratch is separately identifiable: row-target conversion uses
four 128-element u64 buffers (4,096 bytes) plus the reused 128-byte construction
input. Column chunks use one 128-element u64 buffer (1,024 bytes). Their
32-entry owner-effect journal is another 1,024 bytes of effect evidence, not
decoded payload; row construction's one-entry journal is 32 bytes and update's
four entries are 128 bytes. Metadata views/results, spills and ABI call areas
explain additional frame storage. An emitted mixed-format function reserves
its fixed frame even while a branch's source-level buffers are inactive; summing
all IR `alloca` extents would ignore lifetime coloring and double-count reuse.
`KernelCosts::scratch_bound_bytes` counts its declared buffers, not these frames.

## What survives on complete paths

**Construct.** Consumer `construct` and compiled `construct_into` both scan
input value widths; separate TU boundaries preserve both loops. Shape creation,
real Builder creation, Writers opening and target qualification precede any
store. Tuple construction binds once around its row loop and reuses the
construction input. It still reaches outlined `construction_operation::
set_unchecked` and compiled `write_buffered`; the second wide constructor plan
is not executed for this 23-code law. Column chunks still attach a real slice
through compiled `admit_view`. Local64 preparation is mostly scalarized, while
Local20 prepared bind and checked initialize remain outlined.

Do not promote every source-level duplicated check into a runtime claim. In
the bounded Local64 chunk, the explicit construction-effect-capacity check
against 32 journal entries disappears before the emitted initializer call.
Checked `initialize` still validates count, placement count/stride and remaining
journal capacity. Local20 initialization retains value-width admission and
physical coverage work. The source census's two capacity computations describe
the source contract; they do not establish two surviving generic field walks.

**Convert.** The generic `ReadData::field` wrapper is inlined into some chunk
work on both targets. There is no repeated external generic `field` call on
those paths. The source variant branch and immutable Tuple unit checks are
still inside decoded field/chunk work; an outlined Local64 conversion helper
materializes TupleBody or ColumnBody and calls their field methods. Row-target
conversion still calls `ColumnBody::field` for a column source. Local20 dense
range decoding remains an outlined concrete call. Thus opening one homogeneous
source around a complete conversion is a real bounded orchestration candidate,
but “remove all generic field wrappers” overstates what is left after `-O3`.
Current bounded scratch and real re-encoding remain part of conversion.

**Update.** Consumer `update` first resolves and validates at most 64 input rows.
Its key-existence check calls compiled `ReadData::lower_bound`, then compiled
`row_unchecked`, which materializes a 24-byte Row although only key is consumed.
It creates one private Builder, copies each complete source Part once, then
calls `update_into` separately for every offered update. That latter loop and
its per-row qualifier chain survive on both targets. `qualify_shape`'s Tuple
decoder returns a 396-byte expected value through `sret`, followed by an actual
392-byte `memcpy` call and code comparison; Series descriptor decode similarly
returns through `sret`, with a 48-byte success copy in IR.

Tuple mutation endpoints are largely scalarized inside `update_into`; do not
attribute three retained endpoint objects to every row from their source names.
Local20 prepared bind remains an outlined 56-byte result followed by a 48-byte
success copy. Its outlined `set` still checks row, value width and two available
journal entries before the same preserving body/tail stores. Other width paths
inline more heavily. A function containing general alias-analysis fallback
does not show that this whole-view caller executes its heap-vector path.

## Practical follow-ups and lifetime constraints

1. Qualify the fixed private target and bind stationary field endpoints once
   around the update loop. This removes an observed repeated descriptor decode,
   placement admission and binding chain while keeping existing Ikea stores.
   Maintain complete input/target preflight, issued coverage and private-target
   discard on failure; no new container policy or copy grain follows from it.
2. Make update's existence preflight consume only the encoded key. The current
   ordinary call materializes and decodes a full Row only for its key comparison.
   Sharing that bounded traversal with the homogeneous batch read can remove
   work without changing membership/index semantics.
3. Open a homogeneous source once around conversion; evaluate surviving call
   and frame changes rather than relying on source wrapper counts. Keeping
   row/column orchestration in separate local helpers may also avoid reserving
   both branches' fixed frame in their caller. Neither is evidence for a new
   codec or a kernel rewrite.
4. Keep cold construction controls and metadata allocation visible. Schema
   sharing already avoids per-Part copies; the unused second constructor plan
   is actual owned storage and cold preparation, not a hot second kernel call.

The existing ownership closure is meaningful and survives codegen: the four
WriteView destructors run before `Builder::freeze` on successful paths, and
unwind paths destroy Writers/Builder. No Ikea view, prepared operation, journal
or raw span owns those rights. Freeze checks writer closure, not initialized
format correctness; the consumer owns the latter responsibility. Injected
partial construction/conversion errors stay private and are discarded.

A hoisted operation must borrow a named stationary WriteViews value; binding
inside a returned/moved bundle would leave endpoints and journal source identity
pointing at former view addresses. Resolve effects while that same view lives.
Raw WritePart spans cannot be mechanically revoked and must end before Writers
close or Builder revokes. Source Selection protection and immutable Schema must
outlive generic conversion/update reads. Removing metadata carriers must not
silently remove these owner, count/descriptor or failure boundaries. This audit
finds no new owner/lifetime bug in the frozen path; these are concrete constraints
on the bounded orchestration opportunities above.
