# Reusable native query controls

The phase-3 B12 controls extend the existing `frame_cells_controls` with
standalone predicate COUNT, supplied-row-mask SUM, independent consumed points
and materialized gathers. This page defines their callable contracts.
The [scorecard](SCORECARD.md) owns measured results and comparison limits;
the [system-query study](systems/README.md) owns the matched Frame/native,
DuckDB and Polars driver, exact result law and retained evidence.

The [qualified cells controls](../../frame-phase-2/instruments/cells/README.md)
already use genuine typed u8/u16/u32/u64 arrays, a global FOR base, bounded
narrow reductions and independent u32 halves for exact wide sums. The
[earlier scan consumer](../../frame-phase-1/scan/README.md) supplies standalone
COUNT and dense/sparse mask algorithms; the
[point study](../../frame-phase-1/point/README.md) establishes independent
original-row requests with duplicates. Those algorithms are reused here;
historical timing ratios do not qualify this source.

## Callable operations

Link `frame_cells_controls` and include its `cells.h`:

```cpp
frame_cells::native values(original_u64);
auto coarse = frame_cells::block_mask::bind(block_words, original_u64.size());
auto fine = frame_cells::row_mask::bind(row_words, original_u64.size());
auto points = frame_cells::point_requests::bind(request_rows, original_u64.size());
frame_cells::validate_native_selection(values, coarse, fine);
frame_cells::validate_native_points(values, coarse, points);

auto count = frame_cells::native_count_once(values, threshold, coarse);
auto selected = frame_cells::native_selected_sum_once(values, coarse, fine);
auto dense = frame_cells::native_selected_sum_dense_once(values, coarse, fine);
auto consumed = frame_cells::native_points_once(values, coarse, points);
frame_cells::validate_native_gather(values, coarse, points, output_u64);
frame_cells::native_gather_once(values, coarse, points, output_u64);
```

`COUNT` means `value >= threshold` and returns u64. It invokes no SUM kernel
and needs no second payload column. Whole-column bounds can answer all/none
without reading values. Standalone domain admission uses the existing
`validate_native_domains(values, coarse)`.

The SUM and point entries return `answer { u128 sum; u64 count; }`. Sums are
exact unsigned sums, including all base contributions, with zero for an empty
domain. They are not wrapping-u64 or SQL nullable SUM. Points count every
admitted request, including repeated requests, rather than distinct rows.
Request order is retained; exact integer aggregation is order independent.

`native_gather_once` instead writes every requested logical u64 in request
order, preserving duplicates. A request in a coarse-excluded block writes zero;
it does not load that payload. The output has exactly one element per request.
This is the matched boundary for arms that materialize point values; use the
separate exact `native_points_once` control when the demanded work is SUM/count.
Gather admission checks equal domains, exact output length and disjoint output
from the input owner/view objects, typed payload, coarse mask and request list.
It writes nothing on success or refusal. Request binding supplies the existing
per-row bounds proof; it is not repeated inside gather admission or execution.

Both selected-SUM routes **consume a supplied mask**; neither generates its
predicate. The sparse route walks partial words' set bits; the dense route
uses masked arithmetic over their 64 rows. Both use contiguous reductions for
full words. No unmeasured density threshold chooses between them. The
scorecard must identify the executed route and separately time a complete
predicate-to-SUM operation when that is the useful work. Existing
`native_pair_once` remains that fused two-column control.

## Domain, admission and physical access

- `native` owns immutable typed residual storage and its base/maximum.
  It must remain unmodified through repeated operations. This control's
  logical domain is non-null unsigned u64, even when residuals fit a narrower
  native lane. No signed, floating, NULL or missing-value meaning is supplied.
- Coarse bits name original 256-row blocks. The admitted payload and coarse
  domain contain equal complete blocks. Fine bits name original rows, not
  packed lanes or block ordinals. Binding checks fine-word size and unused tail
  bits; `validate_native_selection` additionally checks the payload domain.
- Point binding checks **every** requested row, including a request in a
  subsequently excluded block. `validate_native_points` checks the bound row
  domain against the payload. Requests are independent; no result supplies
  the next address. Four bounded accumulation chains consume them exactly.
- Bindings borrow their mask/request storage. Keep that storage immutable,
  readable and alive, including the derived all/none flags; rebind after
  changes. The `*_once` entries are unchecked repeated-work operations after
  this admission. Public aggregate fields do not permit forged proof objects.
- Coarse-none returns before payload, fine-mask words or point requests are
  acquired. Fine-none returns before payload. Excluded coarse blocks and empty
  fine words are skipped before payload access. The dense selected-SUM route
  may read inactive values within a nonempty 64-row word; its complete admitted
  coarse body remains readable. A fine query selection is not a read grant.
  Coarse-none gather still writes its complete output with zeros, without
  loading payload, coarse-mask words or point rows. Its writable output remains
  disjoint from all immutable inputs for the borrow.

`cells.h` still includes phase 2's private image types. These native entries and
the new checks **never construct or query that image**. They do not select or
revive the rejected shared-image architecture. The existing SUM/pair entry
points and old runner treatments are preserved.

## Qualification

`frame3_native_controls_check` compares against independent loops over original
logical u64 values. It covers every residual width 0..64, all four actual
native lane types, nonzero/high bases, zero and u64 extrema, exact large sums,
all/none/alternating/edge coarse domains, empty/full/partial/clustered/scattered
fine words, threshold endpoints, duplicated/reversed request lists and point
lists crossing the bounded accumulator chunk. Domain mismatches and invalid
requests and short/overlapping gather outputs refuse before repeated work,
preserving source and output bytes. Every gather result is checked independently
against original values in request order, with output guards. Protected pages in the actual native
allocation distinguish no-access-on-empty/coarse-exclusion from a deliberately
faulting selected read. The dense route's partial-word access is explicitly
broader than the sparse route's.

`frame3_native_controls_sanitize` instruments the check and the actual control
implementation with ASan/UBSan. Neither target runs a timer or constructs an
old image. Local Linux AArch64 checks do not qualify native Zen performance,
system-library comparisons, Loom staging or a whole MVCC mutation.

Standalone reproduction with the pinned compiler, from the SixDB root (prefix
Linux commands with `orb -m ubuntu` in this macOS workspace):

```sh
cmake -S workbench/spikes/frame-phase-3/controls \
  -B build/frame-helper/native-controls-v1 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -gline-tables-only' \
  -DSIXDB_SPIKES= -DSIXDB_BENCHMARKS= \
  -DSIXDB_MARCH=armv8-a+simd -DSIXDB_TUNE=neoverse-v2
cmake --build build/frame-helper/native-controls-v1 \
  --target frame3_native_controls_check frame3_native_controls_sanitize -j1
build/frame-helper/native-controls-v1/frame3_native_controls_check
build/frame-helper/native-controls-v1/frame3_native_controls_sanitize
```

The CMake file reuses an existing `frame_cells_controls` target when included
by the parent. Its standalone route compiles the same source without enabling
the old image runner. Root owns the phase-3 include and scorecard integration.
