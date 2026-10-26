# Selections and 256-position values

A selection borrows bits naming original positions. It does not compact or
renumber them. SeriesPack and TuplePack use row coordinates; Intervals uses rank
coordinates. Frame's coarse block selection names whole blocks, a separate
coordinate space. Choosing rows does not establish presence, NULL validity or
permission to read inactive storage.

The three maintained selection types have the same operation names:

```cpp
auto selected = module::selection::bits(first, words);
if (!selected.covers(begin, count)) {
    // Reject the command before accessing data or changing outputs.
}
const auto active = selected.mask(begin, count); // count is 0..64
// active bit r names original position begin+r.
```

`selection::all()` and `selection::none()` need no bitmap storage. A default
selection is `none`. `is_all()` and `is_none()` recognize these named cases;
they do not scan a bitmap for its population. Ordinary operations which select
everything by default spell that default explicitly as `selection::all()`.

`covers(first,count)` checks whether a command's positions have backing and
rejects coordinate overflow. `contains(position)` and `mask(first,count)` are
trusted extraction methods: establish coverage before using them. A zero-length
mask reads no backing. `all` and `none` cover any non-overflowing coordinate
window. Keep borrowed bitmap storage stable through admission and execution,
and retain it across an owner wait if the unfinished operation still uses it.

| Module | Borrowed backing | Coordinate law |
| --- | --- | --- |
| SeriesPack | `span<const uint16_t>` or `span<const bitset256>` | u16 word j names `first+16*j+r`; bitset j names `first+256*j+r`. First is divisible by 16. |
| TuplePack | `span<const uint64_t>` | Numeric bit r of word j names `first+64*j+r`; any origin is valid. |
| Intervals | `span<const std::byte>` | Bit r of byte j names `first+8*j+r`; any origin is valid, with no readable word padding. |

Each logical bit selects one row or rank. The backing word width is independent
of logical selection grain and native execution grain. Intervals additionally
offers `selection::complement(first,bytes)` for a borrowed plane whose set bits
mean exclusion, such as consumer-owned tombstones. It neither owns nor changes
that plane. Its `next(first,limit)` and `previous(limit,end)` helpers visit only
explicitly selected ranks and return the supplied end sentinel when absent;
positions outside explicit bitmap coverage require no access.

## Share a 256-position value

`<ikea/bitset256.h>` supplies an ordinary 32-byte value, separate from the
borrowed selectors and native register carriers:

```cpp
ikea::bitset256 positions; // all zero
positions.set(0);
positions.set(63);
positions.set(64);
positions.set(255);
assert(positions.count() == 4 && positions.rank(64) == 2);
assert(positions.rank(256) == positions.count());

auto rows = ikea::seriespack::selection::bits(32, positions);
auto ranks = ikea::intervals::selection::bits(0, positions);
assert(rows.mask(32,16) == 1 && ranks.contains(255));
```

Position i is numeric bit `i%64` of `words[i/64]`. `contains`, `set`, `count`,
`empty`, `full`, `rank` and `mask` operate on this numeric law. Rank counts set
positions strictly before its argument, including the endpoint 256.
`mask(first,count)` extracts at most 64 positions, requires the window to fit,
and returns bit r for first+r. `mask(256,0)` is valid. The value has natural u64
alignment; an owner can request stronger alignment for its own storage.

`bitset256::first_n(n)` constructs positions `[0,n)` for `n` from 0 through
256. Intersection uses `a & b`; `positions.for_each(callback)` visits set
positions in increasing order. These preserve the value's coordinate domain;
they do not convert block positions into rows.

SeriesPack also accepts `bits(first, span<const bitset256>)` for consecutive
256-row blocks, including Frame FILTER output. It borrows the block objects
without flattening/copying or treating adjacent arrays as one u64 array. Its
origin follows SeriesPack's divisible-by-16 rule; bounded masks may cross blocks.

Every selection type accepts a named `bitset256` at an original-coordinate
origin, without copying or aliasing its u64 objects as u16. The selector borrows
that value; constructing one from a temporary block is rejected. StreamPack's
private liveness companion uses Intervals' selection. This common value does
not make coarse block selection interchangeable with row selection or rank
selection.

`bitset256::load(span<const byte,32>)` and `store(span<byte,32>)` bridge the plain
byte law used by Bec256: position `8*j+b` is bit b of byte j. They preserve that
law on both little- and big-endian targets. The `words` array itself uses host
numeric representation; do not persist its raw object bytes as a portable
description. Native load/store or Boolean operations can stay in their native
carriers without materializing this value between stages.
