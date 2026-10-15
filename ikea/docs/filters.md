# Filtering packed values

SeriesPack and TuplePack can compare unsigned values with a scalar and return a
position mask. `ikea::comparison` selects `equal`, `less`, `less_equal`, `greater`
or `greater_equal`. The right operand is a full `uint64_t`: it is never truncated
to the stored width. A query outside that width therefore has the ordinary
mathematical result. Signed, floating-point, string and composite-key meanings
belong to the enclosing representation.

Filters borrow storage and return a mask without allocating, materializing a
value array, publishing mutation effects or suspending. The owner keeps the source
stable and readable through the call. Zero incoming activity reads no source.

The maintained [ordinary example](../examples/filters/ordinary.cpp) combines
SeriesPack and TuplePack masks and reuses a TuplePack projection for a range
predicate. [The composition example](../examples/filters/composition.cpp) shares
one range-and-count consumer between 12-bit and 15-bit SeriesPack sources. Both are
built and run by `ikea_validate`, as `ikea_example_filters_ordinary` and
`ikea_example_filters_composition`.

## SeriesPack

A checked call accepts a window of zero to 64 original values:

```cpp
using ikea::comparison;
auto selected = ikea::seriespack::filter<comparison::less>(
    source, first, count, 100, incoming);
```

Bit `i` in the result names `first+i`. Bits outside `count` are zero, and an
inactive position cannot become selected. `source` is an admitted SeriesPack
view or dense source; the complete window must fit its extent, even if some
positions are inactive. Out-of-range windows fail before reading data. Source
placement can include tile gaps, separate heads and either Local or Striped
payloads. A nonempty aligned group may read other occupied values in its window;
activity does not grant permission to overread the source or its occupied fields.

`filter_unchecked` has the same arguments and requires the caller to prove the
window bounds. It is inlinable into a consumer. A prepared logical composition
also offers `prepared.filter<Op>(first, count, rhs, incoming)`; substituted children
retain their original value meaning and coordinates.

An authored consumer can include `<ikea/seriespack/author/filter.h>` and call
`filter_values<Op>(native_values, rhs, incoming)` without reloading values.
The ordinary native carrier holds sixteen values; native group carriers support
16/32/64 on NEON and AVX-512. Both return one bit per original lane. These bodies
can be used inline or inside the existing CPS stages. Fuse cheap neighboring
predicates instead of requiring a call boundary for each one.
For reusable stages, [source guidance](source.md) describes normalizing decoded
lane meaning to share downstream bodies while retaining measured fused paths.
The selected default remains an inline complete filter. Use shared canonical
consumers inside an existing CPS operation when avoiding repeated specialization
justifies their measured cost; do not enter CPS solely to execute one small
filter. Keep a range test's two comparisons together after one decode.

For example, the composition example converts each decoder's `values<K>` into
`native::values<16>{decoded.v}` and uses `chain<16, 4>` throughout. Its shared
consumer sees unsigned 16-bit lanes and original-position activity. The source
width no longer specializes the comparisons or reduction. This is a native
carrier conversion, not a stored-format conversion; full-width queries retain
their meaning even when they exceed the original source's range. The owner
keeps the plan, context and storage alive for the complete synchronous chain.

## TuplePack

A TuplePack code is an unsigned 1–8-bit value inside one physical byte. Prepare
a one-code projection across the required rows and bind its filter:

```cpp
auto plan = tp::reader<64, 32>::make(layout, std::array<tp::byte, 1>{code_rank});
auto read = tp::bind_reader(*plan, source);
auto predicate = tp::bind_filter(*read);
auto selected = predicate->filter<comparison::equal>(first, 7, incoming_rows);
```

The named plan and view must outlive the filter. Binding rejects empty, hole-only
and multi-code maps. Bit `r` names original row `first+r`, just as in the bound
reader. Only active rows require storage; inactive rows perform no payload access.
`filter_unchecked<Op>` skips window admission after the caller has established it.
It requires `first <= source.size()`, activity confined to `Rows` bits, and every
active row in bounds. Unlike SeriesPack's complete-window contract, inactive
TuplePack tail rows need no storage. The checked entry rejects activity outside
`Rows`, including for a query whose result is known without loading payload.
Tight, fully active byte columns admit comparisons on encoded bits directly;
other placements retain the prepared projection's access rules.

For an already projected packet, `tp::filter_values<Op, N>(packet, rhs, active)`
returns one bit per **packet byte**. Here activity also uses packet-byte
coordinates. The author excludes holes, inactive rows and unused packet capacity;
`plan.ordering()` defines how groups, slots and original rows map to those bytes.
This entry permits several predicates to share one projection and combine their
masks without rereading the tuple. Comparing a multi-byte integer requires an
explicit composition; byte-code comparison does not silently invent that meaning.

Include the module umbrella or its `filter.h` for ordinary calls; link the
corresponding Ikea library. SeriesPack's native author entry lives in
`seriespack/author/filter.h`. Execution uses explicit AVX2, AVX-512 or NEON
bodies under the module's existing compile-time ISA selection; there is no
additional runtime filter registry or stored layout to configure.

## Evidence

[Packed filters research](../../workbench/spikes/packed-filters/README.md) compares
complete inlined consumers with independent fused kernels, separates dependent
latency from independent throughput, and records direct packed alternatives.
[Shared-stage measurements](../../workbench/spikes/filter-composition/RUNTIME.md)
charge complete consumers on Zen 5, Granite Rapids and Neoverse V2. They include
ordinary calls, CPS entry, shared stages, range tests and TuplePack projections.
