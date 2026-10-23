# Candidate dense runtime control

This is a read-only design proposal for the reopened cohesion research. It does
not alter the qualified Frame cohort, select an ABI, or claim a speed result.

The useful endpoint is a small immutable shape selector over shared
operation-major columns, with source placement supplied at the operation's
natural grain. Ordinary copied-proof bindings and typed callbacks compose on
that same control. Frame should not have to retain a large view or reader per
block, and an ordinary checked caller should not have to abandon its count and
extent checks to get shared control.

## Actual starting points

- `ikea/src/seriespack/readers.cpp:5-40` already owns runtime width/geometry
  admission and selects the specialized dense point/range/native16 functions.
  It currently folds over the widths and returns a 40-byte reader containing
  the byte pointer, count and three function pointers. There is no separately
  reusable public runtime shape object.
- `seriespack/detail/dense_read.h` owns exact-tail range traversal and the
  scalar/native bodies. Typed bindings in Frame's `encoding/kernels.cpp:14-22`
  immediately call these bodies; their transient erased objects can disappear.
  The native qualified object comparisons find every such Frame kernel exact.
- Frame's operation-major catalogue already passes body and count separately
  from width-specific code. Its packing adapter instead expands a 64-width
  checked setup dispatcher. The [mechanism survey](mechanisms.md) retains the
  measured memory benefit of removing per-block bindings and the negative
  full-window, cache and fusion cases.
- StreamPack's `detail/compact/author/read.h:20-38` stores two pointers, to its
  named source and immutable shared layout; `rebind` changes only the source.
  Its public `author::visit` resolves geometry around a complete consumer and
  exposes typed access. This is an existing same-layout population witness.
- Directory has no SeriesPack consumer. `engine/src/directory.cpp:34-46,474-490`
  uses Intervals over direct u32 endpoints or a typed K-byte KeySource/KeyRef
  comparator. Its parent supplies count and key width after `Scope::bind`.
  A SeriesPack runtime object must not be inserted into that path. The
  [Directory findings](../../../../segment-directory/README.md#graduated-reads)
  retain the real extra-call/division failure and owner-visit costs.

These three consumers support a common separation of immutable law from current
placement and content proof. They do not support a universal runtime codec
object or a common byte interpretation for ordered logical keys.

## Shape and direct operations

Illustrative names, not declarations already present:

```cpp
auto p = sp::dense_plan<std::uint64_t>::make(
    sp::layout_value{width, 0, geometry});
if (!p) return p.error();

// Ordinary one-shot read: validates this placement and requested coordinates.
auto ok = p->read(bytes, logical_count, first, output);

// Parent already admitted this plane, its logical count and exact byte grant.
p->read_unchecked(bytes.data(), first, take, output.data());

// Retain one operation when the complete command needs only that operation.
auto range = p->range_operation();
range(bytes.data(), first, take, output.data()); // trusted physical contract
```

`dense_plan<U>` can be one ordinary admitted selector/index, independent of
addresses. `U` is the compile-time unsigned input/output carrier; the law's
width must fit it. Width and Local/Striped geometry select the existing code.
The current runtime surface is headless: a nonzero `heads` must be explicitly
refused, rather than silently dropping fields. Existing placed/headed and
composed typed operations continue to cover their different storage laws.

The selector should index operation-major `point`, `range`, `read16` and
initialize columns. It need not carry every function pointer. A caller that
uses one operation can retain that selected function; a caller using several
can retain the selector. Scalar width/output-lane admission remains in Ikea,
not duplicated in every consumer. Replacing the width fold with indexed
selection also avoids making each dynamic owner generate its own dispatch.

This is preferable as a starting endpoint to a mandatory row of every operation
pointer: Frame already observes an 8-byte pointer-column stride, while a row
adds unused-operation residency. A shared row can still be a measured binding
implementation where several operations benefit from it. Static table bytes,
all emitted width/lane bodies and pointer dependencies must be charged.

The checked direct read admits this buffer/count and the requested interval
before touching output. It preserves the current overflow/extent and lane-fit
rules; output disjointness and owner lifetime remain explicit obligations.
The raw operation takes a byte pointer because the parent already has its
logical count and grant. It never infers permission to read another native
window from an adjacent allocation or a matching width.

## Repeated bindings and construction

```cpp
// Optional retained pointer/count proof; the temporary input wrapper may expire.
auto r = sp::bind_reader(*p, bytes, logical_count);

// Named writable placement is the source identity recorded by real effects.
auto dst = sp::attach_dense(*p, writable_bytes, logical_count);
auto need = p->effect_capacity_initialize(*dst);
auto written = p->initialize(*dst, values, effects);
```

One reasonable dense binding stores byte pointer, count and a shared-control
selector/pointer: three machine words rather than three duplicated operation
pointers. It copies the admitted proof and needs no borrowed temporary plan.
An ordinary two-pointer borrowed handle is another useful comparison when the
named source already owns count/placement. It must be charged with that source
metadata and dependent loads. Sixteen bytes is not a contract to recover by
discarding count, adding a global registry or tagging addresses.

`attach_dense` above denotes an optional small named dense placement; it is not
a requirement that Frame retain one per block. It carries the admitted shape,
pointer and count so a checked call cannot accidentally use a different plan.
It exposes plane 0's exact occupied storage for owner resolution. A named
placement used as a journal source survives execution and journal resolution.
An internal typed view may be transient, but its address must never replace
that named source in records.

A single scratch view cannot be rebound to the next block while old journal
entries still name it. An authored owner can instead map effects to its actual
stable Part/block source and plane offsets, as current typed effects adapters
already do, then use the trusted typed initializer after its own admission.
That mapping belongs at the parent boundary; a control lookup must not invent
owner identity, publication, retirement or a registry.

Construction gets its own shared column, selected under the same law. It
validates exact destination count, input fit and journal capacity before the
first store; there is no preexisting content proof for new bytes. It initializes
the owned final tile/slack as the current initializer does. Point/range
replacement remains a different operation requiring initialized preserving
bytes, selection coverage, real effects and any requested summary. It cannot
be implemented by pretending every update is complete construction.

The first compiled initializer should target actual u64 Frame input. Extra
input-carrier matrices should be generated only with an attributed caller or
shared template need, with their code footprint included. The existing checked
writer supports `journal`; arbitrary owner effects and fused authored programs
can use the typed callback layer rather than a new per-store erased hook.

## Typed whole-command reuse

```cpp
sp::author::visit(*p, [&](auto typed_control) {
    // One shape dispatch around a useful command, not a callback per row.
    for (auto block : same_shape_blocks) {
        auto source = typed_control.attach_unchecked(block.bytes, block.count);
        // Existing typed read/native composition here.
    }
});
```

This follows StreamPack's existing `visit`/typed-access/rebind structure.
It keeps constants and actual source identities visible in a complete consumer,
supports native fusion and custom effects, and does not require a generic
erased row callback. The callback should expose the existing typed law or
source operations, not create a second packed-bit implementation.
The read-only sketch's local source expires at the end of its iteration.
Construction uses a named destination lasting through effects resolution, or
an admitted parent effects adapter as described above.

A same-geometry new width selects a new immutable control; it also changes
extent calculations and specialized code. Changing geometry selects a different
byte law, tile height and grant. Neither is permission to reinterpret bytes
encoded under the previous law. Existing bindings continue to name their old
immutable controls; mutating one shared plan in place would reinterpret live
readers and is the wrong rebind model.

Shape reuse never supplies content proof. StreamPack's next source must retain
its admitted assigned geometry/initialized tail. Frame's dictionaries retain
the current edition; an old/new/old image sequence must not reuse a threshold
translation for a different edition. Directory's Scope continues to own its
node Parts and sorted logical-key interpretation. The shared selector owns
none of those proofs, buffers, versions or liveness policies.

The comparison should therefore retain three real paths: parent descriptor plus
direct operation column, an optional small binding over shared control, and
the current complete binding. Raw return ABI and checked `expected` handoff,
logical count proof, metadata residence, table/code bytes and update/rebind work
are separate costs. Several simultaneous bindings, unpredictable width changes,
short planes and empty/mixed masks keep the experiment from rewarding one
isolated function call while making the complete command worse.
