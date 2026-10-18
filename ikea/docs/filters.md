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
The [guarded example](../examples/filters/guarded.cpp) selects head-first
SeriesPack evaluation and skips a subsequent TuplePack projection when the
first predicate rejects the entire group.

## Choose the consumer path

| Work | Start with | Change when |
| --- | --- | --- |
| One predicate | The ordinary packed filter | A measured enclosing consumer benefits from sharing or guarding |
| First equality match | SeriesPack `indexof` | An owner-specific search can avoid work beyond the first useful match |
| Several tests of the same values | One native read/projection and inline mask combination | Direct packed comparisons avoid enough reconstruction to win instead |
| Repeated formats feeding the same substantial computation | A shared canonical consumer inside an existing composition | Keep the direct path for packed winners and small hot calls |
| Existing eight-bit head plus expensive suffix | Opt-in `composition::guarded` | Heads often resolve entire active groups; test mixed and shared-prefix inputs too |
| Conjunction over different projections | Guard a later expensive projection on a nonzero mask | Unconditional fusion wins when the later work is cheap or branching is unpredictable |

Use the examples above as complete call patterns. [Building fast consumers](consumers.md)
connects preparation, placement visibility and execution boundaries across Ikea.
The ordinary filter retains packed-bit and encoded-column shortcuts; normalizing
all values before every predicate would discard some of those wins.

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
payloads. A nonempty window may read any of its occupied values;
activity does not grant permission to overread the source or its occupied fields.

`filter_unchecked` has the same arguments and requires the caller to prove the
window bounds. It is inlinable into a consumer. A prepared logical composition
also offers `prepared.filter<Op>(first, count, rhs, incoming)`; substituted children
retain their original value meaning and coordinates.

### Find the first match

For the first equality match, use `indexof(source, first, count, query, incoming)`.
It returns `expected<unsigned, error>`: the lowest active matching position
**relative to first**, or `count` when absent. Check `position < count` before
using `first + position`. A zero-length window returns zero. Bounds, activity,
query-domain checks and readable storage follow the filter contract above;
empty activity reads nothing. `indexof_unchecked` omits window admission, and
prepared compositions offer the same `prepared.indexof(...)` operation.
No separate contains operation is needed: compare the returned position to count.
Eligible complete Local windows reuse a prepared residual query. ARM can stop
after the first complete-key match. Tuned Zen 5/Granite AVX2 evaluates the whole
window for four-to-seven residual planes; GFNI + AVX-512 does so when the body
is at least 32 bits. This avoids unpredictable tile-hit branches, which can
hurt reciprocal throughput even when they reduce dependent latency. Other
expressions and edge windows retain the existing filter lowering. In consumers
with random tile hits, compare `indexof` with full-filter plus first-bit
extraction; ARM's whole-table integration currently favors the latter. The [ARM qualification](../../workbench/spikes/filter-composition/ARM-INDEXOF.md)
and [x86 qualification](../../workbench/spikes/filter-composition/X86-INDEXOF.md)
keep both metrics and those losses visible.

### Compare a Local bitplane tile

For a consumer that already owns a Local bitplane word,
`<ikea/seriespack/author/local_equal.h>` provides
`composition::local_equal8<K> match(query)` and `match(word)`, returning an
eight-bit row mask. K is 1..7 and query must fit K bits. Construct once per query
and reuse across tiles. The low K bytes of the word hold the planes in wire
order; other bytes are ignored. The helper performs no loads. It does not grant
permission to load eight bytes from a K-byte leaf. An owner may use a wider load
only after proving all those bytes readable under its synchronization; ordinary
filters preserve exact bounds.
The same helper uses GFNI when enabled, ARM shifted reductions, or a scalar
reduction. On the measured Zen 5/Granite AVX2 targets, BMI2 prepares four-to-seven
query planes once; generic BMI2 builds avoid assuming fast PDEP. GFNI also works
without AVX-512 through a vector compare and movemask. ISA selection is a build
decision, with no new runtime mode or public search knob. Keep query preparation
inside per-query timing when evaluating a consumer; a prepared-core instruction
count omits that work and rank selection.

### Filter complete groups

For 8, 16, 24, …, 64 values starting at a multiple of eight, the ordinary call
uses native bounded groups, including an eight-row final tile. Plane origins
need only byte alignment. A 24-value 16-bit Local payload needs exactly 48 bytes,
not a padded 64-byte load. Other logical edges remain scalar. Keep width, count
and dense placement visible to the inlined consumer when known.

### Reuse decoded values

An authored consumer can include `<ikea/seriespack/author/filter.h>` and call
`filter_values<Op>(native_values, rhs, incoming)` without reloading values.
The ordinary native carrier holds sixteen values; native group carriers support
16/32/64 on NEON and AVX-512. Both return one bit per original lane. These bodies
can be used inline or inside the existing CPS stages. Fuse cheap neighboring
predicates instead of requiring a call boundary for each one.
For reusable stages, [consumer guidance](consumers.md#share-the-work-preserve-the-fast-algorithm)
describes normalizing decoded lane meaning while retaining measured fused paths.
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

### Select head-first execution

For an expression with an eight-bit head and a nonempty payload, opt in after
selecting its children:

```cpp
namespace cp = ikea::seriespack::composition;
const auto expression = cp::guarded(cp::describe(source));
const auto prepared = cp::prepare(expression, source.size());
auto selected = prepared->filter<comparison::equal>(first, count, query, incoming);
```

`guarded` owns the small expression nodes and borrows the same named sources as
`describe`. It changes filtering execution, not the stored format, unsigned
meaning, admission or mask coordinates. It follows substituted payloads, gapped
heads and the owning head's tile geometry. Construct the annotation after
`with_payload`/`with_tail`; it is a read-composition choice, not a writable node.

Complete aligned sixteen- or eight-row groups compare the head first. If no active lane
ties the query's head, the result is already known and the payload is skipped.
Ordered comparisons retain the mask of lanes resolved by the head; the result
need not be zero. Otherwise the ordinary payload filter completes the comparison.
Logical edges shorter than eight rows use ordinary scalar evaluation. Empty activity and
out-of-domain queries retain their existing shortcuts. The owner still admits
the complete source and keeps it stable for the operation.

An inline consumer uses the existing
`cp::filter8_unchecked<Op>(expression, first, query, incoming)`,
`cp::filter16_unchecked<Op>(expression, first, query, incoming)` or
`cp::filter_unchecked<Op>(expression, first, count, query, incoming)` entry.
The eight- and sixteen-row entries require complete windows and `first` divisible
by eight and sixteen respectively. These are row constraints, not byte alignment.
No stored flag, runtime registry or format preset is needed.
The annotation also passes through ordinary read evaluation without changing its
value meaning. It adds no scheduler or CPS boundary.

Choose this path when a cheap head often avoids an expensive suffix. Frequent
ties or mixed easy/hard windows can make branches lose throughput, even when
latency improves. Ordinary filters keep their current execution; an ISA or width
alone does not opt a consumer into guarding. Low final match selectivity is not
sufficient: heads may tie even when every suffix comparison eventually fails.
Measure resolved groups and their predictability, not just the final mask density.
The sixteen-row implementation uses NEON vector-zero reduction for full activity,
AVX2 mask extraction and AVX-512
mask-register tests. Sparse activity resolves only the active lanes.

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

For a conjunction, the consumer can skip the next projection at an existing
composition boundary:

```cpp
auto selected = first.filter_unchecked<comparison::equal>(row, key, incoming);
if (selected)
    selected &= second.filter_unchecked<comparison::less>(row, bound, incoming);
```

Here both filters have already been admitted for `incoming` in the same original
row coordinates. Do not widen a projection whose proof covers only `selected`.
Passing the original activity to the second filter preserves its dense projection
where available; its result is intersected afterwards.
Forwarding `selected` instead preserves the result too, but can select a more
expensive sparse reader. A zero mask skips all work in the second call. The
guarded example demonstrates this across SeriesPack and TuplePack. It is an
ordinary inlinable branch, not another TuplePack mode. Retain unconditional fused
predicates when their throughput wins; guarding cheap work is not automatically
beneficial.

Include the module umbrella or its `filter.h` for ordinary calls; link the
corresponding Ikea library. SeriesPack's native author entry lives in
`seriespack/author/filter.h`. Execution uses explicit AVX2, AVX-512 or NEON
bodies under the module's existing compile-time ISA selection; there is no
additional runtime filter registry or stored layout to configure.

## Evidence

[Eight-slot K1–64 results](../../workbench/spikes/filter-composition/seriespack-tiles.md)
tabulate Local filters against independent fused controls for all five predicates.
The [tile qualification](../../workbench/spikes/filter-composition/TILE-FILTERS.md)
also covers wider windows, actual bucket geometry, sparse activity and byte offsets.
Dependent latency and reciprocal throughput remain separate. The
[earlier sixteen-slot table](../../workbench/spikes/filter-composition/seriespack-widths.md)
records the preceding mask-reduction repair. The
[mask audit and harness correction](../../workbench/spikes/filter-composition/MASK-GRADUATION.md)
explain the NEON u32 repair and the limits of older correlated-stream results.
[Packed filters research](../../workbench/spikes/packed-filters/README.md) compares
complete inlined consumers with independent fused kernels, separates dependent
latency from independent throughput, and records direct packed alternatives.
[Shared-stage measurements](../../workbench/spikes/filter-composition/RUNTIME.md)
charge complete consumers on Zen 5, Granite Rapids and Neoverse V2. They include
ordinary calls, CPS entry, shared stages, range tests and TuplePack projections.
[Early-exit measurements](../../workbench/spikes/packed-filters/EARLY-RESULTS.md)
separate head rejection, suffix cost, mask reduction and unpredictable branches.
[Graduation measurements](../../workbench/spikes/packed-filters/EARLY-GRADUATION.md)
compare the actual `guarded` composition with the spike controls and current
defaults on AVX2, AVX-512 and NEON, including placement costs and losing branches.
