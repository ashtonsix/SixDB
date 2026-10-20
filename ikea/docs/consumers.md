# Building fast consumers

Start with the useful operation: a lookup, a filtered group, a scan or a
replacement with its effects. Bind its stable format and placement once, then
keep decoding, computation and reduction together while the data is ready.
Ikea's ordinary calls are the starting point; the `author/` surfaces let a consumer reuse the same
bodies without materializing intermediate arrays.

There is no single fastest execution style. A narrow packed comparison can beat
decoding into shared lanes; a prepared endpoint can beat a larger inline body;
a branch can improve dependent latency and hurt reciprocal throughput. The
choices below are supported by the linked complete-consumer measurements.

## Choose by the work you need

| Consumer | Start here | Avoid paying for |
| --- | --- | --- |
| One SeriesPack or TuplePack predicate | The [ordinary filter](filters.md); it can compare packed bits directly | Decoding an array just to compare it, or entering CPS for one cheap predicate |
| Several predicates over the same values | Reuse one native read/projection and combine masks inline; compare with direct packed filters | Repeated reconstruction, unless the packed path is cheaper than reconstruction itself |
| Small TuplePack word computation | An 8-byte packet over several useful rows; for scattered codes also try the 64-byte native carrier with the same rows | Repeated point-call admission or assuming a smaller carrier always costs less |
| Sequential StreamPack traversal | `range`, which carries the reservation position | Recomputing a Width prefix for every rank |
| Selected StreamPack values | Point decoding for sparse requests; scratch-backed `select` for dense reuse | Decoding every start for a handful of ranks, or repeatedly decoding for many ranks |
| Bec256 Boolean work and count | Decode independent blocks as a native pair, consume and reduce in registers | A materialized bitset when only a count or encoded result is needed |
| Runtime choice among formats | Select a useful whole operation outside its inner traversal | An indirect call or format dispatch for every small child step |
| Many formats feeding a substantial common computation | Normalize once into a shared native consumer at an existing composition boundary | Sharing that removes a packed fast path or creates a stage per comparison |
| Ordered directory points/ranges | [Boundlist](boundlist/usage.md): one exact-max search for unique-key routing; occupied min/max for range edges | Two searches for a unique point, or interpreting equal endpoints as empty |
| Integer-key set/map | [Hashtable usage recipes](hashtable/usage.md#choose-the-recipe-for-the-work): choose primary geometry for distinct work and bind a compatible native recipe once | A one-bucket scratch map that silently becomes mostly Swiss, or erased ownership/dispatch per key |
| Independent cold hash reads/writes | [Future-home hints](hashtable/usage.md#overlap-independent-requests) in the complete caller loop | Treating plain point timings as scheduled throughput, or retaining lookup state across writes |
| Cold independent lookups | The owner's [Loom direct/Ring choice](../../loom/docs/execution.md) | Scheduler boundaries between ready arithmetic steps, or treating prefetch as readiness |

The module guides turn these starting points into concrete calls:
[SeriesPack](seriespack/usage.md), [TuplePack](tuplepack/usage.md),
[StreamPack](streampack/usage.md), [Bec256](bec256/usage.md) and
[Hashtable](hashtable/usage.md).

## Move stable work out of the loop

Preparation chooses controls; binding qualifies those controls against named
storage. Keep layouts, maps, groups, native transfer qualification and expression
selection outside repeated work. For StreamPack, dispatch around the complete
typed consumer and use `rebind` only with its required shared layout identity.
Use range or packet calls when the operation naturally covers several rows.

Checked calls remain useful when commands vary. Trusted `_unchecked` entries
reuse proofs; they do not create them. The owner must still establish each
window's bounds, activity, initialized bytes, aliasing, effects capacity and
content stability. A proof for one mask does not admit extra rows. Growth,
relocation or a changed interpretation can invalidate bindings even when a
pointer happens to stay equal. [Owner integration](integration.md) explains the
lifetime boundary.

Keep known placement visible to an inlined consumer. A compile-time format does
not make its plane strides or projection controls compile-time constants. The
[guarded-filter recheck](../../workbench/spikes/packed-filters/EARLY-GRADUATION.md)
measures the cost of general bound views beside consumers with visible contiguous
placement. This is a reason to preserve information across the caller boundary,
not to run checked attachment in every iteration or assume tight storage without
proof. Keep a general prepared path for genuinely variable placement.

## Share the work, preserve the fast algorithm

For a range predicate such as `lo <= x && x < hi`, reconstruct once and evaluate
both comparisons together when reconstruction is needed. The
[ordinary filter example](../examples/filters/ordinary.cpp) shares a TuplePack
projection; the [composition example](../examples/filters/composition.cpp)
normalizes two SeriesPack formats to one 16-bit range-and-count consumer.

Canonical lanes mean values with the same unsigned meaning, lane width and
coordinates, regardless of their original packed width. They can remove a
specialization dimension from downstream code. They are not a requirement for
every filter: narrow Local bitplanes and tight TuplePack byte columns have
direct packed comparisons worth retaining. A carrier match alone also does not
prove matching coordinates or value meaning.

Keep native results in registers until the consumer needs stored output.
Reduce to a count or mask before returning when that is the actual result.
TuplePack's packet grouping can put jointly consumed codes together without
changing their physical storage. For small word consumers, qualify a supported
transfer once with `admit_transfer<Bytes>()`; it removes repeated transfer-route
selection, but does not admit rows or retain backing. The
[TuplePack author guide](tuplepack/extending.md#keep-native-payloads-native)
gives the fallback and lifetime rules.

Ikea continuation-passing (CPS) chains pass native values between stages; they
do not suspend into Loom. Keep adjacent cheap predicates in one body. If a larger
operation already needs a chain, a shared decode/consume boundary can reduce
specialization without paying another ordinary-to-chain entry. The
[corrected runtime comparison](../../workbench/spikes/filter-composition/MASK-GRADUATION.md)
favors prepared fusion in the measured mixed catalogues. Earlier x86 throughput
wins used correlated stream addresses and did not survive the trace correction.
Sharing can still reduce code size; qualify its complete consumer at the boundary
you actually need. There is no automatic catalogue-size threshold or universal
CPS policy. Enter through the supported
chain `run` and leave explicit waits to the owner after return.

## Treat masks and guards as execution choices

Preserve original coordinates. A SeriesPack filter bit names a position in its
requested window; a bound TuplePack filter bit names an original row; a native
TuplePack `filter_values` bit names a packet byte. Grouped maps and holes require
the plan's ordering and row-to-byte mask, not a cast between these masks.

Sparse activity can select a different reader, notably for TuplePack projections.
When a conjunction has already admitted the original input window, it can guard
the next call on a nonzero result, pass the **original** activity on survival,
then intersect the masks.
This can retain the dense reader. Where the reader guarantees no access to
inactive rows, as TuplePack does, retain a sparse mask if only those rows are
readable. SeriesPack's complete-window requirements still apply. Its ordinary
packed filter masks once after bounded native comparisons; it does not require
a sparse per-row decoding strategy. Use
one window call for 8, 16, 24, …, 64 rows instead of eight point calls or padding
to sixteen. Byte-aligned payloads are supported. Do not widen reads merely for speed.

For an existing SeriesPack eight-bit head with a nonempty payload,
`composition::guarded(expression)` can skip suffix decoding when the head decides
every active lane. Apply it after child substitution. Keep ordinary defaults
when suffix work is cheap or head ties are common/unpredictable. Final match
selectivity alone cannot predict the saving: a zero-result window may still need
every suffix. Ordered predicates can finish early with a nonzero mask.
The [guarded example](../examples/filters/guarded.cpp) combines head rejection
with a subsequent TuplePack projection; [filter guidance](filters.md) owns the
exact calls. The [measurements](../../workbench/spikes/packed-filters/EARLY-RESULTS.md)
retain branch losses as well as gains.

## Include writes and ownership in the choice

Prepare mutation bindings and budget effects for the complete useful operation.
A wider store can preserve logical neighbors yet reissue their bytes, changing
protection and journal costs. Compare complete replacement operations with the
owner's actual access grants, not only their arithmetic. Range traversal can
amortize admission and coalesce effects; the capacity proof must not assume that
coalescing will happen.

Request only the summary work the owner needs: SeriesPack can omit `sum_change`,
and TuplePack observation can request before-values, after-values, both or
invalidation coordinates. Keep publication and any persistent summary writes
explicit. A scratch-only comparison with weaker effect obligations is not a
measurement of the MVCC mutation. See
[effects and publication](integration.md#consume-effects-and-publish).

## Verify the remaining consumer-specific choice

Use maintained examples for the contract and retained measurements to narrow
the alternatives. Measure the useful operation, including real entry/return,
context loads, projection, masking, effects and result handling. Count any paid
scratch preparation or binding that the application cannot reuse. Do not turn
this into another full layout search before trying the supported starting path.

Keep dependent latency, independent reciprocal throughput and memory separate.
A dependent chain waits for each result before choosing the next request;
reciprocal throughput is time per operation with independent requests in flight.
Report units per useful operation, selected density, working set and actual
platform; resident warm microbenchmarks do not establish DRAM behavior. Inspect
the inlined caller for repeated dispatch, helper calls, stack materialization and
the intended dependency chain. Explicit SIMD bodies still need that inspection;
their presence alone does not prove an efficient composition. Use the pinned
[build profiles](../../BUILDING.md) and shared
[measurement tools](../../workbench/benchmarks/README.md).
