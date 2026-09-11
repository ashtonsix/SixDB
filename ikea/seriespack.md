# SeriesPack: from values to native work

Suppose a structure holds unsigned counters that fit in twelve bits. It needs
to read one counter, replace selected counters, and sum those below a threshold.
An ordinary `uint16_t` array makes those operations easy, but reserves four
unused bits per value. SeriesPack supplies a packed representation together
with operations that can consume it directly.

We will follow the same ten values from the ordinary API down to their exact
bytes, then through scalar and native computation. The complete
[query example](examples/seriespack_query.cpp) compares materialized and native consumption;
the [basics example](examples/seriespack_basics.cpp) demonstrates mutation.
We keep the original values until we reach that update.

## Construct and read a counter

Include `<ikea/seriespack.h>` and link `ikea::seriespack`. `format<12>` chooses
the compact layout: eight values per physical **tile**, twelve bytes per tile.
Ten values need two complete tiles, so the owner supplies 24 bytes:

```cpp
#include <ikea/seriespack.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

int main() {
    namespace pack = ikea::seriespack;
    using Format = pack::format<12>;
    std::array<std::uint16_t, 10> values{7, 19, 42, 0, 4095, 3, 8, 91, 12, 6};
    alignas(64) std::array<std::byte, 2 * Format::payload::tile_bytes> bytes{};

    auto attached = pack::static_mutable_view<Format>::attach(
        values.size(), {{bytes, Format::payload::tile_bytes}, {}});
    if (!attached) return 1;
    auto writable = attached->as_dynamic();
    auto readable = writable.as_const();
    if (!pack::encode(writable, std::span{values})) return 2;

    auto value = pack::get(readable, 2);
    if (!value || *value != 42) return 3;
}
```

The runnable query example begins with these same attach/encode/get steps,
then continues through the query paths below. From the repository root, with
the `dev` preset configured as described in [Building](../BUILDING.md):

```sh
cmake --build --preset dev --target ikea_seriespack_query
./build/clang/dev/ikea/ikea_seriespack_query
```

These are Linux commands; prefix each with `orb -m ubuntu` from the Mac
workspace. The example checks its results and prints the sums.

`12` describes the unsigned stored domain, 0 through 4095. `uint16_t` is the
C++ type carrying the inputs. SeriesPack supports widths 1 through 64; the
enclosing structure chooses that width and any interpretation beyond unsigned
integers. Later snippets continue inside this `main`, using the same locals.

The owner supplies a byte span and the byte **stride** between tile starts.
Here the stride is twelve, so tiles are adjacent. `attach` checks that this
placement can hold the declared array. `encode` writes its values and zeroes
the six unused positions in the final tile. Only positions 0 through 9 belong
to the logical array; point reads reject later positions.

The static view retains `Format` in its type. `as_dynamic()` supplies the same
description to compiled operations; it does not transform the bytes.
`readable` and `writable` alias the same storage. Their owner keeps that storage
alive and controls conflicting accesses, just as it would for other borrowed
views. Attachment itself does not encode or allocate anything.

Ten values occupy 24 bytes here, compared with 20 in the input array. This
small example exposes the cost of rounding to complete tiles. With eight values
the layout uses 12 bytes instead of 16; with sixteen it uses 24 instead of 32.
The following byte map explains both the saving within a tile and the slack
at the end of our array.

## Follow the point read into the bytes

Within each compact tile, a value is split into a whole-byte **body** and four
low **tail** bits:

```text
body = value >> 4
tail = value & 15
value = (body << 4) | tail
```

For the first eight values, those pieces are:

```text
Value (decimal)    7   19   42    0  4095    3    8   91
Body  (hex)       00   01   02   00    FF   00   00   05
Tail  (hex)        7    3    A    0     F    3    8    B
```

The eight body bytes go first, in value order. The tails are transposed: one
byte collects bit 0 of all eight tails, another collects bit 1, and so on.
Bit `i` of each such byte belongs to tile-local value `i`.

```text
Value index        7 6 5 4 3 2 1 0     Stored byte
Tail bit 0         1 0 1 1 0 0 1 1        B3
Tail bit 1         1 0 1 1 0 1 1 1        B7
Tail bit 2         0 0 0 1 0 0 0 1        11
Tail bit 3         1 1 0 1 0 1 0 0        D4
```

That gives eight body bytes plus four tail bytes: twelve bytes for eight
12-bit values. This internal tail mechanism is called LocalPack.

The second tile holds 12 and 6 followed by six unused zero positions. The
complete encoding is therefore:

```text
                    Body bytes                   Tail bytes
Tile 0:   00 01 02 00 FF 00 00 05          |     B3 B7 11 D4
Tile 1:   00 00 00 00 00 00 00 00          |     00 02 03 01
```

Position 2 belongs to tile `2 / 8`, at local position `2 % 8`. Its body is
byte 2, `0x02`. Bit 2 of tail bytes `B3 B7 11 D4` gives low bits `0,1,0,1`,
or binary `1010`. Reconstructing `(2 << 4) | 10` produces 42.

The scalar implementation performs this extraction with a load of the four
tail bytes, masks and a bit-gathering multiply. It does not need to materialize
the other seven values. The same addressing rule works in later tiles:
`tile_base = payload_base + (index / 8) * stride`.

The bytes contain no count, width or ownership header. That is the meaning of
**headless**: the enclosing owner keeps the description needed to interpret
them. The array's values do not determine their own format.

## Read the whole array

To compute with an ordinary C++ loop, explicitly materialize the values:

```cpp
std::array<std::uint16_t, 10> decoded{};
if (!pack::decode(readable, {0, values.size()}, std::span{decoded})) return 4;

std::uint64_t scalar_sum = 0;
for (auto x : decoded)
    if (x < 100) scalar_sum += x;
// scalar_sum == 188
```

The requested range is half-open and uses original array positions. Output
slot zero corresponds to the range's first position. Its type must hold the
full 12-bit domain, so `uint16_t` is sufficient even though the input includes 4095. Neither the logical range nor the output includes the final six storage
positions.

Checked calls validate their arguments. For repeated calls whose range and
output facts are already established, a caller can retain a bound reader:

```cpp
auto reader = pack::bind_reader(readable);
if (!reader) return 5;
reader->decode({0, values.size()}, std::span{decoded});
```

Binding selects compiled endpoints for the existing description. The last
call trusts that the output is large enough, has a suitable type, is disjoint
from the encoded bytes, and that the storage remains valid. Those facts are
visible in this example. Static `trusted_get` provides a corresponding point
path with the format kept in the type.

## Consume decoded values in registers

The loop above expresses useful work, but stores and reloads a decoded array.
Ikea also lets a packed reader feed a native consumer directly. The supplied
`selected_sum` is one authored function with this body:

```cpp
auto values = read(ops, source, rows, active);
auto keep = ops.unsigned_less(values, cutoff, active);
return ops.sum(values, keep, modulo_u64_sum{});
```

Here `source` is an expression describing what to read. `Ops` supplies the
concrete operations. The decoded `values` local feeds both the comparison and
the sum, so the algorithm expresses reuse rather than two independent reads.
The arithmetic law is unsigned addition modulo `2^64`, matching our C++ sum.

On ARM, include `<ikea/seriespack/composition_neon.h>` and continue with the
same encoded array. The [complete query example](examples/seriespack_query.cpp)
also chooses an available x86 executor at compile time:

```cpp
namespace comp = pack::composition;
namespace native = pack::neon;
auto source = attached->as_const();
auto parts = comp::describe(source);
using Ops = native::composition_ops<Format, 0>;
Ops ops;

auto first = comp::selected_sum(ops, parts, native::tile_position{0},
                                 Ops::active_all(), std::uint64_t{100});
auto last = comp::selected_sum(ops, parts, native::tile_position{1},
                                Ops::active_bits(0b11), std::uint64_t{100});
auto native_sum = first + last;
// first == 170, last == 18, native_sum == scalar_sum == 188
```

`describe` names the body's and tail's roles while retaining their actual
source. It reads no values. For this format, the ordinary expansion reads both
parts and performs the same four-bit join we derived from the wire. The named
`source` stays alive because `parts` borrows that object, as well as relying on
the underlying storage.

This NEON executor reconstructs eight values in 16-bit register lanes. It
compares them with 100, keeps the native predicate, and reduces selected values
without requiring an intermediate array. AVX2 and AVX-512 have their own
executors over the same wire.

The second invocation demonstrates three coordinate units. Its tile ordinal
is 1; its first original position is 8; its eight native lanes correspond to
positions 8 through 15. Only lanes 0 and 1 are active, so they contribute 12
and 6. The complete second tile is readable storage; the mask identifies which
positions are logical inputs. It does not shrink the reader's byte access.

Eight-value storage tiles happen to match this executor's grain. Other native
regions can combine several tiles, and wider values can require several
fragments per tile. The driver chooses regions that fit its storage and
logical range. Grain, working lanes and when to reduce to a scalar are execution
choices; they need not change the encoded representation.

The same authored function can also run with symbolic operations that record
its reads, comparison and reduction. That exposes dependencies without a
second handwritten algorithm. Defining another consumer or a different native
lowering builds on this separation; the [extension guide](seriespack/extending.md)
continues with a two-source example.

## Update selected counters

Now change positions 2, 5 and 8 to 100, 200 and 300. Add `#include <vector>` for
the reporting buffer. The basics example supplies the replacements as a
selection over `[2,10)`:

```cpp
constexpr pack::index_range rows{2, 10};
const std::array<std::uint64_t, 1> mask{0b01001001};
const auto selected = pack::selection::bitmap(rows.begin, rows.size(), mask);
const std::array<std::uint16_t, 8> replacements{100, 0, 0, 200, 0, 0, 300, 0};
const auto capacity = pack::write_effect_capacity(readable, rows, selected);
if (!capacity) return 6;
std::vector<pack::byte_span> coverage(*capacity);
pack::effect_output effects{coverage};
if (!pack::write(writable, rows, std::span{replacements}, selected, &effects)) return 7;
```

Mask bit zero names original position 2. Bits 0, 3 and 6 therefore select
positions 2, 5 and 8, whose inputs come from replacement slots 0, 3 and 6.
The replacement array preserves those positions; it is not a three-value list.
The encoded array now represents `7,19,100,0,4095,200,8,91,300,6`.

Look at the update from 42 to 100. Its body byte becomes `100 >> 4`, or 6,
and bit 2 in each of the four shared tail bytes is updated from tail 10 to
tail 4. The other values' bits in those bytes must survive. This is why the
writer uses read-modify-write and why changing one logical value can touch
bytes shared with neighboring values.

`effects` reports coverage of physical writes. Its spans can overlap, and its
record count is independent of the selected-row count. The owner supplies the
reporting buffer and isolates the actual write footprint. A caller that does
not need reporting can omit the effect output.

Checked writes complete rejecting checks before changing either destination
bytes or effects. For example, 4096 would fail the 12-bit value-fit check.
This protects the caller from partial mutation on an error; publication and
synchronization still belong to the enclosing structure. The reported addresses
borrow the current storage, and do not themselves retain it or describe semantic
old/new values for an aggregate.

## Vary the arrangement when the caller changes

We have now used one description, one placement and several execution paths.
Three nearby alternatives show why SeriesPack keeps those choices separate.

A **strided placement** can leave room for sibling data. Using stride 16 for
our compact tiles gives twelve bytes of payload, four foreign bytes, then the
next twelve bytes. Occupied storage is still 24 bytes, while the address
envelope is 28. `required_extents` reports both quantities. The caller must
provide that placement when constructing or attaching its bytes.

The **bulk preset** for width 12 changes the geometry. It has a 64-value tile
with this arrangement:

```text
Bytes  0..31: body bytes for values  0..31
Bytes 32..63: body bytes for values 32..63
Bytes 64..95: byte j holds tail[j] in its low nibble,
                         tail[j+32] in its high nibble
```

This internal stripe mechanism is called ScanPack. A vector reader extracts
many residuals in parallel from its byte lanes. Our ten values would require
one full 96-byte tile, so changing to bulk is not a storage win for this tiny
array. It constructs different bytes; selecting a different CPU reader alone
does not.

A **head split**, such as `format<12, preset::filter, 8>`, places each value's
highest eight bits in a separate byte plane and leaves four bits in the payload.
This preset uses a striped four-bit payload, so it also constructs new bytes.
For the original 42, head 2 and payload 10 reconstruct `(2 << 4) | 10`.
These heads are value data, so they coexist with a headless metadata format.

For our predicate `x < 100`, a head below 6 certifies a match and a head above 6
rejects it. Head 6 needs refinement: its payload must be below 4. A sum of the
matching full values still needs their payloads; summing head bytes computes a
different quantity. The module exposes the parts needed for such compositions,
while efficient progressive head/payload traversal remains ongoing work.

The [contract reference](seriespack/reference.md) defines the supported bit
maps, head splits, extents and operation obligations beyond this example.
The [extension guide](seriespack/extending.md) explains how to add consumers
and lowerings. For performance comparisons and their scope, start with the
[SeriesPack benchmarks](../workbench/benchmarks/seriespack/README.md).
