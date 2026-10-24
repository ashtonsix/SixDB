# Representations and planning

[Ikea's source guide](../../ikea/docs/source.md) and
[consumer guide](../../ikea/docs/consumers.md) are the starting points for current
capabilities. Research below explains choices and alternatives; captured spike
formats and proposed analysers are not additional production contracts.

[Interface cohesion](../spikes/ikea-composition/interface-cohesion.md) checks shared
vocabulary against actual consumers: selection grain versus storage words,
row/packet coordinates, source-qualified effects, public point leaves and
module-owned errors. The [broader cohesion decisions](../spikes/ikea-composition/interface-proposal.md)
replace its overly conservative vocabulary, byte and schema conclusions while
retaining coordinate, exact-access and owner-lifetime requirements. Those findings
do not establish that coherent boundary types are slow.

The follow-up [reusable execution survey](../spikes/ikea-composition/reusable-execution.md)
examines small bindings, shared controls and cheap updates across blocks. It
retains checked admission, exact grants and content/owner identity while testing
actual caller ABI and complete mixed commands. Implemented predicate bindings
and experimental shared controls retain separate qualification.

## Match the representation to useful work

| Question | Route through the evidence |
| --- | --- |
| Which Frame encoding stages are Ikea's to fuse and which are Engine's to compose, and how should Frame win per operation and tier? | [Frame phase 2](../spikes/frame-phase-2/README.md): [stage classification](../spikes/frame-phase-2/STAGES.md), [mechanisms and targets](../spikes/frame-phase-2/MECHANISMS.md); proposals awaiting measurement |
| What should Frame expose for computation over encoded data? | [Frame's architectural review with Engine](../spikes/frame-phase-1/REVIEW.md): retained structure, selected residual algebra, schema fidelity, cross-column alternatives and mutation lifecycles; research directions, not an implemented module |
| Do compressed Frame programs repay their cost through real writes and reads? | [Owner program costs](../spikes/frame-phase-1/lifecycle/PROGRAM-COSTS.md) and [warm Zen 5 AVX2/AVX-512](../spikes/frame-phase-1/lifecycle/ZEN5.md): native scans reverse several ARM rankings; retain final-fold costs, strong native forms and losing cases |
| Row layout, point access, mixed reads/writes and scan batching | [Tuple-layout viability](../spikes/tuple-layout/viability.md), [implementation decisions](../spikes/tuple-layout/implementation-decisions.md), then [current TuplePack](../../ikea/docs/tuplepack/usage.md); NEON sparse packets in [NEON sparse TuplePack packets](../spikes/neon-tuplepack-masked/README.md) |
| Planes, projections, metadata and placement selected together | [Layout-analyser findings](../spikes/layout-analyser/findings.md), [connected case study](../spikes/layout-analyser/case-study.md), [executable reference](../spikes/layout-analyser/tuplepack-reference/README.md) |
| Variable bytes, offsets, sidecars and mutation | [StreamPack recommendation](../spikes/streampack/RECOMMENDATION.md), [results](../spikes/streampack/RESULTS.md), [current usage](../../ikea/docs/streampack/usage.md) |
| Shared interval geometry across value storage, routing and encoded frames | [Maintained Intervals](../../ikea/docs/intervals/usage.md) and [graduation](../spikes/intervals/GRADUATION.md), with the [live caller audit](../spikes/intervals/AUDIT.md): StreamPack owns compact retirement; sparse spacing is independent of execution grain; the [native search study](../spikes/intervals-search/README.md) owns the shared boundary step and its read grant |
| Ordered boundaries versus value storage | [Boundlist recommendation](../spikes/boundlist/RECOMMENDATION.md), [ownership](../spikes/boundlist/OWNERSHIP.md), [consumer portfolio](../spikes/boundlist/PORTFOLIO.md) |
| NEON cost of Local bit-plane residuals | [NEON Local transpose](../spikes/neon-local-transpose/README.md), including its [preset re-measure](../spikes/neon-local-transpose/README.md#preset-follow-up) (keep `bulk_arm`) and [mixed reads](../spikes/neon-local-transpose/README.md#mixed-local-reads) |
| Real encoded column bodies: block kinds, dictionaries, checkpoints, bytes against DuckDB/Parquet/ZSTD, encoded SUM/COUNT | [Frame encoding findings](../spikes/frame-phase-1/encoding/README.md): run-end bodies, delta checkpoint options, exceptions as insurance, real part dictionaries, the [non-decreasing delta COUNT](../spikes/frame-phase-1/encoding/README.md#count-over-non-decreasing-delta-blocks), [borrowed block extents](../spikes/frame-phase-1/encoding/README.md#borrowed-blocks), [arrival-order dictionaries](../spikes/frame-phase-1/encoding/README.md#arrival-order-dictionaries) with their [cross-call caches](../spikes/frame-phase-1/encoding/README.md#dictionary-caches-across-calls) (hash bytes near ZSTD-3, at higher scan cost), [wide exact SUM by halves](../spikes/frame-phase-1/encoding/README.md#wide-exact-sum-by-32-bit-halves), and [row-selected aggregates with NULL fill](../spikes/frame-phase-1/encoding/README.md#row-selected-aggregates-and-null-fill) (predictor fill costs bodies nothing; compaction pays only in packed/delta-heavy, NULL-heavy blocks), and [fused aggregates](../spikes/frame-phase-1/encoding/README.md#fused-aggregates) (one pass for SUM + COUNT: 0.58–0.74× separate on packed/run columns, no gain where COUNT is closed form) |
| Predictive coding and complete analysis/encoding work | [Bec256 composition](../spikes/bec256-composition/README.md), [residual-bound questions](pfor-bounds.md) |
| Larger bitsets as one body: shared level core, fused multi-block kernels, block access versus whole-tree layout | [Bec4096 guide](../../ikea/docs/bec/bec4096.md), [graduation study](../spikes/bec4096/README.md) |

Physical tile size, execution grain, working lanes and result representation
can vary independently. [SeriesPack history](../spikes/ikea-composition/seriespack-history.md)
retains the cases where a direct authored body matched its control yet both lost
to materialization. A layout analyser must charge complete mixed workloads;
isolated kernel costs can choose the wrong layout.

## Filter before decoding, or decode and consume?

[Packed-filter results](../spikes/packed-filters/RESULTS.md) separate dependent
latency from independent throughput. Their
[early-exit qualification](../spikes/packed-filters/EARLY-GRADUATION.md) separates
stopping work from layout and fusion. [Filter composition](../spikes/filter-composition/README.md)
contains the compiler/runtime tradeoffs, corrected throughput traces and ISA
differences behind the maintained [filter contract](../../ikea/docs/filters.md).

For strings and general Boolean expressions, read
[regexp conclusions](../spikes/regexp-lowering/CONCLUSIONS.md) beside
[row-signature findings](../spikes/row-filter-signatures/FINDINGS.md).
Avoided RE2 calls or Boolean evaluations are counts, not measured speedups.
Cheap resident columns can favor direct evaluation while expensive text or
joint block summaries justify an extra filter. The
[secondary-summary note](secondary-summaries.md) separates pruning from estimation
and approximate answers.

[Frame's complete head consumers](../spikes/frame-phase-1/filter/FINDINGS.md) compare
logical H8/H16, residual heads, exact block bounds and quantised window bounds
with actual native/Polars controls. Logical H16 helps predicted Jena queries but
adds two bytes/row; RegionID and the losing window-bound program rule out a
universal evidence layout. [Point integration](../spikes/frame-phase-1/point/INTEGRATION.md)
preserves native-prefix gains while retaining the substantial native-array gap.
Its [curated run-source follow-up](../spikes/frame-phase-1/point/CURATED-RUNS.md)
tests the maintained Intervals native source in the real point readers, while
keeping the predictable projection and non-RLE controls.

## Conversion, append growth and changing interpretation

[Object integration outcomes](../spikes/orbital-mvcc/OUTCOMES.md) connect actual
SeriesPack/TuplePack conversion, old readers and changed Part counts to current
Object checks. The [performance evidence map](../spikes/orbital-mvcc/performance/README.md)
locates append-copy grain, shared-prefix construction and conversion capacity
findings. Charge construction, simultaneous source/target ownership, later
consumption and retirement; a smaller target body alone does not price conversion.

Keep shared immutable geometry separate from per-realization counts and backing.
The [composed Shape study](../spikes/orbital-mvcc/metadata/composed/README.md)
includes equal byte extents with unequal logical counts. Its
[Schema construction negative](../spikes/orbital-mvcc/performance/FEEDBACK.md#transient-schema-construction-phase)
saved resident metadata but made repeated tiny Tuple construction roughly nine
times slower. Shrinking metadata is useful only when it serves performance and
sensible ownership. [Memory and lifetimes](memory-and-lifetimes.md) covers the
related physical accounting.

## Plans that keep improving

Ashton's proposed Engine model retains plans and equivalence graphs, accumulating
improvements rather than only caching an executable. Periodic probes could
change how the remaining work of a large scan runs. The
[analysis/execution discussion](../spikes/ikea-composition/sketches.md#clarification-analysis-and-execution-surfaces)
and [composition design](../spikes/ikea-composition/design.md) explore the split
between deep inspection during analysis and a small bound execution call.
This remains a design direction, not an implemented optimiser.

[Value semantics and integration](../spikes/ikea-composition/semantics-and-integration.md)
keeps semantic laws, resource borrowing and mutation consequences visible across
substitution. An encoded-domain comparison does not justify arithmetic there.
Floating-point addition can change with regrouping even without fast-math;
the [aggregate semantics note](../spikes/aggregate-maintenance/design.md#separate-the-aggregate-semantics)
retains the numeric question and ReproBLAS lead.

Empirical cost evidence has different validity rules from semantic equivalence.
[Executable-placement findings](../spikes/executable-placement/README.md) show
that relinking unchanged instructions can change their measured cost.
[Execution and code generation](execution-and-codegen.md) connects those effects
to composition boundaries, code size and compiler resources.
