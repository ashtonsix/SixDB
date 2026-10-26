# Build and read a Frame

Frame composes integer transformations over SeriesPack. Start with a recipe,
fit values into group sections, place those sections, validate granted contents,
then reuse a reader. Link `ikea::frame` and include `<ikea/frame.h>`.
The [ordinary example](../../examples/frame/ordinary.cpp) is a complete caller;
the [owner example](../../examples/frame/owner/history.cpp) retains interpretations
and backing through actual Orbital publication and conversion.

The initial value domain is non-null `uint64_t`. A binding unit contains up to
65,536 rows: 256 blocks of 256 rows, placed in groups of 16 blocks. A column can
contain any number of units; callers choose their placement and lifetime.
`build` constructs one unit. This is a binding boundary, not a 65,536-row column
format limit.

## Choose and describe a recipe

A description names a graph in child-before-parent order. Edges name both a
child identity and its coordinate mapping. It contains stored meaning, without
addresses, fit weights or operation plans.

```cpp
namespace fr = ikea::frame;
fr::description recipe;
recipe.nodes = {
    {fr::node_kind::pack, 0, 0, {}},
    {fr::node_kind::bound, 0, 0, {{0,fr::coordinate::identity}}},
};
recipe.root = 1; // bound -> pack, compact geometry
```

A `pack` node uses a resolved geometry family: `compact`, `bulk_x86` or
`bulk_arm`. The family stored in Fr/v1 has a frozen per-width byte law;
future tuning recommendations do not reinterpret existing data. A bound node
chooses a window per block and stores outliers in a group exception area.

An intervals root has two children: fields in original-row coordinates, and
checkpoints in a separate row domain. Both children can be pack or bound→pack.
Choose `intervals_sizes` for predecessor differences or
`intervals_relative_ends` for offsets from each checkpoint. G16 and G64 are the
initial tuned choices. Their checkpoint child is a separately placed stream,
with one checkpoint per G original rows. The [reference](reference.md) spells
out the admitted shapes and capabilities.

The recipe is a choice, not an instruction to compress every column. Plain
native columns remain useful for wide, dispersed and point-heavy work. Frame
returns fit facts; Engine chooses recipes using its workload and distribution
knowledge. Relative ends can improve SUM and points while widening fields.

## Fit, emit and place

```cpp
fr::fitting_policy policy; // penalty=0, exception_cap=64, slack=0
const auto fitted = fr::build(recipe, values, policy);
if (!fitted) { /* handle fitted.error() */ }
```

The result owns the emitted bytes. Each stream reports its description node,
role path, logical row count and group byte vectors. Fit census reports widths,
exception buckets and interval evidence. Policy prices exceptions and controls
capacity reservation; it never changes an existing image's meaning. Spare
exception capacity is available as a layout choice, with no initial write API.

The ordinary example builds `stream_manifest` locations over the emitted bytes.
A section location is `(owner_part, offset, extent)`. The ordinal is an owner
allocation identifier, not an Orbital type. A group, stream, allocation and
Orbital Part need not coincide: sections may share a Part if they do not overlap.
Unclaimed bytes in a Part are allowed. Each unit's manifest retains every
expected stream, including zero-group streams for an empty unit.

Owners can copy emitted sections into their own allocator or durable storage.
Retain immutable bytes at their admitted addresses through all readers. `built_unit`
may serve as backing for simple consumers; Frame does not retain it automatically.

## Attach, validate and reuse a reader

```cpp
fr::image_view placed{rows, part_extents, streams, parts, available};
const auto image = fr::validate_image(recipe, placed);
if (!image) { /* handle image.error() */ }
const auto reader = fr::bind_reader(*image);
const auto sum = reader.sum(); // exact unsigned 128-bit total
```

`attach_image(recipe, placed)` admits metadata and placement without reading
payload. `validate_image` also copies the description/manifest, attaches only
granted sections and proves their content premises. `bind_reader(image)` retains
that immutable proof and execution state without rereading payload. The convenience
`bind_reader(recipe, placed)` performs both steps.

`available` explicitly grants initialized, readable and read-isolated sections.
Physical co-location and a Part extent grant nothing by themselves. A missing
checkpoint dependency may attach, but operations needing it refuse. Only complete
root groups with their dependencies and content proof become readable. False
conservative monotone/exact flags disable shortcuts; an unsound true flag or a
false last-value summary refuses validation. The zero offset stored at checkpoint
rows is checked even though ordinary reconstruction ignores that field.

The validated image and readers own their interpretation and execution state,
so temporary description, manifest and Part wrappers can disappear. Payload
remains borrowed. Copies of a reader share immutable proof, without retaining
Orbital scopes or payload allocations. The owner must keep those alive.

## Operations and masks

`get(row)` reads one row; `read(first, output)` reads a contiguous range.
`sum(row_range{first, count}, blocks, rows)` and
`count(comparison, threshold, row_range{first, count}, blocks)` accept a logical
span in original unit-row coordinates. Coarse masks name blocks; optional fine
masks narrow SUM. Full interior runs keep their range laws while only partial
edges need ranged or masked treatment. Callers need not widen spans or build an
8 KiB unit bitmap. The whole-unit forms `sum(blocks)` and
`count(comparison, threshold, blocks)` remain available.
`filter(comparison, threshold, output, blocks)` writes an original-row `bitset256`
for each selected block. Other block slots remain untouched, and high tail bits
are clear. `sum_selected`, `select` and `summarise_selected` consume fine row
selections. The fused summary returns exact SUM, COUNT, MIN and MAX together;
empty summaries have count/sum/max zero and min `UINT64_MAX`.

- Coarse selections use `ikea::intervals::selection`, in **block** coordinates
  starting at zero. Storage must cover the unit's logical blocks.
- Fine selections use `ikea::seriespack::selection`, including
  `bits(0, span<const bitset256>(filter_output))` without a bitmap copy, in **original unit row**
  coordinates. For a span SUM, coverage is required only for the span's intersection with
  selected coarse blocks; whole-unit selected operations require coverage over
  those blocks. Fine origins remain absolute even when a span starts mid-block.
  An empty fine selection eliminates that block's payload demand.
- `all()` and `none()` need no bitmap backing. Padding bits beyond the logical
  unit or final block do not introduce rows or blocks.

For example, `reader.sum({3900, 4600}, coarse, fine)` selects rows in
`[3900,8500)`. A coarse mask can exclude the middle group completely. A fine
bitmap starting at row 3900 need cover only the selected intersections, and its
result is not rebased to zero. `count(op, threshold, span, coarse)` uses the same
span/coarse intersection without a fine argument.

All masks remain borrowed and stable through a call. Selections are demand,
not access grants. Checked calls admit the complete range, dependency closure,
comparison, mask coverage and output capacity before writing any result.
`select` preserves original row order and leaves the suffix beyond its returned
count untouched. Outputs must be disjoint from input masks, source bytes and
reader state. Bad allocation can refuse private construction/validation; it
cannot publish a partial replacement.

Some predicates settle from the non-null unsigned domain: `>=0` and `<=MAX`
match all requested rows; `<0` and `>MAX` match none. COUNT/FILTER answer those
without touching unavailable payload. Other operations never silently treat
missing sections as zeros.

## Integrate with owners and Loom

`required_sections` and `reader.required(ranges)` return conservative point/read
dependency closures before acquisition. Acquire those sections through the owner
or Loom, then validate/bind using explicit grants. A closure is neither a read
grant nor an owner lease. Frame does not suspend or acquire storage internally.
Owners keep interpretation, placement, proof and backing coherent across waits.
`reader.required(span, coarse)` derives demand from the same span/block lowering
used by execution: a completely masked group in the middle demands no section.
It includes checkpoint dependencies, even when their rows are outside the
selected root-row set. Fine selections and contribution multiplicities do not
alter the meaning of those dependencies.

A conversion constructs new images, proves equivalent logical values, then uses
its owner's adoption/publication operation. Old readers retain their original
meaning. The owner example checks sizes↔relative-ends conversion at the same
logical version, external value replacement at a new version, independent
checkpoint placement, retained old readers and reclamation after release.

Frame initially has no point mutation or checked private preparation operation.
The research `prepare_group` and its copy/rebind/rebuild scratch are excluded.
Constructing and publishing an external replacement is useful, but is not a
competitive point-write claim. Future changes must still distinguish logical
contributions, issued bytes and invalidated evidence; those protocols are research.

Measured strengths and limits are source-qualified in the
[landing qualification](../../../workbench/spikes/frame-phase-3/GRADUATION.md) and
[span/seam qualification](../../../workbench/spikes/frame-seam/NATIVE.md).
Wide SUM, dispersed predicates and independent points can lose to native arrays;
encoding is not a universal default.
