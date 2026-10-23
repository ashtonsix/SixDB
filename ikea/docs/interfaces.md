# Composing Ikea interfaces

Ikea uses one vocabulary for layout, placement and operations. The modules keep
their useful representations and specialized bodies; a caller should not have
to relearn what `attach`, `make`, `reader` or `writer` means for each one.
[Module guides](../README.md) own the detailed commands and access bounds.

## Follow one lifecycle

| Step | Interface | Established fact |
| --- | --- | --- |
| Describe | `layout::make(...)` or a compile-time layout | The byte law, independent of addresses and logical count |
| Place | `view::attach(...)` / `const_view::attach(...)` | Addresses, extents and placement metadata satisfy that law |
| Validate | `view.validate()` where encoded contents can be invalid | The recovered contents obey the law |
| Plan | `read_plan::make(...)`, `write_plan::make(...)` or an author plan | Storage-independent maps, grouping or routes |
| Bind | `bind_reader(...)`, `bind_writer(...)`, `bind_transfer(...)`, `bind_predicate(...)` | A reusable operation with its stated borrowing and admission |
| Erase | `.erase<...>()` or an explicitly erased binding | A runtime-dispatched whole operation |
| Run | `get`, `read`, `filter`, `set`, `replace`, `initialize`, `copy`, ... | One command, checked by default |

A module can omit a step. Intervals projects borrowed endpoints and needs no
layout object or operation binding for an ordinary search. StreamPack's free
commands operate on a placed value owner; author dispatch binds its typed access
for an enclosing consumer. Hashtable owns storage and exposes `table` or
`erased_table`, rather than pretending to be another borrowed pack.

`attach` checks placement metadata, not payload contents. `attach_unchecked`
reuses exactly that placement proof. `validate` examines contents. Neither grants
lifetime, initialization, isolation or bytes beyond the supplied extent.
Bec256 makes the distinction explicit: place a `const_view`, then `bind_reader`
validates once into a reader carrying its admitted interpretation. An owner with
a surviving content proof can use `bind_reader_unchecked`.

Plans copy the metadata documented by their module. A bound operation can borrow
its named plan and view; copies retain that borrowing. Erased operations can
borrow the concrete operation too. Name these objects before retaining a handle:

```cpp
namespace sp = ikea::seriespack;
using L = sp::layout<12>;
std::array<std::byte, 24> storage{};
auto placed = sp::view<L>::attach(16, {{{storage, L::tile_bytes}, {}, {}}});
// Check placed before dereferencing it.
auto reader = sp::bind_reader(*placed);
auto erased = reader.erase<std::uint32_t>();
// Keep placed, reader and backing alive for erased's documented borrowing.
```

A general expression reader owns its expression and borrows its leaves. Erasing
such a temporary is rejected. A dense source instead copies its admitted
pointer/count proof; that wrapper need not survive its erased reader. The
explicit `bind_reader<U>(view)` shortcut binds an erased buffered reader directly
to a named physical view, without adding an intermediate retained operation.
The [SeriesPack reference](seriespack/reference.md) states these lifetimes.

TuplePack's general reader borrows its named plan and view. For repeated
one-code filters, `bind_predicate<Rows>(layout, code_index, view)` selects a code
from a validated physical law without preparing a packet plan. The owner supplies
the authoritative layout for the bytes; matching view extent alone proves no
layout identity. The predicate copies the code and retains only the named view.
Its layout, preparation plan and intermediate reader may expire;
the view's value, address and backing remain stable through every call. The
equivalent `bind_predicate(reader)` and `bind_predicate(plan, view)` conveniences
reuse existing one-code plans.
`reader.filter` remains the checked single-command convenience. The
[TuplePack guide](tuplepack/usage.md#project-codes-into-a-packet) shows both forms.

`_unchecked` sits beside the checked command and reuses its command facts. Author
physical stores are a separate building block: SeriesPack `author::store` and
`store_unchecked` preserve neighbors but emit neither effects nor summary changes.
Ordinary logical mutations use a bound writer. Their checked spelling must never
silently imply owner admission or publication. SeriesPack and TuplePack writer
commands put the required effects argument immediately after the input/value,
then selection/activity and maintenance. Omitting a SeriesPack summary uses its
stateless no-summary recipe; explicit stateful summaries remain borrowed lvalues.

## Storage bytes and decoded values

Borrowed physical storage and encoded descriptions use `std::byte` spans.
`view` is writable; `const_view` is read-only. `basic_view<..., Byte>` underlies
those aliases where useful. Native kernels use explicit `to_u8` bridges for
unsigned-character arithmetic; those bridges preserve constness and extent.

Decoded unsigned values, code-map indices and numeric fields remain integers.
TuplePack's decoded packet slots are `uint8_t` values, not opaque storage bytes.
Hashtable's opaque value payload is `std::byte`. A new target must preserve the
specified numeric bit/byte order; serializing native objects is not a substitute.

## Give every position a coordinate system

| Coordinate | Meaning |
| --- | --- |
| Row | Original logical value/record position in a pack view |
| Rank | Ordered interval/reservation identity, or a code index when explicitly qualified as code rank |
| Window bit | Bit `r` names original row `first+r` in an ordinary filter result |
| Packet byte | Decoded TuplePack slot; a plan's map and grouping determine its original row and code |
| Block bit | An enclosing consumer's coarse block position, separate from rows within that block |
| Plane byte | Issued physical store offset within a named source plane |

Ordinary SeriesPack and bound TuplePack filters already agree on original-row
bits. A native TuplePack predicate returns packet-byte bits; casts cannot map
those to rows. Use `plan.ordering().offset(row, slot)` and decide how several
fields combine. The [grouped filter example](filters.md#convert-a-grouped-packets-result-to-row-coordinates)
performs that translation and intersects predicates over the same window.

SeriesPack, TuplePack and Intervals call their bitmap argument `selection`, with the same operations:
`all`, `none`, `bits`, `is_all`, `is_none`, `covers`, `contains` and `mask`.
Default construction means none; ordinary command defaults spell `all()`.
Selections borrow backing and preserve original positions. They do not decide
whether an excluded value is NULL, retired, absent or merely unnecessary.

Logical selection and physical read permission remain separate. TuplePack's
ordinary operations read active units only. An author can explicitly choose
`read_access::common_window` when its owner grants initialized, read-isolated
access through the bounded first-to-last selected window. That grant changes
possible reads, including preserving writes' old-data reads; stores and effects
remain active-only. Attachment and stride geometry alone do not supply it.

| Backing | Origin and read grant |
| --- | --- |
| SeriesPack numeric u16 words | Origin divisible by 16; one bit per original row |
| TuplePack numeric u64 words | Arbitrary origin; crossing-word windows stay bounded |
| Intervals exact bitmap bytes | Arbitrary origin; exact last byte; `complement` accepts an owner's exclusion bitmap |
| Shared `bitset256` | Numeric word `i/64`, bit `i%64`; 256 original positions |

Backing width is not logical grain. A wider load cannot read past a narrower
backing grant. `contains`/`mask` require a covered window; checked commands admit
that coverage first. [Selection](selection.md) owns exact tails, overflow,
complement and typed-block contracts.

`bitset256` is a zero-initialized value with natural alignment, count/rank/window
operations and explicit portable little-endian load/store bridges. Bec256 accepts
it directly. It imposes neither Frame block semantics nor an execution packet
width on another consumer.

## Use one effects protocol

`ikea::journal` records `{source, plane, offset, size}` in preallocated storage.
A source is the actual named view or owner, including a substituted leaf. Every
Ikea effect source (views, the Intervals editor, Hashtable tables, StreamPack
blocks, Bec256 destinations and `seriespack::dense_output`) exposes
`plane_storage(plane)`. A record resolves uniformly as that span's
`data() + offset`. Resolve
that identity to owner/partition/generation coordinates before the named object
dies **or its placement changes**: a record does not carry its plane origin. An
owner that may retarget a view before resolving should keep the
`plane_storage(plane).data()` it issued against and compare it at resolution.
Measured on single-row TuplePack writes, carrying that origin in every record
cost 8–33% on NEON and 5–17% on SeriesPack substituted writes on x86, so the
check belongs to the owner rather than the hot hook. A pointer to a copied
metadata wrapper is not the original source identity.
The journal can extend its last record when stores touch. `used` counts current
coverage records, so it is not an append-only event cursor. Use a fresh journal
for per-command resolution, or resolve the complete current coverage.

A caller can bind the next block after resolving the previous command's complete
journal against its original mapping. Existing bindings retain their module's
plan/view stability requirements. A trusted
effects adapter can instead translate each span immediately into pre-admitted
owner coordinates. That mapping must retain the correct backing generation;
keeping an owner pointer while replacing its storage is insufficient. Neither
option grants isolation or publication authority.

Checked mutations use the journal. Trusted mutations accept the `effects`
concept: `before(source, plane, offset, size) noexcept`. `no_effects` explicitly
opts out where the enclosing author already accounts for private writes. A hook
cannot fail, allocate or suspend during execution. Hashtable retains its owner
`enter`/`prepare` admission and resolves records through `plane_storage`; sharing
the hook does not remove that protocol.

Effects describe **issued bytes**, including preserved neighbors, rather than
changed bits. Logical destinations, old-value dependencies and summary writes
are different obligations. An owner must protect the physical read/store grant
even if two logical mutations name different bits in one packed byte.

Capacity queries count **additional records for a command**, conservatively,
before opportunistic coalescing. They do not include existing journal entries.
Invalid coordinates return an error where needed; a capacity bound is not command
admission. Initialization has a separate query because it can write owned slack
and omit old-value observation. Storage shortage uses `error::capacity`; a full
journal uses `error::effects`.

The [integration guide](integration.md) follows source resolution, maintenance and
coherent publication. Ikea effects supply neither beforeimages nor a transaction.

## Keep data headless and frame shared descriptions

A layout describes the byte law only. Logical count, attach-time origins, strides,
plane placement and semantic field identity belong to the parent. Internal code
offsets or reservation geometry remain law facts. Per-block mutable state such
as StreamPack assigned count is data in caller-placed planes.

A parent can persist one layout in a schema shared by many blocks. No Ikea data
block requires a descriptor, tag or version header. Encoded layout descriptions
share this schema envelope:

| Bytes | Meaning |
| --- | --- |
| 0–1 | Module tag |
| 2 | Module byte-law version |
| 3–4 | Little-endian body length |
| 5 onward | Module-owned canonical body |

`encoded_size`, `encode(std::span<std::byte>)` and
`decode(std::span<const std::byte>)` agree across the dynamic layouts. Decoders
require exact length and reject unknown tags, versions and noncanonical bodies.
SeriesPack decodes a `layout_value` containing K/H/geometry, checked against a
compile-time `layout` during attachment. Its eight-byte schema contains no count
or stride. TuplePack preserves all 128 code triples. Bec256 has one fixed law
and no layout description; `erased_table` is a runtime owning binding with no wire
form. Persist no native view, C++ layout object or function table.

Independent valid child descriptions do not prove that values, validity,
geometry or overrides belong to one version. The parent retains and publishes
that coherent interpretation.

## Errors, authors and execution profiles

Modules own typed errors and cold `describe(error) -> std::string_view` diagnostics.
Shared conditions have shared meanings: `range`, `value`, `capacity`, `effects`,
`overlap`, `description` and `contents`. Module-specific encoded-content errors
remain meaningful; operation outcomes such as Hashtable `absent` are statuses.
Error enum ordinals are not a persisted or cross-module status format.

Ordinary names live in the module namespace. `author` holds expressions,
substitution, observations and shared execution mechanisms; `native` holds
ISA-shaped carriers and bodies; `detail` is private. Shared CPS lives in
`ikea::author::chain`, with its own `chain_error` and `make` builder.

Current supported execution profiles are x86-64-v3 and ARMv8 SIMD, checked once
by `ikea/config.h`. All ordinary entries exist on those profiles. Ordinary
signatures and wire laws are target-neutral so future backends can implement
them without a new user vocabulary. Native carriers retain their ISA contracts.
