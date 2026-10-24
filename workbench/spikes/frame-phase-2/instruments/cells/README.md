# Complete queries on the shared image

Front A owns this instrument and its reusable native controls. It consumes B's
unchanged [image](../../substrate/image.h) and C's header leaves; it introduces
no Frame representation or Engine interface.

The [first native findings](FINDINGS.md) retain small-query losses and useful
concurrent capacity reversals on all three ISA profiles.

The [phase-1 scan findings](../../../frame-phase-1/scan/FINDINGS.md) establish
compressed pruning and selected-consumption wins, alongside a scattered-cold
loss. Their corrected native FOR loops, exact arithmetic, balanced ordering and
actual-buffer conditioning are reused. B's
[shared image](../../substrate/README.md#the-shared-image-substrate-over-front-cs-leaves)
removes the old body implementation from that question. A's
[calibration](../FINDINGS.md) demonstrates why unequal worker finishing times
can make an equal-work batch misleading as a saturation ceiling. This instrument
therefore measures complete queries with actual common-window intervals.

## Cells and controls

A cell is SUM(B), or SUM(B),COUNT(*) WHERE A>=threshold, optionally restricted
by a supplied one-bit-per-block mask. Inputs are non-null stored-domain u64 in
complete 256-row blocks. Sums are exact u128. Empty selection returns zero and
count zero; this is not SQL's NULL-SUM contract.

Each pinned worker constructs its private pool of logically identical cells.
Increasing pool size increases working set while preserving values, row
selection, threshold, result and per-cell layout. Sequential or deterministically
shuffled pool visits never repeat a replica within a pass. This is a many-small-
queries forcing workload, not automatically a whole-column scan. `--cell-blocks`
also permits large complete queries. `prefix` uses the first blocks; `stratified`
takes evenly spaced original complete blocks, retaining their internal row order
and A/B alignment. Both are forcing slices, not population samples. Source block
IDs are recorded; incomplete source-file tails are outside the admitted query.

Three treatments and exact duplicates use a six-treatment Williams schedule:
each position and ordered within-trial predecessor pair is balanced over six
trials. SUM compares segment-hoisted Frame, native narrow FOR and Frame's kind
index. Pair queries compare per-block fused Frame, native fused and two-stage
Frame masks then payload. Coarse exclusion precedes descriptor/body access.
Indexed SUM under coarse exclusion filters its index then uses per-block leaves;
it does not claim the all-domain index's fully hoisted kernel.

Native arrays have actual typed u8/u16/u32/u64 storage. Bounded narrow accumulators
and independent u64 halves preserve exact sums and vectorization. Pair controls
specialize all 16 lane combinations outside row loops. Global native FOR bounds
resolve trivial all/none predicates. Frame uses its own per-block evidence;
there is no hidden additional global Frame pruning index.

The private `frame_cells_controls` target exports `cells.h`:

```cpp
frame_cells::native a(original_a), b(original_b);
auto selected = frame_cells::block_mask::bind(coarse_words, rows);
frame_cells::validate_native_domains(b, selected, &a); // once, before repeated work
auto sum = frame_cells::native_sum_once(b, selected);
auto pair = frame_cells::native_pair_once(a, b, threshold, selected);
```

Owners and bound mask stay immutable and alive. Mask binding validates mask
geometry; `validate_native_domains` additionally admits equal complete operand
extents. The repeated-work entry points are unchecked after that admission.
These are the same compiled kernels as the
instrument and require no Frame image. Reuse does not transfer residency claims.

## Execution and accounting

`warm-repeat` conditions one pool pass, synchronizes workers and runs to a common
deadline. Clock checks follow bounded quanta of complete queries (default 64).
Per-worker work, timestamps, CPU time, actual CPU and distinct replicas are
retained. Every replica/arm is independently checked before timing. Every timed
result is compared to the oracle into a mismatch flag; flags and accumulated
exact results are inspected only after all timers stop. Complete-query/quantum
overshoot remains visible; actual overlap determines the saturation claim.

`flushed-first-pass` tests instruction availability in a child before threading,
uses a stride no greater than actual L1 data line size, flushes the selected
arm's actual input allocations, then queries each replica once. This is a
synchronized completed batch, not repeated flushed-query saturation. Tiny
single-replica batches include substantial clock/control overhead and cannot
settle close ranks. Successful cache maintenance alone does not prove service
from any particular tier.

Execution and conditioning borrow the same noncopyable owners. Reported ranges
include all descriptor planes, initialized 256-byte image suffix, native arrays
and the indexed treatment's index. Request masks, visitation order, object headers
and scratch are not deliberately flushed. This does not prove they stay hot:
pool size and shared allocator boundary lines can evict them. Their bytes are
separate from logical input ranges and vector capacities. Logical range bytes
include padding and are not measured traffic. All representations coexist; the
active arm footprint and total process allocation differ. Process smaps/NUMA maps
are captured once per cohort. C's halo is admitted by an immutable whole-image
window; this instrument claims no exact-boundary route. Inline assembly in C's
leaves is outside some sanitizer instrumentation.

The [PMU limits](../FINDINGS.md#counter-limits) still apply. Footprint does not
prove serving tier; a small guest's throughput is not socket bandwidth. Initial
native runs use small guests, not a full-host sweep.

## Build and check

Select `frame-phase-1;frame-phase-2` in the ordinary root CMake build, then build
`frame_cells` and optionally `frame_cells_sanitize`. Run either with `--check`;
`--help` describes bounded workload options. The sanitizer target instruments
the C++ caller and included leaves, not all Ikea or external peers.

The functional matrix independently checks every native lane pair, mixed
constant/run/plane/wide blocks, nonzero bases, exact wide sums, coarse all/none/
scattered/clustered masks, threshold boundaries, both placements, poisoned scratch,
empty input and wholly excluded payload. It also verifies shared-image points
against original rows. Functional success supplies no timing rank.
