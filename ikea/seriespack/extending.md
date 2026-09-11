# Extend a SeriesPack computation

Suppose two arrays share original positions: unsigned counters and their tags.
Compute the sum of counters whose tag equals a requested key, restricted by an
incoming selection. This combines two existing readers and native operations;
it needs no new packed format or change to SeriesPack's public API.

Read the [SeriesPack tour](../seriespack.md) first. The program below uses its
same ten counter values and adds a separately placed nine-bit tag array. Both
formats use eight-value local tiles and 16-bit native lanes. Their byte strides
differ, but lane `i` in either invocation names the same original position.

Tag 1 selects original positions 0, 2, 4, 7 and 9, so the expected sum is
`7 + 42 + 4095 + 91 + 6 = 4241`. Excluding position 4 through the incoming
selection gives 146.

## Run and extend the example

The complete [tagged-counter example](../examples/seriespack_join.cpp) owns both
arrays, constructs their bytes and compares a native computation with a
materialized reference. It also checks an empty selection and tag 512, which
cannot match a nine-bit stored tag. Query constants are not truncated to the
stored domain.

From the repository root after configuring the `dev` preset:

```sh
cmake --build --preset dev --target ikea_seriespack_join
./build/clang/dev/ikea/ikea_seriespack_join
```

Prefix Linux commands with `orb -m ubuntu` from the Mac workspace; see
[Building](../../BUILDING.md). The example selects NEON, AVX-512 or AVX2 from
compile-time feature availability, with no runtime CPU probing. A baseline
build checks the materialized result and reports that native work was skipped.

The new logical operation is this complete function:

```cpp
template<class TagOps, class ValueOps, class Tags, class Values,
         class Rows, class Active>
auto sum_where_tag(TagOps& tag_ops, ValueOps& value_ops,
                   const Tags& tags, const Values& values,
                   Rows rows, Active active, std::uint64_t wanted) {
    auto tag_values = comp::read(tag_ops, tags, rows, active);
    auto matches = tag_ops.unsigned_equal(tag_values, wanted, active);
    auto counters = comp::read(value_ops, values, rows, matches);
    return value_ops.sum(counters, matches, pack::modulo_u64_sum{});
}
```

`main` attaches the counter tiles with stride 16 and tag tiles with stride 12.
Its named const views produce `parts` and `tag_parts`. The following driver
chooses compatible native operations and covers the ten original positions:

```cpp
using ValueOps = native::composition_ops<Format, 0>;
using TagOps = native::composition_ops<TagFormat, 0>;
static_assert(ValueOps::lanes == 8 && TagOps::lanes == 8);
static_assert(ValueOps::L == TagOps::L);
ValueOps value_ops;
TagOps tag_ops;

// Bit i of candidates names original position i in this ten-value example.
const auto run = [&](std::uint64_t wanted, std::uint64_t candidates) {
    std::uint64_t total = 0;
    for (std::size_t origin = 0; origin < values.size(); origin += 8) {
        const auto count = std::min<std::size_t>(8, values.size() - origin);
        const auto bits = (candidates >> origin) & ((std::uint64_t{1} << count) - 1);
        if (bits == 0) continue;
        total += sum_where_tag(tag_ops, value_ops, tag_parts, parts,
            native::tile_position{origin / 8}, TagOps::active_bits(bits), wanted);
    }
    return total;
};
```

Both formats use eight-value local tiles, and these concrete executors have
eight 16-bit lanes on each supported ISA. Changing the compiled executor
therefore preserves this example's lane map. Other widths or geometries may
need different grains or an adapter; ISA names alone do not establish
compatibility.

## What crosses the composition boundary

The tag comparison returns a native mask that becomes the counter read's active
selection and the sum's selection. There is no required decoded array or scalar
bitmap round trip between those operations. The two operation objects remain
separate, so neither reader can silently use the other array's placement.

Three facts make that handoff valid:

- **Same original positions.** Both local formats map tile `t`, lane `i` to
  `8*t+i`. Equal register types or lane counts alone would not establish this.
- **Compatible masks and domains.** These executors use the same native mask
  encoding and 16-bit lanes. The tag domain is nine bits; the counter domain is
  twelve. The sum is unsigned modulo `2^64`, independent of either stored width.
- **Independent read permission.** Each actual source has two complete initialized
  tiles and its own stride. The final invocation selects only positions 8 and 9;
  readable zero slack does not become additional records.

`source`, `tag_source` and both byte arrays outlive their expressions and every
read. `describe` borrows the named source objects themselves. Keeping only the
arrays alive would not repair an expression referring to a moved or destroyed
view object.

The loop skips a region when its incoming selection is empty. It does not check
whether the tag comparison produces an empty mask: current native readers can
still load the admitted counter bytes in that case. Adding such a skip is an
execution choice to measure, not a permission to read an otherwise unavailable
source. This function does not implement suspension or early-exit signaling.

## Change the physical implementation

Start at the boundary whose cost you want to change. The logical operation can
stay the same while its readers, native consumer or enclosing driver change.

For example, `composition::read_payload` uses an `Ops::read_payload` member when
one exists, otherwise expanding the payload's body, tail and joins. A new native
executor can use that seam to share loads or fuse bit reconstruction. Its result
must preserve the active values, coordinates and required observations, and its
reads must fit each actual child's admission. A body and tail with matching types
can still refer to different sources; a fused reader cannot infer shared storage
from the types or silently recover a parent pointer.

A larger region is another choice. It may need more readable tiles, a different
lane map or adaptation between the tag and counter grains. Establish those facts
in the driver, before the region executes. The existing dense/grouped executors
show explicit region admission; they are not automatically interchangeable with
this eight-row call. Neither physical tile size nor external output width fixes
the best native work size.

For a critical computation, manual fusion can implement this same query directly.
For source reuse, the ordinary function can inline into a caller. The function's
small operation vocabulary also allows a recording implementation to inspect it;
record the actual function rather than maintaining a second handwritten graph.
General CPS calling conventions and retained state across erased or suspended
boundaries are still design work. The current register values are not a universal
pipeline ABI.

## Costs to keep visible

| Choice in this example | Work and consequence |
| --- | --- |
| Construction | Attachment and encoding happen before the query. The counter and tag arrays occupy 24 and 18 encoded bytes; their address envelopes are 28 and 21 bytes because of gaps. Small complete tiles need not save space over the input arrays. |
| Each nonempty incoming region | Two logical reads, one equality comparison and one sum. Each read here expands to body, tail and a join; these are logical operations, not an instruction or load count. A zero result mask still reaches the counter reader. |
| Eight-row grain | Two invocations cover ten values; the second has two active lanes. The supplied executors finalize a scalar sum each time. A deferred result could reduce less often, but must preserve the same arithmetic law. |
| Static composition | Format and executor choices are template facts, so the region needs no runtime format selection. Inline definitions are compiled in their callers; adding recipes and target combinations can increase compile time and code size. There is no promise that the compiler retains every temporary in a register. |
| Reusable compiled entry | A hot query can sit behind an ordinary function compiled once, with erasure at that boundary. Its call cost and lost inlining opportunities must be compared with the inline form in the real caller. No per-row dispatch is required by this algorithm. |

The program checks semantics, not competitiveness. Measure the complete consumer
against the strongest relevant alternative, including materialize-then-consume
when useful. Direct and composed forms producing the same code show that a wrapper
adds no cost there; they do not show that either implementation is good. Keep
runtime, build time and code size visible when choosing inline or fused work.

## Where to make and check changes

| Work | Source or tool |
| --- | --- |
| Author the logical operation | Start in its consumer. [composition.h](../include/ikea/seriespack/composition.h) supplies expressions, shared expansion and authored helpers. |
| Supply immediate native operations | [NEON](../include/ikea/seriespack/composition_neon.h) and [x86](../include/ikea/seriespack/composition_x86.h) executors bind fragments to actual sources; `native_*.h` and `detail/` contain their byte mechanisms. |
| Change checked admission or compiled execution | [view.cpp](../src/seriespack/view.cpp) admits placement; [operations.cpp](../src/seriespack/operations.cpp) owns checked calls, scalar binding and effects; [native.cpp](../src/seriespack/native.cpp) owns compiled native drivers. |
| Check substitution and coordinates | [Composition checks](../test/seriespack/composition.cpp) exercise symbolic recording, independent leaves and mapped selection; [native checks](../test/seriespack/native_composition.cpp) exercise real carriers and mixed representations. |
| Exercise the module | Build `ikea_seriespack_check` with the configured preset. For a changed path, choose the checks that challenge its values, bounds and actual dependencies. |
| Compare implementations | The [SeriesPack benchmark suite](../../workbench/benchmarks/seriespack/README.md) supplies recurring workloads and links retained evidence. Use the repository's CPU-pinning and sequential-measurement conventions. |

The [reference](reference.md) owns exact wire, access and mutation contracts.
A new computation does not need to absorb Engine schema decisions, summary
maintenance or Loom resource ownership into its native body. Put required
integration work in its enclosing driver and make the consumed state explicit.
