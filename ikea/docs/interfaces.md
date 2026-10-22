# Composing Ikea interfaces

Compose operations by matching their value meaning, coordinates, accessible bytes
and lifetime. A similarly named reader or a matching register width does not
establish those facts. This guide connects the existing module interfaces; the
module references specify their exact checks and access bounds.

## Follow the work, then the names

A **description** specifies how values mean bytes. **Placement** associates that
law with actual addresses and accessible extents. A **binding** retains enough
of that association or a prepared operation to reuse it. **Admission** is the
validation that establishes a stated requirement, such as sufficient storage or
a valid command. A **proof** here means those established facts, not an object
that acquires locks or prevents the owner from changing storage.

The modules divide this work differently:

| Module | Describe and place | Prepare and execute |
| --- | --- | --- |
| SeriesPack | `format<K, geometry, H>` is a compile-time byte law; `view<F, Byte>::attach` checks plane placement and count. | `bind_decoder` prepares reads; `bind_mutation` prepares source-aware writes. Reuse their point/range operations. |
| TuplePack | `layout::make` validates code locations in one unit; `view::bind` checks the units' storage, count, stride and offset. | `reader::make` prepares a reusable code map and packet grouping; `bind_reader` associates it with a named view. Writer and construction plans have their own admitted commands. |
| StreamPack | `layout::prepare` derives reservation and used-length geometry; `view::bind` checks the payload and metadata windows. | Initialize new storage or validate recovered contents, then use value operations. Author dispatch keeps schema selection outside repeated typed work. |
| Intervals | A source projects ordered endpoints; geometry adds an origin. There need not be a layout or a stored descriptor. | Ordinary search needs no separate binding. Author search can retain numeric preparation or a staged local window when useful. |
| Bec256 | A source has an exact body length and caller-supplied population; `source::admit` validates its framing and readable storage. | Decode an admitted source, or `prepare` a transient encoding before writing a named destination. |
| Hashtable | The owning runtime `descriptor` retains storage and operation bindings. | Use its selected table operations and rebuild lifecycle. This descriptor is not a portable byte description. |

For example, the [SeriesPack ordinary program](../examples/seriespack/ordinary.cpp)
attaches planes once, binds mutation, initializes them, replaces a point and a
range, then reuses a decoder. The [TuplePack ordinary program](../examples/tuplepack/ordinary.cpp)
separates its reusable code plan from the concrete storage binding. Renaming
both layout constructors would not make their input or retained state equivalent.

`assume_valid` reuses an already established placement/content requirement stated
by that entry. `_unchecked` reuses the command facts stated by its checked peer.
Neither establishes initialization, lifetime, synchronization or permission to
access additional bytes. The [owner integration guide](integration.md) follows
one complete mutation through those responsibilities.

## Give every position a coordinate system

| Coordinate | Meaning |
| --- | --- |
| Row | An original logical value/record position in a pack view. |
| Rank | An ordered local interval or StreamPack reservation identity; excluding it does not renumber later ranks. A TuplePack code rank instead indexes a layout's code description. |
| Window bit | Bit `r` of an ordinary SeriesPack or bound TuplePack filter names original row `first+r`. Combine predicates over the same original rows, origin and window. |
| Packet byte | One decoded byte slot in a native TuplePack packet. Grouping, repeated code slots and holes can separate this from a row position. |
| Block bit | A consumer-defined coarse coordinate, such as one bit per 256-row block in the [Frame study](../../workbench/spikes/ikea-frame/README.md). It is separate from row activity within a selected block. |
| Plane byte | An issued physical write offset relative to one admitted source plane. It is not a row, code or changed-bit index. |

The [grouped filter example](filters.md#convert-a-grouped-packets-result-to-row-coordinates)
projects two TuplePack fields with a hole, compares their packet bytes and gathers
each chosen map slot back to row bits using the plan's ordering. It then intersects
those masks with a SeriesPack result over the same `first=7` window. A cast cannot
perform that mapping; a consumer must also decide how several fields combine.

A **selection** says which original elements an operation includes. A filter
**mask** can carry such a result, but does not say whether excluded elements are
absent, NULL, retired or simply unnecessary to this command. Keep that meaning
with the consumer.

SeriesPack `row_selection` stores sixteen row bits per u16 with an aligned origin;
TuplePack `selection` stores row bits in u64 words with an arbitrary origin;
Intervals `rank_selection` borrows exact bytes and can invert an owner's bitmap.
All have one bit per logical element. Their storage widths are not selection
grain, and widening one into another can read beyond the original grant.
The Frame study's coarse block selection introduces another coordinate, rather than a
fourth interchangeable fine bitmap format.

## Carry writes with their actual source

`ikea::write_journal` records `{plane, offset, size}`. It suits a caller that already
knows the single source to which all entries belong. `ikea::source_write_journal`
records `{source, plane, offset, size}`. It distinguishes actual named leaf views
when a composition substitutes children or writes several destinations.
SeriesPack's composition namespace keeps the latter's name
`source_write_journal`; it does not give it the other type's name.

The [SeriesPack integration adapter](../examples/seriespack/integration.cpp)
keeps its named view and journal storage inside a stable work object. It maps
each reported source and plane to an owner offset before releasing those objects.
An address identifying a local view cannot be retained after the view dies, even
if its backing bytes remain allocated.

Both journals describe **issued bytes**, including neighbors preserved by packed
stores. Logical destinations and maintenance dependencies serve different work:
a two-row mutation can issue a wider byte store, and its summary can depend on
untouched values. Hooks run before their associated local store group; reserve
their capacity before execution. They cannot fail, allocate or suspend midway.
They do not supply beforeimages or publish a transaction.

SeriesPack's supported [scalar point header](seriespack/reference.md#scalar-point-building-blocks)
also offers raw `get`/`set` leaves. Raw stores emit neither journals nor summary
changes. Use the bound mutation operation when those consequences are required;
calling a checked raw setter does not add them.

## Cross byte and error boundaries explicitly

SeriesPack and TuplePack expose unsigned bytes for packed arithmetic. StreamPack
and Bec256 expose `std::byte` spans for object bytes. When an owner passes an
unsigned-byte buffer to an object-byte operation, use the standard span view:

```cpp
#include <array>
#include <cstdint>
#include <span>

std::array<std::uint8_t, 256> buffer{};
std::span<std::uint8_t> storage{buffer};
std::span<std::byte> object_bytes = std::as_writable_bytes(storage);
std::span<const std::byte> input_bytes = std::as_bytes(storage);
```

This preserves the byte extent and borrowing; it does not validate the target
format, create integer objects, change endian order or acquire write permission.
Keep any reverse interpretation at the owner's admitted placement boundary.

Each pack owns its error domain. In particular, `streampack::error::retired`
belongs to the value owner; Intervals only receives the owner's eligibility
filter and cannot diagnose retirement. The five pack namespaces supply
`describe(error) -> std::string_view` for cold diagnostics. A caller can attach
its module/operation context when reporting or propagating an error. Error enum
values are not a persistent format or a common cross-module status code.
Hashtable's operation statuses and `std::errc` retain their distinct meanings.

## Let the parent frame child descriptions

Portable child descriptions describe different things: a SeriesPack descriptor
includes array count and placement geometry; TuplePack encodes a unit layout;
StreamPack encodes a shared value schema with assigned count in its control
plane. Bec256 body interpretation needs population and length from its parent.
Persist each module's encoded form with the parent's field identity and exact
extent; validate it through that module's decoder. Never serialize a native
layout, view, runtime Hashtable descriptor or operation table as its wire form.

The parent must still associate the children with one coherent logical version.
Independent valid descriptions do not prove that values, geometry, validity or
overrides belong together. Lifetime retention preserves backing; the owner's
version and publication rules preserve that association.

[Module usage](../README.md) supplies concrete operations. [Building consumers](consumers.md)
explains where fusion, preparation and traversal earn their cost; [source guidance](source.md)
locates the supported author entries and shared mechanics.
