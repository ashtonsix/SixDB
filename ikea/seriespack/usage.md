# Using SeriesPack

SeriesPack stores fixed-width unsigned values in caller-owned bytes. Choose a
representation, attach its storage, and prepare bindings for repeated reads or
mutations. [ordinary.cpp](../examples/seriespack/ordinary.cpp) demonstrates this
with a 20-bit array: construction, point/range replacement, reads, failures and
physical-description serialization. Build and run it with `ikea_example_seriespack_ordinary`.

## Choose and place the representation

Include `<ikea/seriespack.h>` and link `ikea::seriespack`.
`preset_format<20, preset::bulk_x86, 8>` selects eight separated head bits and a
striped 12-bit payload. The [physical tour](representation.md#byte-layout-tour)
shows those bytes; the [preset catalogue](representation.md#preset-policy) explains
the available starting choices. Head separation is the independent H argument and
defaults to zero. `format<K, geometry, H>` selects the physical law explicitly.

SeriesPack borrows storage; the owner acquires and places bytes. For each present
plane, supply a byte span and a **tile stride in bytes**. Planes are payload,
highest byte, and next-highest byte. Use the format's `tile_rows` to size placement.
Allocate full occupied final-tile storage,
even for an incomplete logical tile. `describe_preset<F>` can suggest tight or
cacheline strides; it does not allocate.

```cpp
using F = sp::preset_format<20, sp::preset::bulk_x86, 8>;
auto storage = sp::view<F, std::uint8_t>::attach(count, planes);
if (!storage) { /* report sp::describe(storage.error()) */ }
// After checking storage:
auto prepared = sp::bind_mutation(*storage);
// After checking prepared:
auto write = prepared->erase<std::uint32_t, sp::sum_change>();
auto read = sp::bind_decoder<std::uint32_t>(*storage);
```

`attach` inspects metadata, not bytes. It checks full occupied extents, stride,
64-byte payload alignment, arithmetic overflow and occupied-field overlap.
Disjoint fields may interleave inside overlapping span envelopes; gaps remain
owned by their siblings, as in the physical tour's shared-partition example.

The named view, prepared mutation and byte owner remain alive at stable addresses
while the erased operations borrow them. Copying a view copies a borrow. Replacing
its mapping or moving a prepared operation requires rebinding. A dense read binding
copies its pointer/count proof and does not borrow a view object.

## Construct, read and mutate

| Operation | Surface | Result |
| --- | --- | --- |
| Construct/reinitialize | `write.initialize(input, effects)` | Exactly `size()` values; initializes owned slack, preserves gaps; no old-value summary |
| Read a point | `read.get(row)` | `expected<U,error>` |
| Read a range | `read.read(first, output_span)` | Output length determines count; arbitrary edges |
| Replace a point | `write.set(row, value, summary, effects)` | A scalar value crosses the boundary directly |
| Replace a selected range | `write.replace(first, input_span, selection, summary, effects)` | All checks precede any local write |
| Already admitted read | `get_unchecked(row)`, `read_unchecked(first,count,out)` | No repeated bounds/capacity checks |
| Already admitted write | `replace_unchecked(first,count,input,selection,summary,effects)` | Whole concrete traversal remains behind one erased call |
| Already admitted region | `read16_unchecked`, `replace16_unchecked` | Sixteen logical positions, original first row divisible by 16 |

Checked calls return errors for command range, value domain or capacity. A returned
failure leaves output/data, summary and effect records unchanged. Lifetime,
isolation and non-aliasing remain caller obligations. “Checked” is a local command
guarantee, not transactional publication. The [reference](reference.md) specifies
which inputs must remain readable.

Use the typed prepared operation directly when erasure has no benefit. It also
provides `effect_capacity(first,count,selection)` and
`construction_effect_capacity()`: conservative **additional record counts**, not
bytes. Reserve that many slots beyond existing records before entering trusted
execution. Coalescing can make the actual output smaller. The query reads only
placement/selection metadata. `replace` and `initialize` perform their own checks;
do not query capacity on every call when the owner already has a reusable bound.

## Selection and effects

`row_selection::all()` and `none()` avoid mask-array traversal.
`row_selection::regions(origin, words)` borrows one 16-bit word per group of
sixteen original positions. `origin` is divisible by 16; bit zero names that
origin. Filtering never silently renumbers rows. Keep selection words stable and
cover the entire request, including partially used boundary groups.

Empty regions skip input/data/effect work. A nonempty native region may read all
sixteen input slots; only selected values must fit width K. Scalar edges read
selected slots only. An activity mask does not grant permission to overread.

`composition::write_journal` writes `{actual source, plane, byte offset, byte size}`
into caller-provided storage. These are issued stores, including preserved
neighbors, not a list of byte differences or beforeimages. Resolve source views
to owner/partition offsets while those identities remain alive. Do not clear the
journal until the owner has consumed the effects.

`sum_change` accumulates an optional modulo-u64 replacement delta from current
old and new values; finalize once at the desired boundary with `finish()`.
`no_summary` removes this work. Initialization has no replacement delta; build
initial summaries from the input or a separate operation. Other summaries require
their own contribution law or invalidation. See [owner integration](../integration.md).

## Diagnose a failure

Use `sp::describe(error)` at a cold boundary. For nested writable bindings, an
optional `mutation_diagnostic` supplied to `prepare_mutation` identifies a short
source or both conflicting source/plane pairs. Resolve those pointers using the
owner's binding names. Overlap admission is intentionally conservative: distinct
writable leaves' whole fields must not overlap, even if a proposed bit-level
alias might be safe under a stronger proof.

Capacity errors can mean short plane/input/output storage or insufficient journal
records; report the operation and supplied extents with the error. Fix the command
or storage reservation and retry. A failure after a *previous successful call*
does not undo that earlier call. Cancellation, acquisition and publication failures
belong to the enclosing owner protocol.
