# Using TuplePack

TuplePack holds byte-contained unsigned codes in small fixed-layout units. Use it
for transactional projections and updates; use SeriesPack for packed integer
arrays. Neither primitive owns application field semantics or chooses a schema.
Supply the layout and operation maps manually. Automatic layout analysis is
experimental; higher-level components need a layout-selection policy from Engine.

Link `ikea::tuplepack` and include `<ikea/tuplepack.h>`. Start with the executable
[ordinary example](../../examples/tuplepack/ordinary.cpp), built as
`ikea_example_tuplepack_ordinary`. [Reference](reference.md) owns exact contracts;
[composition](extending.md) covers native callers and substituted children.

## Describe bytes, then place them

The example has a flag, seven-bit rank and three-bit tag:

```text
physical byte 0:  r r r r r r r f      bit 7 ... bit 0
physical byte 1:  . . . . . t t t      '.' is spare, with no value semantics
code rank:       0=flag, 1=rank, 2=tag
```

```cpp
std::array<tp::code, 3> codes{{{0, 0, 1}, {0, 1, 7}, {1, 0, 3}}};
auto format = tp::layout::make(2, codes); // check expected before dereferencing
auto placed = tp::view::bind(*format, bytes, 2, 8, 2);
```

`view::bind` places two units at storage offsets 2 and 10: unit bytes=2,
stride=8, unit offset=2. The enclosing allocation belongs to the caller. The
last unit needs only its own two bytes, not a full final stride. A read-only
owner uses `const_view`. Views admit extents and address arithmetic, not the
meaning of existing bytes: the caller must bind the description that encoded them.

Keep each named view and prepared plan at a stable address while operations
borrow them. Reader/writer preparation copies the controls, so the original
description and map need not survive. A view's raw storage stays alive and its
placement remains unchanged. A move of a containing work object can invalidate
self-references; the integration example explicitly prevents such moves.

For a larger record, bind separate units with the larger record stride and their
own offsets. For column-like access, place units in separate 16/32-byte planes.
Changing placement is independent of changing code widths, maps or execution
style. Rebinding does not migrate bytes; different segments can retain different
descriptions indefinitely.

## Construct, project and replace

`constructor::make(format)` prepares complete rank-ordered initialization.
`construction_input` has 128 byte slots; only defined codes are consumed. Bind
it with `bind_constructor`, then call `initialize(first, inputs, effects)`.
The command validates every selected code before zeroing any destination unit.
It initializes spare bits to zero and preserves bytes outside each unit.

Readers and replacement writers select ordered code ranks:

```cpp
std::array<tp::byte, 4> map{1, tp::hole, 0, 2};
auto plan = tp::reader<8>::make(*format, map);
auto read = tp::bind_reader(*plan, *placed);
auto result = read->get(0);
```

`reader<8>` returns a uint64_t whose low byte is code 1, next byte zero,
third byte code 0 and fourth byte code 2. All unused slots are zero. Duplicate
read ranks are allowed. `reader<64>` returns a 64-byte array for ordinary buffered
use; native authors can keep the same result in registers. A short map is padded
with holes. A map cannot exceed its reader's 8/64-slot capacity.

`writer<8>` accepts the scalar packet and `writer<64>` the byte array. Bind with
`bind_writer`, then use `set(row, input, effects)` or
`replace(first, inputs, effects, selection)`. Writers reject duplicate ranks at
preparation and out-of-width selected values at invocation. Hole slots are
ignored, even if nonzero. Replacement preserves unselected codes and spare bits.

`read(first, outputs, selection)` and `replace` use original row coordinates.
`selection::bits(origin, words)` supplies 64 original-row bits per word; the origin
need not be a multiple of 64. Inactive rows produce zero read output and issue no
payload accesses or mutations. Zero output is not evidence of absence. Selection
storage and all input slots remain stable through admission and execution.

## Effects and failure

Provide preallocated `ikea::source_write_journal` storage. Construction requires
one entry per initialized row. `writer::effect_capacity()` bounds entries per
selected replacement row before coalescing. Effects identify the actual named
view and plane-zero byte spans relative to its storage, including the unit offset.
They describe issued writes, including preserved neighboring bits; unchanged
values can still generate effects. Owner isolation must protect those bytes.

Checked range, value and capacity errors leave the **whole call's** data,
maintenance and effect output unchanged. Earlier completed calls remain completed.
Checked operations do not prove arbitrary object lifetimes, locking or aliasing of
input/plan/selection/output metadata. Those are explicit owner obligations.
Effect callbacks must have sufficient admitted capacity and cannot fail or suspend.

Trusted `_unchecked` entries reuse these proofs. A plan's raw
`set_unchecked(byte*, input)` emits no effects; use the bound operation's trusted
entry to retain its pre-write coverage. Native kernel authors can fuse bodies
inside that admitted boundary. Nothing here acquires storage or publishes data.

## Build and validate

Use the [pinned Linux toolchain](../../../BUILDING.md). Configure the desired ISA,
then build `ikea_validate` for both Ikea modules, or just
`ikea_tuplepack_wire_check`, `ikea_tuplepack_operations_check`,
`ikea_tuplepack_execution_check`, `ikea_tuplepack_ownership_check` and the examples.
`python3 ikea/test/headers.py BUILD` checks header independence.
The [routine benchmark suite](../../../workbench/benchmarks/tuplepack/README.md)
distinguishes bodies, ordinary calls and matched effects controls.
